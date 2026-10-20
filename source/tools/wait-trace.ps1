# Adds the wait trace (diagnostics) to the SDK, then build with:
#   .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$inc = @'
// [online] Wait trace: guest waits and sleeps of 20..250 ms, grouped by the guest
// code that asked for them (LR). Every 5 s one line per caller: how many waits,
// the timeout asked for, how long they really took and how many ran to the
// timeout. Finds the loop that paces online matches (they stall every ~100 ms).
namespace {
struct WaitStat {
  const char* name = "";
  uint32_t tid = 0;
  int n = 0, timed_out = 0;
  double asked = 0, took = 0;
};
struct WaitTrace {
  const char* name;
  int64_t timeout;
  std::chrono::steady_clock::time_point start;
  WaitTrace(const char* n, int64_t t) : name(n), timeout(t), start(std::chrono::steady_clock::now()) {}
  ~WaitTrace() {
    if (timeout >= 0) return;  // none, or an absolute time
    double ms = double(-timeout) / 10000.0;
    if (ms < 20 || ms > 250) return;
    auto now = std::chrono::steady_clock::now();
    double took = std::chrono::duration<double, std::milli>(now - start).count();
    uint32_t lr = 0, tid = 0;
    if (auto* th = rex::system::XThread::GetCurrentThread()) {
      tid = th->thread_id();
      if (th->thread_state() && th->thread_state()->context())
        lr = uint32_t(th->thread_state()->context()->lr);
    }
    static std::mutex mu;
    static std::map<uint32_t, WaitStat> stats;
    static auto last = now;
    std::lock_guard<std::mutex> lock(mu);
    WaitStat& st = stats[lr];
    st.name = name;
    st.tid = tid;
    ++st.n;
    st.asked += ms;
    st.took += took;
    if (took >= ms * 0.9) ++st.timed_out;
    if (now - last >= std::chrono::seconds(5)) {
      last = now;
      for (auto& [l, s] : stats)
        REXKRNL_INFO("[wait] lr {:08X} thread {:X} {}: {} waits/5s, asked {:.0f} ms, took {:.1f} ms avg, {} ran to the timeout",
                     l, s.tid, s.name, s.n, s.asked / s.n, s.took / s.n, s.timed_out);
      stats.clear();
    }
  }
};
}  // namespace
'@
$inc = $inc.Replace("`r`n", "`n") + "`n"

function Patch($rel, $funcs, $before, $incFrom, $incTo) {
  $file = Join-Path $Sdk $rel
  $raw = [IO.File]::ReadAllText($file)
  $crlf = $raw.Contains("`r`n")
  $s = $raw.Replace("`r`n", "`n")
  if ($s.Contains("[online] Wait trace")) { Write-Host "$rel already has the wait trace" -ForegroundColor Yellow; return }
  $i = $s.IndexOf($before)
  if ($i -lt 0) { throw "$rel : '$before' not found (nothing changed)" }
  if ($s.IndexOf($incFrom) -lt 0) { throw "$rel : include anchor not found (nothing changed)" }
  foreach ($f in $funcs) { if ($s.IndexOf($f[0]) -lt 0) { throw "$rel : $($f[0]) not found (nothing changed)" } }
  $s = $s.Substring(0, $i) + $inc + $s.Substring($i)
  foreach ($f in $funcs) {
    $j = $s.IndexOf($f[0])
    $k = $s.IndexOf(") {`n", $j) + 4
    $label = $f[0].Replace("u32 ", "").Replace("_entry(", "").Replace("NetDll_", "")
    $s = $s.Substring(0, $k) + "  WaitTrace wait_trace__(`"$label`", " + $f[1] + ");`n" + $s.Substring($k)
  }
  $s = $s.Replace($incFrom, $incTo)
  if ($crlf) { $s = $s.Replace("`n", "`r`n") }
  Copy-Item $file "$file.before-waittrace" -Force
  [IO.File]::WriteAllText($file, $s, (New-Object Text.UTF8Encoding($false)))
  Write-Host "wait trace added to $rel" -ForegroundColor Green
}

$tp = "timeout_ptr ? int64_t(static_cast<uint64_t>(*timeout_ptr)) : 0"
Patch "src\kernel\xboxkrnl\xboxkrnl_threading.cpp" @(
  ,@("u32 KeDelayExecutionThread_entry(", "int64_t(static_cast<uint64_t>(*interval_ptr))")
  ,@("u32 KeWaitForSingleObject_entry(", $tp)
  ,@("u32 NtWaitForSingleObjectEx_entry(", $tp)
  ,@("u32 KeWaitForMultipleObjects_entry(", $tp)
  ,@("u32 NtWaitForMultipleObjectsEx_entry(", $tp)
  ,@("u32 NtSignalAndWaitForSingleObjectEx_entry(", $tp)
) "u32 KeDelayExecutionThread_entry(" "#include <memory>`n" "#include <map>`n#include <memory>`n#include <mutex>`n"
Patch "src\kernel\xam\xam_net.cpp" @(
  ,@("u32 NetDll_WSAWaitForMultipleEvents_entry(", "timeout == 0xFFFFFFFFu ? 0 : -int64_t(timeout) * 10000")
) "u32 NetDll_WSAWaitForMultipleEvents_entry(" "#include <rex/system/xthread.h>`n" "#include <rex/system/thread_state.h>`n#include <rex/system/xthread.h>`n"
Write-Host "Now run: .\tools\apply-patches.ps1" -ForegroundColor Green
