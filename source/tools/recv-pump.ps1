# Adds the receive pump to the SDK's xam_net.cpp (by text anchors), then build:
#   .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$file = Join-Path $Sdk "src\kernel\xam\xam_net.cpp"
$raw = [IO.File]::ReadAllText($file)
$crlf = $raw.Contains("`r`n")
$s = $raw.Replace("`r`n", "`n")
if ($s.Contains("[online] Receive pump.")) { Write-Host "The receive pump is already in xam_net.cpp" -ForegroundColor Yellow; exit 0 }
$pairs = @()
$pairs += ,@(@'
// Completes every pending overlapped receive that has data (or failed).
static void PollPendingRecvs() {
  std::vector<PendingRecv> list;
'@, @'
// [online] Receive pump. Overlapped receives used to be completed only when the
// game itself called WSAWaitForMultipleEvents / WSAGetOverlappedResult. FIFA 09's
// network thread does not do that every frame, so a packet that had already
// arrived waited up to ~100 ms before the game saw it, and the lock-step match
// stalled about 9 times a second even on a LAN. A host thread now completes
// every pending receive as soon as its socket has data (checked every ms).
static std::mutex g_poll_mutex;
static std::atomic<int> g_pump_completions{0};

static bool NativeReadable(uint64_t native) {
  fd_set fds;
  FD_ZERO(&fds);
#if REX_PLATFORM_WIN32
  FD_SET(static_cast<SOCKET>(native), &fds);
  timeval tv = {0, 0};
  return ::select(0, &fds, nullptr, nullptr, &tv) > 0;
#else
  FD_SET(static_cast<int>(native), &fds);
  timeval tv = {0, 0};
  return ::select(static_cast<int>(native) + 1, &fds, nullptr, nullptr, &tv) > 0;
#endif
}

static void PollPendingRecvsImpl(bool from_pump);
static void PollPendingRecvs() { PollPendingRecvsImpl(false); }

// Completes every pending overlapped receive that has data (or failed).
static void PollPendingRecvsImpl(bool from_pump) {
  std::lock_guard<std::mutex> poll_lock(g_poll_mutex);
  std::vector<PendingRecv> list;
'@)
$pairs += ,@(@'
  for (auto& r : list) {
    int e = 0;
    int got = RecvIntoGuest(r, &e);
    if (got < 0 && WouldBlock(e)) continue;
'@, @'
  for (auto& r : list) {
    if (from_pump && !NativeReadable(r.native)) continue;
    int e = 0;
    int got = RecvIntoGuest(r, &e);
    if (got < 0 && WouldBlock(e)) continue;
'@)
$pairs += ,@(@'
      CompleteOverlapped(r.overlapped_ptr, 0, uint32_t(got));
    } else {
'@, @'
      CompleteOverlapped(r.overlapped_ptr, 0, uint32_t(got));
      if (from_pump) ++g_pump_completions;
    } else {
'@)
$pairs += ,@(@'
u32 NetDll_WSARecvFrom_entry(
'@, @'
static void StartRecvPump() {
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  REXKRNL_INFO("[online] receive pump started");
  std::thread([] {
    auto last = std::chrono::steady_clock::now();
    for (;;) {
      if (HavePendingRecvs()) PollPendingRecvsImpl(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      auto now = std::chrono::steady_clock::now();
      if (now - last >= std::chrono::seconds(10)) {
        last = now;
        int n = g_pump_completions.exchange(0);
        if (n) REXKRNL_INFO("[online] receive pump: {} packets handed to the game early in 10 s", n);
      }
    }
  }).detach();
}

u32 NetDll_WSARecvFrom_entry(
'@)
$pairs += ,@(@'
    g_pending_recvs.push_back(r);
  }
  XThread::SetLastError(kWsaIoPending);
'@, @'
    g_pending_recvs.push_back(r);
  }
  StartRecvPump();
  XThread::SetLastError(kWsaIoPending);
'@)
$n = 0
foreach ($p in $pairs) {
  $a = $p[0].Replace("`r`n", "`n"); $n++
  $i = $s.IndexOf($a)
  if ($i -lt 0) { throw "anchor $n not found in xam_net.cpp (nothing was changed)" }
  if ($s.IndexOf($a, $i + 1) -ge 0) { throw "anchor $n found twice (nothing was changed)" }
}
foreach ($p in $pairs) { $s = $s.Replace($p[0].Replace("`r`n", "`n"), $p[1].Replace("`r`n", "`n")) }
if ($crlf) { $s = $s.Replace("`n", "`r`n") }
Copy-Item $file "$file.before-pump" -Force
[IO.File]::WriteAllText($file, $s, (New-Object Text.UTF8Encoding($false)))
Write-Host "Receive pump added (backup: xam_net.cpp.before-pump). Now run: .\tools\apply-patches.ps1" -ForegroundColor Green
