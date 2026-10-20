#pragma once
// [online] Debug probes placed inside FIFA 09's EA session code with
// [[midasm_hook]] entries in config/online_hooks.toml. They only log
// ("[sesdbg] ...") so we can see why the host never creates its Xbox Live
// session. Included once, from fifa09_app.h.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xsocket.h>

// Diagnostics (online timing probes, sync / packet traces) are off in normal
// builds. Set the environment variable NOBK_DIAG=1 before starting the game
// to turn them on.
// True while an online match link (CommUDP) is open: set when CommUDP registers
// its socket callback for a match, cleared when the link closes.
inline std::atomic<bool> g_online_match{false};

namespace diag {
inline bool On() {
  static const bool on = [] { const char* e = std::getenv("NOBK_DIAG"); return e && *e && *e != '0'; }();
  return on;
}
}  // namespace diag

// Noisy probe logs ([tp], [sesdbg], [osdk] ...) only with NOBK_DIAG=1 (launcher:
// Files > "Detailed log"). The arguments are only evaluated when it is on.
#define NOBK_DIAG_INFO(...) do { if (diag::On()) REXKRNL_INFO(__VA_ARGS__); } while (0)
#define NOBK_DIAG_WARN(...) do { if (diag::On()) REXKRNL_WARN(__VA_ARGS__); } while (0)

// Time of the last online-session activity (steady clock ms): session state changes, game
// session create / join / update, OSDK lobby sizing. The Quit to Desktop guess (uiquit) is
// cancelled by any of it: Play a Friend, Be A Pro rooms and lobby search/join all start like
// the Quit item (StartLogin from the main menu) but then touch a session; the Quit item does not.
inline std::atomic<int64_t> g_sess_change_ms{0};
inline void NetActivity() {
  g_sess_change_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Online receive-path trace. Hooks in the online session update (0x82853328)
// and its packet receive loop (0x8284bbc0) push timestamped events into a
// ring; NetProbe dumps the events of the last stall window, so we can see
// whether the session update / packet reads pause together with the sim.
namespace netdbg {
struct Ev { int64_t us; char kind; uint32_t a; uint32_t tid; };
inline std::mutex mu;
inline Ev ring[4096];
inline uint64_t head = 0;
inline int64_t NowUs() {
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}
inline uint32_t Tid() {
  return uint32_t(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFF);
}
// per-5 s counters
inline int upd = 0, upd_loop = 0, recv_calls = 0, recv_hits = 0, pkts = 0, dgrams = 0, cb_recv = 0, cb_idle = 0;
inline int64_t last_sum = 0, last_upd = -1, max_upd_gap = 0;
inline void Push(char kind, uint32_t a) {
  if (!diag::On()) return;
  std::lock_guard<std::mutex> lock(mu);
  int64_t now = NowUs();
  ring[head++ % 4096] = {now, kind, a, Tid()};
  switch (kind) {
    case 'U':
      ++upd;
      if (last_upd >= 0) max_upd_gap = std::max(max_upd_gap, now - last_upd);
      last_upd = now;
      break;
    case 'L': if (int32_t(a) > 1) ++upd_loop; break;
    case 'r': ++recv_calls; break;
    case 'R': ++recv_hits; pkts += int(a); break;
    case 'D': ++dgrams; break;
    case 'C': ++cb_recv; break;
    case 'I': ++cb_idle; break;
  }
  if (last_sum == 0) last_sum = now;
  if (now - last_sum >= 5000000) {
    NOBK_DIAG_INFO("[netdbg] per s: session update {:.1f} (max gap {} ms), update with r29>1 {:.1f}, "
                 "receive loop {:.1f}, with packets {:.1f}, packets {:.1f} | CommUDP datagrams {:.1f}, "
                 "callback on data {:.1f}, on idle timer {:.1f}",
                 upd / 5.0, max_upd_gap / 1000, upd_loop / 5.0, recv_calls / 5.0, recv_hits / 5.0, pkts / 5.0,
                 dgrams / 5.0, cb_recv / 5.0, cb_idle / 5.0);
    upd = upd_loop = recv_calls = recv_hits = pkts = dgrams = cb_recv = cb_idle = 0;
    max_upd_gap = 0;
    last_sum = now;
  }
}
// Called by NetProbe on a stall: prints the events of the last `ms` (+20) milliseconds.
inline void Dump(double ms) {
  static int dumps = 0;
  std::lock_guard<std::mutex> lock(mu);
  if (dumps++ >= 60) return;
  int64_t now = NowUs(), from = now - int64_t((ms + 20) * 1000);
  std::string line;
  uint64_t start = head > 4096 ? head - 4096 : 0;
  for (uint64_t i = start; i < head; ++i) {
    const Ev& e = ring[i % 4096];
    if (e.us < from || e.kind == 'U' || e.kind == 'r' || e.kind == 'L') continue;  // too frequent
    char b[48];
    std::snprintf(b, sizeof(b), " %c%d@-%lld/%04X", e.kind, int32_t(e.a), (long long)((now - e.us) / 1000), e.tid);
    line += b;
  }
  NOBK_DIAG_INFO("[netdbg] stall window:{}", line.empty() ? std::string(" (no events)") : line);
}
}  // namespace netdbg

namespace sesdbg {

inline const uint8_t* Ptr(uint32_t addr) {
  if (addr < 0x10000) return nullptr;
  return REX_KERNEL_MEMORY()->TranslateVirtual<const uint8_t*>(addr);
}

inline uint32_t U32(uint32_t addr) {
  const uint8_t* p = Ptr(addr);
  if (!p) return 0xDEADBEEF;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

inline uint16_t U16(uint32_t addr) {
  const uint8_t* p = Ptr(addr);
  return p ? uint16_t((uint16_t(p[0]) << 8) | p[1]) : 0xEEEE;
}

inline uint64_t U64(uint32_t addr) { return (uint64_t(U32(addr)) << 32) | U32(addr + 4); }

inline uint8_t U8(uint32_t addr) {
  const uint8_t* p = Ptr(addr);
  return p ? p[0] : 0xEE;
}

inline std::string Str(uint32_t addr, size_t max = 80) {
  const uint8_t* p = Ptr(addr);
  if (!p) return "<null>";
  std::string s;
  for (size_t i = 0; i < max && p[i]; ++i) {
    char c = char(p[i]);
    if (c >= 32 && c < 127) s += c;
    else { char b[8]; std::snprintf(b, sizeof(b), "\\x%02x", p[i]); s += b; }
  }
  return s;
}

inline std::string Cc(uint32_t v) {
  std::string s;
  for (int i = 3; i >= 0; --i) {
    char c = char((v >> (i * 8)) & 0xFF);
    s += (c >= 32 && c < 127) ? c : '.';
  }
  return s;
}

// Limit noisy probes.
inline bool Budget(int& n, int max = 60) { return n++ < max; }


// Guest call chain from a frame pointer (saved LR sits at back-chain - 8).
// Stops at the outermost frame: its back chain is the stack top, and the
// page above it is not mapped.
inline std::string Chain(uint32_t sp, int depth = 10) {
  std::string chain;
  for (int i = 0; i < depth; ++i) {
    uint32_t next = U32(sp);
    if (next == 0 || next == 0xDEADBEEF || next <= sp || next - sp > 0x10000) break;
    char b[12];
    std::snprintf(b, sizeof(b), " %08X", U32(next - 8));
    chain += b;
    if ((next & 0xFFF) == 0) break;
    sp = next;
  }
  return chain;
}
}  // namespace sesdbg

void SesDbg_GameUpdate(PPCRegister& r3, PPCRegister& r4) {
  NetActivity();
  static int n = 0;
  if (!sesdbg::Budget(n)) return;
  uint32_t m = r3.u32, g = r4.u32;
  REXKRNL_INFO(
      "[sesdbg] GameUpdate mgr={:08X} game={:08X} state={} players={} flag6c9c={} host='{}' "
      "self='{}' sess='{}' nonce='{}' gameflag4={}",
      m, g, sesdbg::U32(m + 0x6c78), sesdbg::U32(m + 0x220), sesdbg::U8(m + 0x6c9c),
      sesdbg::Str(g + 0x28), sesdbg::Str(m + 0x6c68), sesdbg::Str(g + 0x4c),
      sesdbg::Str(g + 0x150, 40), sesdbg::U8(g + 4));
}

// Probe the receive dispatcher at its entry, before the 'osdk' type check.
// If the peer's REQ reaches CommUDP but this never runs, the loss is upstream
// of the OSDK dispatcher (or the call path/address differs from this build).
void SesDbg_OsdkEntry(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5) {
  static std::atomic<uint32_t> calls{0};
  uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n < 20 || (n < 8192 && n % 128 == 0)) {
    NOBK_DIAG_INFO("[osdk] entry #{} obj={:08X} r4={:08X} r5={:08X}",
                 n, r3.u32, r4.u32, r5.u32);
  }
}

// OSDK peer messages ("osdk" channel over the game link, e.g. the Online Team
// Play lobby's "NAME|REQ|...", "NAME|USET|...", the 1v1 "NAME|HSK" / "NAME|HSR").
// --- Team play lobby settings (for the website's Be A Pro stats) -------------
// OSDK peers keep every lobby member's settings in short 'ctrl' messages:
//   "SENDER|USET|USER|S=-1;P=3;Y=1;..."  one user's changed keys
//   "HOST|BIGG|\USER1|k=v;...;\USER2|k=v;...\T1=.."  the host's full snapshot
// Seen so far: S = side (-1 / 0 / 1, the lobby's left / middle / right column),
// L = column slider (-10..10), P = position picked (-1 none), Y = ready,
// T0/T1 = the two team ids, J0/J1 = kits. The meaning of the rest (T, C, A, N, O)
// is still open, so everything is kept raw and sent to the server at kick-off.
namespace tplobby {
struct Lobby {
  std::mutex mu;
  std::map<std::string, std::map<std::string, std::string>> users;  // "*" = game-level keys
};
inline Lobby& Get() { static Lobby l; return l; }

inline void MergeKv(std::map<std::string, std::string>& into, const std::string& kvs) {
  size_t i = 0;
  while (i < kvs.size()) {
    size_t e = kvs.find(';', i);
    if (e == std::string::npos) e = kvs.size();
    std::string kv = kvs.substr(i, e - i);
    size_t eq = kv.find('=');
    if (eq != std::string::npos && eq > 0 && eq < 8 && kv.size() - eq - 1 < 64) {
      std::string k = kv.substr(0, eq);
      if (k != "ack") into[k] = kv.substr(eq + 1);
    }
    i = e + 1;
  }
}

inline bool NameOk(const std::string& n) {
  if (n.empty() || n.size() > 24) return false;
  for (unsigned char c : n)
    if (c < 32 || c >= 127 || c == '"' || c == '\\' || c == '|') return false;
  return true;
}

inline void Feed(const std::string& text) {
  size_t a = text.find('|');
  if (a == std::string::npos) return;
  size_t b = text.find('|', a + 1);
  if (b == std::string::npos) return;
  std::string kind = text.substr(a + 1, b - a - 1);
  std::string rest = text.substr(b + 1);
  Lobby& l = Get();
  std::lock_guard<std::mutex> lock(l.mu);
  if (kind == "USET") {
    size_t c = rest.find('|');
    if (c == std::string::npos) return;
    std::string user = rest.substr(0, c);
    if (NameOk(user)) MergeKv(l.users[user], rest.substr(c + 1));
  } else if (kind == "BIGG") {
    // a fresh full snapshot: start over, so players who left are gone
    std::map<std::string, std::map<std::string, std::string>> snap;
    size_t i = 0;
    while (i < rest.size()) {
      if (rest[i] == '\\') { ++i; continue; }
      size_t e = rest.find('\\', i);
      if (e == std::string::npos) e = rest.size();
      std::string part = rest.substr(i, e - i);
      size_t bar = part.find('|');
      if (bar != std::string::npos) {
        std::string user = part.substr(0, bar);
        if (NameOk(user)) MergeKv(snap[user], part.substr(bar + 1));
      } else {
        MergeKv(snap["*"], part);
      }
      i = e + 1;
    }
    if (snap.size() >= 2) {
      // keep keys a later USET added for users that are still there
      for (auto& [u, kv] : snap)
        if (l.users.count(u))
          for (auto& [k, v] : l.users[u])
            if (!kv.count(k)) kv[k] = v;
      l.users.swap(snap);
    }
  }
}

inline std::string Esc(const std::string& v) {
  std::string o;
  for (unsigned char c : v) {
    if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
    else if (c >= 32 && c < 127) o += char(c);
  }
  return o;
}

// {"NAME":{"S":"-1","P":"3"},...} or "" when there is no team play lobby
inline std::string Json() {
  Lobby& l = Get();
  std::lock_guard<std::mutex> lock(l.mu);
  int people = 0;
  for (auto& [u, kv] : l.users) people += u != "*" ? 1 : 0;
  if (people < 2) return "";
  std::string o = "{";
  bool first = true;
  for (auto& [u, kv] : l.users) {
    o += (first ? "\"" : ",\"") + Esc(u) + "\":{";
    first = false;
    bool f2 = true;
    for (auto& [k, v] : kv) {
      o += (f2 ? "\"" : ",\"") + Esc(k) + "\":\"" + Esc(v) + "\"";
      f2 = false;
    }
    o += "}";
  }
  return o + "}";
}
}  // namespace tplobby

// Hooked in the receive dispatcher 0x82c84fc8 after its type check: r7 = sub
// type ('ctrl'), r8 = text, r9 = length, r3 - 0x3c / - 0x38 = listener array /
// count. Each listener gets the message through vtable + 0x24; their addresses
// tell which game component would have to answer.
void SesDbg_OsdkRecv(PPCRegister& r3, PPCRegister& r7, PPCRegister& r8, PPCRegister& r9) {
  if (sesdbg::Cc(r7.u32) == "ctrl" && r9.u32 > 0 && r9.u32 < 4096)
    tplobby::Feed(sesdbg::Str(r8.u32, r9.u32));
  static int n = 0;
  if (!sesdbg::Budget(n, 400)) return;
  uint32_t self = r3.u32, len = r9.u32;
  uint32_t cnt = sesdbg::U32(self - 0x38), arr = sesdbg::U32(self - 0x3c);
  std::string fns;
  for (uint32_t i = 0; i < cnt && i < 8; ++i) {
    uint32_t obj = sesdbg::U32(arr + 4 * i);
    uint32_t fn = (obj && obj != 0xDEADBEEF) ? sesdbg::U32(sesdbg::U32(obj) + 0x24) : 0;
    char b[32];
    std::snprintf(b, sizeof(b), " %08X:%08X", obj, fn);
    fns += b;
  }
  NOBK_DIAG_INFO("[osdk] recv '{}' len={} text='{}' listeners={}{}", sesdbg::Cc(r7.u32), len,
               sesdbg::Str(r8.u32, std::min<uint32_t>(len, 120)), int32_t(cnt), fns);
}

// --- Team play: why the host never gets an 'osdk' stream for a joiner -------
// OSDK chain (from the game's own debug strings): ConnApi callback 0x82d0e238 ->
// PeerConnectionManagerConcrete::HandleConnApiGameCallback 0x82d035e8. On
// "connected" (new status 3) it looks the client name up in the current game
// session's user list (GetGameUserFromClientList 0x82c8da88). Only if a user is
// found it creates a PeerConnection and notifies GameSetupConcrete::
// PeerConnectGameSuccess 0x82cff0b0 -> AddStreamedPeerConnection 0x82ce5418,
// which creates the 'osdk' NetGameLink stream. No stream = REQ/USET/ASET sit in
// the link buffer forever (what the [commudp] stream-worker probe showed).
// All probes below are read-only and bounded ("[tp]" lines).
// Reference counts of watched OSDK objects (GameUser of a peer): the 3rd-player crash
// looks like a GameUser freed while still in use ("OCALEX" found on a free list).
// OSDK refcount = top 10 bits of word +4; AddRef 0x82CA5B28, Release 0x82C80C28.
namespace tpref {
inline std::mutex mu;
inline std::unordered_set<uint32_t> watched;
inline void Watch(uint32_t o) { std::lock_guard<std::mutex> l(mu); watched.insert(o); }
inline bool Is(uint32_t o) { std::lock_guard<std::mutex> l(mu); return watched.count(o) != 0; }
}
namespace tpdbg {
inline bool Budget(std::atomic<int>& n, int max = 60) { return n.fetch_add(1) < max; }
// Listener list of PeerConnectionManager (+8 array, +12 count): "obj:slot0" each.
inline std::string Listeners(uint32_t m) {
  uint32_t arr = sesdbg::U32(m + 8), cnt = sesdbg::U32(m + 12);
  std::string s = std::to_string(int32_t(cnt));
  for (uint32_t i = 0; i < cnt && i < 8; ++i) {
    uint32_t o = sesdbg::U32(arr + 4 * i);
    uint32_t f = (o && o != 0xDEADBEEF) ? sesdbg::U32(sesdbg::U32(o)) : 0;
    char b[24];
    std::snprintf(b, sizeof(b), " %08X:%08X", o, f);
    s += b;
  }
  return s;
}
}

// HandleConnApiGameCallback after its debug print: r29 = event
// {+0 client index, +4 kind, +8 old status, +12 new status}, r31 = manager.
void Tp_ConnApiGameCb(PPCRegister& r29, PPCRegister& r31) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 120)) return;
  uint32_t e = r29.u32, m = r31.u32;
  NOBK_DIAG_INFO("[tp] HandleConnApiGameCallback mgr={:08X} client={} old={} new={} connapi@5c={:08X} listeners={}",
               m, int32_t(sesdbg::U32(e)), sesdbg::U32(e + 8), sesdbg::U32(e + 12), sesdbg::U32(m + 0x5c),
               tpdbg::Listeners(m));
}

// ... about to notify the listeners of a new PeerConnection (r31 = manager, r29 = peer).
void Tp_NotifyPeerListeners(PPCRegister& r31, PPCRegister& r29) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   new PeerConnection={:08X} -> notify listeners {}", r29.u32, tpdbg::Listeners(r31.u32));
}

// ... RegisterConnection call (r29 = new PeerConnection, r30 = game user).
void Tp_RegisterPeer(PPCRegister& r29, PPCRegister& r30) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   RegisterConnection peer={:08X} user={:08X}", r29.u32, r30.u32);
}

// ... result of GetPeerConnection(client name): non-zero = already known, the
// callback then does nothing on "connected".
void Tp_ConnApiGameCbExisting(PPCRegister& r3, PPCRegister& r29) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 60)) return;
  NOBK_DIAG_INFO("[tp]   existing PeerConnection={:08X} (client {} new={})", r3.u32,
               int32_t(sesdbg::U32(r29.u32)), sesdbg::U32(r29.u32 + 12));
}

// ... ConnApiStatus('gsmd') result: 1 = streamed peer object, else game-user lookup.
void Tp_ConnApiGameCbGsmd(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   ConnApi 'gsmd' = {}", int32_t(r3.u32));
}

// GetGameUserFromClientList: which user list is used (r3 = bool "in game session").
void Tp_GameUserSource(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   GetGameUser: source = {}", (r3.u32 & 0xFF) ? "game session users" : "userset (+0x60)");
}

// ... each user name compared with the ConnApi client name (r3 = user name, r24 = client name).
void Tp_GameUserCompare(PPCRegister& r3, PPCRegister& r24, PPCRegister& r30, PPCRegister& r28) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 120)) return;
  NOBK_DIAG_INFO("[tp]   GetGameUser: user[{}/{}]='{}' vs client='{}'", r30.u32, r28.u32,
               sesdbg::Str(r3.u32, 32), sesdbg::Str(r24.u32, 32));
}

// ... result (r26 = GameUser or 0, r25 = client index, r24 = client name).
void Tp_GameUserResult(PPCRegister& r26, PPCRegister& r25, PPCRegister& r24) {
  if (r26.u32) tpref::Watch(r26.u32);  // always (Tp_RefDestroy relies on it), the log is limited
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 60)) return;
  NOBK_DIAG_INFO("[tp]   GetGameUser(client {} '{}') -> {:08X}{} refs={}", int32_t(r25.u32), sesdbg::Str(r24.u32, 32),
               r26.u32, r26.u32 ? "" : "  <-- NOT FOUND: no PeerConnection, no 'osdk' stream",
               sesdbg::U32(r26.u32 + 4) >> 22);
  if (r26.u32) tpref::Watch(r26.u32);
}

// GameSetupConcrete::PeerConnectGameSuccess (r3 = listener interface, r4 = PeerConnection).
void Tp_PeerConnectGameSuccess(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::PeerConnectGameSuccess setup={:08X} peer={:08X}", r3.u32, r4.u32);
}

// GameSetupConcrete::AddStreamedPeerConnection (r3 = GameSetup, r4 = PeerConnection).
void Tp_AddStreamedPeer(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::AddStreamedPeerConnection setup={:08X} peer={:08X}", r3.u32, r4.u32);
}

// GameSetupConcrete::UpdateStreamedPeerConnectionList / PeerConnectAllPeersConnected.
void Tp_UpdateStreamedList(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::UpdateStreamedPeerConnectionList setup={:08X}", r3.u32);
}
// UpdateStreamedPeerConnectionList internals (3rd player crash, 18 Oct 23:17):
// loop 1 = streamed connections (this+116 list, r25 = count), loop 2 = manager's peers.
inline uint32_t TpVfn(uint32_t o, uint32_t off) {
  uint32_t vt = (o && o != 0xDEADBEEF) ? sesdbg::U32(o) : 0;
  return (vt && vt != 0xDEADBEEF) ? sesdbg::U32(vt + off) : 0;
}
void Tp_UpdStreamedItem(PPCRegister& r3, PPCRegister& r31, PPCRegister& r25) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 80)) return;
  NOBK_DIAG_INFO("[tp]   streamed[{}/{}]={:08X} vt={:08X} fn20={:08X}", r31.u32, r25.u32, r3.u32,
               sesdbg::U32(r3.u32), TpVfn(r3.u32, 20));
}
void Tp_UpdPeerItem(PPCRegister& r29, PPCRegister& r27, PPCRegister& r24) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 80)) return;
  NOBK_DIAG_INFO("[tp]   manager peer[{}/{}]={:08X} vt={:08X}", r27.u32, r24.u32, r29.u32, sesdbg::U32(r29.u32));
}
void Tp_RemoveStreamedPeer(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::RemoveStreamedPeerConnection setup={:08X} peer={:08X} from {:08X}", r3.u32,
               r4.u32, r12.u32);
}

// PeerConnectAllPeersConnected internals (3rd player: "call to 0x00000004" right after
// UpdateStreamedPeerConnectionList, 18 Oct 23:39).
void Tp_AllPeersFrom(PPCRegister& r12, PPCRegister& r1) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   AllPeersConnected from {:08X} stack:{}", r12.u32, sesdbg::Chain(r1.u32, 10));
}
void Tp_AllPeersListener(PPCRegister& r3, PPCRegister& r30) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 80)) return;
  NOBK_DIAG_INFO("[tp]   AllPeers: setup listener {:08X} vt={:08X} slot8={:08X} (left {})", r3.u32,
               sesdbg::U32(r3.u32), TpVfn(r3.u32, 8), r30.u32);
}
void Tp_AllPeersState(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   AllPeers: manager vfn24 = {}", r3.u32 & 0xFF);
}
void Tp_AllPeersCount(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   AllPeers: session vfn8 = {}", int32_t(r3.u32));
}
void Tp_AllPeersListener2(PPCRegister& r3, PPCRegister& r30) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 80)) return;
  NOBK_DIAG_INFO("[tp]   AllPeers: notify {:08X} vt={:08X} slot28={:08X} (left {})", r3.u32,
               sesdbg::U32(r3.u32), TpVfn(r3.u32, 28), r30.u32);
}
void Tp_AllPeersTail(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp]   AllPeers: -> sub_82C85368({:08X})", r3.u32);
}

void Tp_AllPeersConnected(PPCRegister& r3) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::PeerConnectAllPeersConnected obj={:08X}", r3.u32);
}

// GameSetupConcrete::GameSessionStarted / GameSessionJoined (r4 = session).
void Tp_SessionStarted(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::GameSessionStarted obj={:08X} session={:08X}", r3.u32, r4.u32);
}
// GameSetupConcrete::Shutdown / Startup, hooked at the second prologue instruction so
// r12 still holds the return address (= the caller).
void Tp_SetupShutdown(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 20)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::Shutdown obj={:08X} from {:08X} stack:{}", r3.u32, r12.u32,
               sesdbg::Chain(r1.u32, 12));
}
void Tp_SetupStartup(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 20)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::Startup obj={:08X} from {:08X} stack:{}", r3.u32, r12.u32,
               sesdbg::Chain(r1.u32, 12));
}
// GameSessionStarted: who gets told (GameSetup's own listeners, slot +0x10), and the caller.
void Tp_SessionStartedFrom(PPCRegister& r3, PPCRegister& r12, PPCRegister& r1) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 20)) return;
  uint32_t self = r3.u32, cnt = sesdbg::U32(self - 48), arr = sesdbg::U32(self - 52);
  std::string ls;
  for (uint32_t i = 0; i < cnt && i < 8; ++i) {
    uint32_t o = sesdbg::U32(arr + 4 * i);
    uint32_t f = (o && o != 0xDEADBEEF) ? sesdbg::U32(sesdbg::U32(o) + 16) : 0;
    char b[24];
    std::snprintf(b, sizeof(b), " %08X:%08X", o, f);
    ls += b;
  }
  NOBK_DIAG_INFO("[tp] GameSessionStarted from {:08X} flag44={} listeners={}{} stack:{}", r12.u32,
               sesdbg::U8(self + 44), int32_t(cnt), ls, sesdbg::Chain(r1.u32, 12));
}

void Tp_SessionJoined(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 40)) return;
  NOBK_DIAG_INFO("[tp] GameSetup::GameSessionJoined obj={:08X} session={:08X}", r3.u32, r4.u32);
}

// PeerConnectionManager game-session state listener (r4 = old, r5 = new); going
// from < 5 to >= 5 makes the manager open its peer connections.
void Tp_SessionState(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5) {
  NetActivity();
  static std::atomic<int> n{0};
  if (!tpdbg::Budget(n, 60)) return;
  NOBK_DIAG_INFO("[tp] PeerConnectionManager session state {} -> {} (obj={:08X})", int32_t(r4.u32),
               int32_t(r5.u32), r3.u32);
}

// See online_hooks.toml "Lobby leave crash": skip table slots that are not object pointers.
void Tp_PeerSlotGuard(PPCRegister& r7) {
  const uint32_t v = r7.u32;
  if (v == 0 || (v >= 0x40000000u && (v & 3) == 0)) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 20) REXKRNL_WARN("[tp] peer slot guard: bogus slot {:08X} skipped (lobby leave)", v);
  r7.u64 = 0;
}

void SesDbg_GameStart(PPCRegister& r3, PPCRegister& r4) {
  NetActivity();
  uint32_t m = r3.u32, g = r4.u32;
  REXKRNL_INFO(
      "[sesdbg] GameStart mgr={:08X} host='{}' self='{}' sess='{}' sysflags@164={:08X} "
      "players@20c={} sm={:08X}",
      m, sesdbg::Str(g + 0x28), sesdbg::Str(m + 0x6c68), sesdbg::Str(g + 0x4c),
      sesdbg::U32(g + 0x164), sesdbg::U32(g + 0x20c), sesdbg::U32(m + 4));
}

void SesDbg_IdleResult(PPCRegister& r3) {
  NOBK_DIAG_INFO("[sesdbg] GameStart: session manager 'idle' status = {} ({})", int32_t(r3.u32),
               r3.u32 ? "BUSY -> lock, no session" : "idle");
}

void SesDbg_HostDecision(PPCRegister& r11) {
  NOBK_DIAG_INFO("[sesdbg] GameStart: role = {} ({})", r11.u32,
               r11.u32 == 1 ? "HOST -> create session" : "JOINER -> join session");
}

void SesDbg_HostSession(PPCRegister& r3, PPCRegister& r5, PPCRegister& r7) {
  NetActivity();
  uint32_t sm = r3.u32;
  NOBK_DIAG_INFO("[sesdbg] HostSession sm={:08X} busy@184={} players={} flags={:X} xsm@4c={:08X}",
               sm, sesdbg::U32(sm + 0x184), r5.u32, r7.u32, sesdbg::U32(sm + 0x4c));
}

void SesDbg_JoinSession(PPCRegister& r3, PPCRegister& r5, PPCRegister& r7) {
  NetActivity();
  uint32_t sm = r3.u32;
  NOBK_DIAG_INFO("[sesdbg] JoinSession sm={:08X} busy@184={} players={} flags={:X}", sm,
               sesdbg::U32(sm + 0x184), r5.u32, r7.u32);
}

void SesDbg_Control(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  // [online] The game gives the match link a 10 s timeout ('time'). While a
  // PC loads the match it stops refreshing the link for longer than that and
  // the joiner ends the session at kick-off, so allow 60 s.
  if (r4.u32 == 0x74696D65 /* 'time' */ && r5.u32 < 60000) r5.u64 = 60000;
  static int n = 0;
  if (!sesdbg::Budget(n, 150)) return;
  NOBK_DIAG_INFO("[sesdbg] Control sm={:08X} '{}' arg={:X} ptr={:08X}", r3.u32,
               sesdbg::Cc(r4.u32), r5.u32, r6.u32);
}

void SesDbg_XControl(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  static int n = 0;
  if (!sesdbg::Budget(n, 150)) return;
  uint32_t x = r3.u32;
  NOBK_DIAG_INFO("[sesdbg] XControl xsm={:08X} '{}' arg={:X} ptr={:08X} handle@64={:08X} state@70={}",
               x, sesdbg::Cc(r4.u32), r5.u32, r6.u32, sesdbg::U32(x + 0x64),
               sesdbg::U32(x + 0x70));
}

void SesDbg_CreateEntry(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5) {
  NetActivity();
  uint32_t x = r3.u32;
  REXKRNL_INFO(
      "[sesdbg] CreateSession xsm={:08X} arg1={:X} ranked={:X} handle@64={:08X} id@78={:016X} "
      "id@88={:016X} flags@a4={:X}",
      x, r4.u32, r5.u32, sesdbg::U32(x + 0x64), sesdbg::U64(x + 0x78), sesdbg::U64(x + 0x88),
      sesdbg::U32(x + 0xa4));
}

void SesDbg_CreateUser(PPCRegister& r3) {
  NOBK_DIAG_INFO("[sesdbg] CreateSession: user index = {} {}", int32_t(r3.u32),
               int32_t(r3.u32) < 0 ? "(NOT FOUND -> fail)" : "");
}

void SesDbg_CreateResult(PPCRegister& r3) {
  NOBK_DIAG_INFO("[sesdbg] CreateSession: XSessionCreate -> {:08X}", r3.u32);
}

void SesDbg_SessionEvent(PPCRegister& r3, PPCRegister& r4, PPCRegister& r12, PPCRegister& r1) {
  static int n = 0;
  if (!sesdbg::Budget(n, 100)) return;
  uint32_t e = r4.u32;
  uint32_t status = sesdbg::U32(e + 0xc);
  NOBK_DIAG_INFO("[sesdbg] SessionEvent kind={} status={} caller={:08X}", sesdbg::U32(e + 4), status,
               r12.u32);
  if (status >= 5) {
    std::string chain = sesdbg::Chain(r1.u32, 12);
    NOBK_DIAG_INFO("[sesdbg] SessionEvent status={} stack:{}", status, chain);
  }
}

namespace sesdbg {
inline std::string Addr(uint32_t sa) {
  // DirtySock sockaddr: family(2) port(2) addr(4)
  char b[64];
  std::snprintf(b, sizeof(b), "fam=%u %u.%u.%u.%u:%u", (U8(sa) << 8) | U8(sa + 1), U8(sa + 4),
                U8(sa + 5), U8(sa + 6), U8(sa + 7), (U8(sa + 2) << 8) | U8(sa + 3));
  return b;
}
}  // namespace sesdbg

// See online_hooks.toml "Team play routing". NOBK_TP_DIRECT=0 turns it off.
void Tp_CommUdpSendToPeer(PPCRegister& r31, PPCRegister& r7) {
  static const bool off = [] {
    const char* e = std::getenv("NOBK_TP_DIRECT");
    return e && e[0] == '0';
  }();
  if (off) return;
  const uint32_t ref = r31.u32, to = r7.u32, peer = ref + 0x8c;
  {
    static std::atomic<int> d{0};
    if (ref >= 0x10000 && to >= 0x10000 && d.fetch_add(1) < 16)
      REXKRNL_INFO("[tp] CommUDP send ref {:08X} to@{:08X} {} | peer@{:08X} {}", ref, to, sesdbg::Addr(to), peer,
                   sesdbg::Addr(peer));
  }
  if (ref < 0x10000 || to < 0x10000 || to == peer) return;
  // DirtySock sockaddr: family(2) port(2) addr(4)
  if (sesdbg::U16(peer) != 2 || sesdbg::U8(peer + 4) != 10) return;   // not bound to a player yet
  if (sesdbg::U16(to) != 2 || sesdbg::U8(to + 4) == 10) return;       // already a player address
  if (sesdbg::U16(peer + 2) != sesdbg::U16(to + 2)) return;           // other port: leave it
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 8)
    REXKRNL_INFO("[tp] CommUDP ref {:08X}: send to bound peer {} instead of {}", ref, sesdbg::Addr(peer),
                 sesdbg::Addr(to));
  r7.u64 = peer;
}

// See online_hooks.toml "Team play routing" (ProtoTunnel flush). NOBK_TP_DIRECT=0 turns it off.
void Tp_TunnelSendToPeer(PPCRegister& r31, PPCRegister& r7) {
  static const bool off = [] {
    const char* e = std::getenv("NOBK_TP_DIRECT");
    return e && e[0] == '0';
  }();
  const uint32_t t = r31.u32, to = r7.u32;
  if (t < 0x10000 || to < 0x10000) return;
  const uint32_t remote = sesdbg::U32(t + 4), vaddr = sesdbg::U32(t + 36);
  const uint32_t dst = sesdbg::U32(to + 4);
  const bool fix = !off && (vaddr >> 24) == 10 && (dst >> 24) != 10 && dst == remote;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 12)
    REXKRNL_INFO("[tp] tunnel {:08X}: remote {}.{}.{}.{}:{} virtual {}.{}.{}.{} to {} -> {}", t, remote >> 24,
                 (remote >> 16) & 255, (remote >> 8) & 255, remote & 255, sesdbg::U16(t + 8), vaddr >> 24,
                 (vaddr >> 16) & 255, (vaddr >> 8) & 255, vaddr & 255, sesdbg::Addr(to),
                 fix ? "send to the virtual address" : "keep");
  if (!fix) return;
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(to + 4);
  if (!p) return;
  p[0] = uint8_t(vaddr >> 24); p[1] = uint8_t(vaddr >> 16); p[2] = uint8_t(vaddr >> 8); p[3] = uint8_t(vaddr);
}

// Diagnostics: every guest sendto (thunk sub_82FB5E40, r7 = destination): where team play
// packets are really addressed. First 24 calls per run.
void Tp_SendtoTrace(PPCRegister& r3, PPCRegister& r5, PPCRegister& r7, PPCRegister& r1) {
  static std::atomic<int> n{0};
  if (r7.u32 < 0x10000 || n.fetch_add(1) >= 24) return;
  const uint32_t back = sesdbg::U32(r1.u32);           // caller's frame
  REXKRNL_INFO("[tp] sendto sock {:08X} len {} to {} | caller LR {:08X} {:08X}", r3.u32, r5.u32,
               sesdbg::Addr(r7.u32), sesdbg::U32(back - 8), sesdbg::U32(sesdbg::U32(back) - 8));
}

// See online_hooks.toml "Team play routing, the real spot".
void Tp_KeepVirtualAddr(PPCRegister& r27, PPCRegister& r11) {
  static const bool off = [] {
    const char* e = std::getenv("NOBK_TP_DIRECT");
    return e && e[0] == '0';
  }();
  const uint32_t want = r27.u32, mapped = r11.u32;
  if (off || (want >> 24) != 10 || (mapped >> 24) == 10) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 8)
    REXKRNL_INFO("[tp] address map: keep {}.{}.{}.{} (was mapped to {}.{}.{}.{})", want >> 24, (want >> 16) & 255,
                 (want >> 8) & 255, want & 255, mapped >> 24, (mapped >> 16) & 255, (mapped >> 8) & 255, mapped & 255);
  r11.u64 = want;
}

// See online_hooks.toml "the ROUTE table". NOBK_TP_DIRECT=0 turns it off.
bool Tp_SkipRouteForPlayers(PPCRegister& r27, PPCRegister& r28, PPCRegister& r29) {
  static const bool off = [] {
    const char* e = std::getenv("NOBK_TP_DIRECT");
    return e && e[0] == '0';
  }();
  const uint32_t dst = r27.u32;
  if (off || (dst >> 24) != 10) return false;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 8) {
    const uint32_t gw = sesdbg::U32(r29.u32 - 8);
    REXKRNL_INFO("[tp] route: keep {}.{}.{}.{}:{} (route gateway {}.{}.{}.{})", dst >> 24, (dst >> 16) & 255,
                 (dst >> 8) & 255, dst & 255, r28.u32 & 0xFFFF, gw >> 24, (gw >> 16) & 255, (gw >> 8) & 255,
                 gw & 255);
  }
  return true;
}

void SesDbg_UdpPacket(PPCRegister& r31, PPCRegister& r3, PPCRegister& r1) {
  netdbg::Push('D', r3.u32);  // datagram read by CommUDP
  static int n = 0;
  if (!sesdbg::Budget(n, 80)) return;
  uint32_t ref = r31.u32, sp = r1.u32;
  // packet buffer: global at r30-0x520 is not in a register here; the header
  // words are logged by SesDbg_UdpControl. Here: who received what from where.
  REXKRNL_INFO(
      "[sesdbg] UDP ref={:08X} len={} seq={:08X} ack={:08X} body={:08X} from {} | ref state={} "
      "connident={:08X} clientid={:08X} peer {} sock={:08X}",
      ref, int32_t(r3.u32), sesdbg::U32(0x836EE498), sesdbg::U32(0x836EE49C),
      sesdbg::U32(0x836EE4A0), sesdbg::Addr(sp + 0x60), sesdbg::U32(ref + 0x9c),
      sesdbg::U32(ref + 0xa0), sesdbg::U32(ref + 0xa4), sesdbg::Addr(ref + 0x8c),
      sesdbg::U32(ref + 0x88));
}

// CommUDP invokes this reassembly/drain routine only after it accepted a
// reliable data sequence. Its optional push callback at ref + 0x3c is null in
// this game path, so application packets must be collected through CommUDP's
// virtual receive method at vtable + 0x34 (sub_82875CC8).
void SesDbg_CommUdpProcess(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<uint32_t> calls{0};
  uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n >= 200) return;
  const uint32_t ref = r3.u32;
  REXKRNL_INFO(
      "[commudp] process #{} ref={:08X} now={} appcb={:08X} state={} recvwin={}/{}/{} sendwin={}/{}/{}",
      n, ref, r4.u32, sesdbg::U32(ref + 0x3c), sesdbg::U32(ref + 0x9c),
      sesdbg::U32(ref + 0xcc), sesdbg::U32(ref + 0xd0), sesdbg::U32(ref + 0xd4),
      sesdbg::U32(ref + 0xd8), sesdbg::U32(ref + 0xdc), sesdbg::U32(ref + 0xe0));
  // The earlier run established that +0x3c is zero. Dump the first 0xa0 bytes
  // for only the first three drains so the ref's real callback/data pair can
  // be identified without relying on guessed DirtySock structure offsets.
  if (n < 3) {
    for (uint32_t off = 0; off < 0xa0; off += 0x20) {
      NOBK_DIAG_INFO("[commudp] ref+{:02X}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                   off, sesdbg::U32(ref + off), sesdbg::U32(ref + off + 4),
                   sesdbg::U32(ref + off + 8), sesdbg::U32(ref + off + 12),
                   sesdbg::U32(ref + off + 16), sesdbg::U32(ref + off + 20),
                   sesdbg::U32(ref + off + 24), sesdbg::U32(ref + off + 28));
    }
  }
}

// CommUDP's virtual pull receive method (vtable + 0x34). A null +0x3c push
// callback is normal for this title; the game polls this method instead. Keep
// the destination paired with its caller-provided output-length slot so its
// successful return can expose the exact payload and guest caller below.
inline std::mutex g_commudp_recv_mu;
inline std::unordered_map<uint32_t, uint32_t> g_commudp_recv_dst;
void SesDbg_CommUdpRecv(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  static std::atomic<uint32_t> calls{0};
  const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (r6.u32) {
    std::lock_guard<std::mutex> lock(g_commudp_recv_mu);
    g_commudp_recv_dst[r6.u32] = r4.u32;
  }
  if (n < 80 || (n < 4096 && n % 128 == 0)) {
    NOBK_DIAG_INFO("[commudp] pull-recv #{} ref={:08X} dst={:08X} max={} outlen={:08X} state={}",
                 n, r3.u32, r4.u32, r5.u32, r6.u32, sesdbg::U32(r3.u32 + 0x9c));
  }
}

// sub_82875CC8 has loaded its return length into r3 at 0x82875D44. Positive
// returns are the decisive hand-off: log the payload and guest call chain once
// per packet, then patch the caller rather than guessing a dispatcher address.
void SesDbg_CommUdpRecvResult(PPCRegister& r3, PPCRegister& r30, PPCRegister& r1) {
  const uint32_t len = r3.u32;
  if (!len || len > 0x4bc) return;
  uint32_t dst = 0;
  {
    std::lock_guard<std::mutex> lock(g_commudp_recv_mu);
    if (const auto it = g_commudp_recv_dst.find(r30.u32); it != g_commudp_recv_dst.end()) dst = it->second;
  }
  static std::atomic<uint32_t> packets{0};
  const uint32_t n = packets.fetch_add(1, std::memory_order_relaxed);
  if (n < 200) {
    NOBK_DIAG_INFO("[commudp] pull-result #{} len={} dst={:08X} text='{}' caller:{}",
                 n, len, dst, sesdbg::Str(dst, std::min<uint32_t>(len, 120)), sesdbg::Chain(r1.u32, 10));
  }
}

// The CommUDP stream assembler has completed an internal record. r29 points at
// its record (u16 length at +4, bytes at +8); r11 is the CommUDP owner's vtable
// callback. This is transport control/timing plumbing, not the OSDK dispatcher.
void SesDbg_CommUdpMessageReady(PPCRegister& r3, PPCRegister& r11, PPCRegister& r29,
                                PPCRegister& r1) {
  const uint32_t rec = r29.u32, len = sesdbg::U16(rec + 4);
  static std::atomic<uint32_t> messages{0};
  const uint32_t n = messages.fetch_add(1, std::memory_order_relaxed);
  if (n < 200 && len && len <= 0x4bc) {
    // `text` alone is misleading here: this stream also carries binary match
    // frames. The words expose its exact framing even when it contains NULs.
    NOBK_DIAG_INFO("[commudp] message-ready #{} owner={:08X} callback={:08X} len={} "
                 "words={:08X} {:08X} {:08X} {:08X} text='{}' caller:{}",
                 n, r3.u32, r11.u32, len, sesdbg::U32(rec + 8), sesdbg::U32(rec + 12),
                 sesdbg::U32(rec + 16), sesdbg::U32(rec + 20),
                 sesdbg::Str(rec + 8, std::min<uint32_t>(len, 120)), sesdbg::Chain(r1.u32, 10));
  }
}

// Runs for every completed record after the stream loaded its trailing tag
// from record+6, but before it decides whether tag 2 warrants the owner's
// callback. The callback probe is intentionally silent for tag-0 timing frames.
// This reveals the raw record type and the callback that *would* receive tag 2.
void SesDbg_CommUdpRecord(PPCRegister& r31, PPCRegister& r29, PPCRegister& r11,
                          PPCRegister& r1) {
  const uint32_t stream = r31.u32, rec = r29.u32, len = sesdbg::U16(rec + 4);
  if (!len || len > 0x4bc) return;
  static std::atomic<uint32_t> records{0};
  const uint32_t n = records.fetch_add(1, std::memory_order_relaxed);
  const uint32_t owner = sesdbg::U32(stream);
  const uint32_t callback = owner && owner != 0xDEADBEEF ? sesdbg::U32(owner + 28) : 0;
  if (n >= 200) return;
  NOBK_DIAG_INFO("[commudp] record #{} stream={:08X} owner={:08X} tag={} callback={:08X} len={} "
               "words={:08X} {:08X} {:08X} {:08X} text='{}' caller:{}",
               n, stream, owner, r11.u32, callback, len, sesdbg::U32(rec + 8),
               sesdbg::U32(rec + 12), sesdbg::U32(rec + 16), sesdbg::U32(rec + 20),
               sesdbg::Str(rec + 8, std::min<uint32_t>(len, 120)), sesdbg::Chain(r1.u32, 10));
}

// This is the actual application notification after the CommUDP stream worker
// buffered one or more normal (non-tag-2) records. `r3` is the receiver context
// held at stream+0x1e4 and `r11` is its callback from stream+0x1e8. It is the
// handoff that follows a successful `osdk/ctrl` pull; unlike the tag-2 callback,
// it must receive normal lobby messages. Read-only by design.
void SesDbg_CommUdpStreamNotify(PPCRegister& r3, PPCRegister& r11,
                                PPCRegister& r31, PPCRegister& r1) {
  static std::atomic<uint32_t> calls{0};
  const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n >= 200) return;
  const uint32_t stream = r31.u32;
  NOBK_DIAG_INFO("[commudp] stream-notify #{} stream={:08X} receiver={:08X} callback={:08X} "
               "pending={} base={:08X} used={} caller:{}",
               n, stream, r3.u32, r11.u32, sesdbg::U32(stream + 492),
               sesdbg::U32(stream + 428), sesdbg::U32(stream + 432),
               sesdbg::Chain(r1.u32, 10));
}

// Called once for each CommUDP stream-worker pass, after it resolved the stream
// from its owning worker. It records why the notification above was skipped:
// no buffered bytes, no application callback, or no completed record. It is
// deliberately bounded and does not alter the stream.
void SesDbg_CommUdpStreamWorker(PPCRegister& r3, PPCRegister& r31,
                                PPCRegister& r1) {
  static std::atomic<uint32_t> calls{0};
  const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n >= 80 || !r31.u32) return;
  const uint32_t stream = r31.u32;
  NOBK_DIAG_INFO("[commudp] stream-worker #{} worker={:08X} stream={:08X} base={:08X} used={} "
               "receiver={:08X} callback={:08X} pending={} busy={} caller:{}",
               n, r3.u32, stream, sesdbg::U32(stream + 428), sesdbg::U32(stream + 432),
               sesdbg::U32(stream + 484), sesdbg::U32(stream + 488),
               sesdbg::U32(stream + 492), sesdbg::U32(stream + 496), sesdbg::Chain(r1.u32, 8));
}

// Final CommUDP application delivery sites in sub_82874448. These probes
// read registers only: did a validated/reassembled datagram reach the ref's
// optional push callback, and which guest function was selected? No payload or
// protocol state is changed. Logging is bounded even on a busy link.
inline void NetDbg_CommUdpDeliver(char path, PPCRegister& r30, PPCRegister& r11,
                                   PPCRegister& r4, PPCRegister& r5, PPCRegister& r6) {
  if (!diag::On()) return;
  static std::atomic<uint32_t> calls{0};
  uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
  if (n < 32 || (n < 4096 && n % 128 == 0)) {
    NOBK_DIAG_INFO("[netdbg] CommUDP delivery {} #{} ref={:08X} callback={:08X} "
                 "payload={:08X} word={:08X} r6={:08X}",
                 path, n, r30.u32, r11.u32, r4.u32, r5.u32, r6.u32);
  }
}

void NetDbg_CommUdpDeliverA(PPCRegister& r30, PPCRegister& r11, PPCRegister& r4,
                             PPCRegister& r5, PPCRegister& r6) {
  NetDbg_CommUdpDeliver('A', r30, r11, r4, r5, r6);
}
void NetDbg_CommUdpDeliverB(PPCRegister& r30, PPCRegister& r11, PPCRegister& r4,
                             PPCRegister& r5, PPCRegister& r6) {
  NetDbg_CommUdpDeliver('B', r30, r11, r4, r5, r6);
}

void SesDbg_UdpControl(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5) {
  static int n = 0;
  if (!sesdbg::Budget(n, 80)) return;
  uint32_t ref = r3.u32, pkt = r4.u32;
  NOBK_DIAG_INFO("[sesdbg] UDP control ref={:08X} state={} seq={} ack={:08X} body={:08X} from {}",
               ref, sesdbg::U32(ref + 0x9c), sesdbg::U32(pkt + 8), sesdbg::U32(pkt + 0xc),
               sesdbg::U32(pkt + 0x10), sesdbg::Addr(r5.u32));
}


// [online] ConnApi link check: +0x59 is "comm link usable" (CommUDP status
// 2/3); when it drops to 0 the client is disconnected (session status 6).
void SesDbg_LinkCheck(PPCRegister& r3, PPCRegister& r30, PPCRegister& r31) {
  static uint32_t last_log = 0;
  static int dead_logs = 0;
  uint32_t st = r3.u32;
  uint32_t now = sesdbg::U32(st), last = sesdbg::U32(st + 0x20), limit = sesdbg::U32(r30.u32 + 0xdc);
  uint8_t usable = sesdbg::U8(st + 0x59), online = sesdbg::U8(st + 0x58);
  uint32_t idle = now - last;
  bool bad = !usable || idle > limit;
  if (bad) {
    if (dead_logs++ > 20) return;
  } else if (now - last_log < 10000) {
    return;
  }
  last_log = now;
  NOBK_DIAG_INFO("[sesdbg] LinkCheck client={:08X} idle={}ms limit={}ms online={} usable={} pkts={} {}",
               r31.u32, idle, limit, online, usable, sesdbg::U32(st + 4), bad ? "<-- DEAD" : "");
}

// [online] CommUDP closes a link (state -> 5). r31 = ref, r1 = frame.
void SesDbg_UdpClose(PPCRegister& r31, PPCRegister& r1) {
  g_online_match = false;
  static int n = 0;
  if (!sesdbg::Budget(n, 20)) return;
  uint32_t ref = r31.u32;
  REXKRNL_INFO(
      "[sesdbg] CommUDP CLOSE ref={:08X} state={} senderr={} lastsend={} lastrecv={} peer {} via{}",
      ref, sesdbg::U32(ref + 0x9c), int32_t(sesdbg::U32(ref + 0xec)), sesdbg::U32(ref + 0xf0),
      sesdbg::U32(ref + 0xf4), sesdbg::Addr(ref + 0x8c), sesdbg::Chain(r1.u32));
}

// [online] CommUDP got an INIT with a different connection id: link closed.
void SesDbg_UdpBadInit(PPCRegister& r3, PPCRegister& r4) {
  static int n = 0;
  if (!sesdbg::Budget(n, 20)) return;
  NOBK_DIAG_INFO("[sesdbg] CommUDP INIT with foreign id {:08X} (ours {:08X}) body={:08X} -> CLOSE",
               sesdbg::U32(r4.u32 + 0xc), sesdbg::U32(r3.u32 + 0xa0), sesdbg::U32(r4.u32 + 0x10));
}

// [online] CommUDP drops an open link when (now - last receive stamp) > 120 s
// and nothing was sent for 2 s. The receive stamp is never set in this port,
// so any 2 s hitch ended the match. Disable the rule; ConnApi's own idle
// check ('time', 60 s, fed by real receive times) still catches a dead peer.
void Online_CommUdpIdleCheck(PPCRegister& r11, PPCRegister& r27, PPCRegister& r31) {
  static int n = 0;
  uint32_t since_send = r27.u32 - sesdbg::U32(r31.u32 + 0xf0);
  if (int32_t(r11.u32) > 120000 && int32_t(since_send) >= 2000 && sesdbg::Budget(n, 10)) {
    REXKRNL_INFO("[online] CommUDP idle rule suppressed (no send for {} ms)", since_send);
  }
  r11.u64 = 0;
}

// ===========================================================================
// Match events (step 1: discovery). The game's gameplay code posts every event
// ("Gameplay::Goal", "Gameplay::Substitution", ...) through one function
// (0x826072f0: r5 = message type, whose first word is the djb2 hash of its
// name, r6 = payload, r7 = payload size). This hook logs which message types
// a match produces and dumps the payload of the interesting ones, so the next
// step can read the scorer, the minute, cards, substitutions and line-ups.
// ===========================================================================
namespace matchev {

inline uint32_t Djb2(const char* s) {
  uint32_t h = 5381;
  for (;;) {
    uint8_t c = uint8_t(*s++);
    h = h * 33 + c;
    if (!c) return h;
  }
}

struct Type { uint32_t hash; const char* name; bool detail; };

inline const std::vector<Type>& Types() {
  static std::vector<Type> types = [] {
    static const char* names[] = {
  "Advantage", "AnimOverrideFacialExpression", "AnimOverrideFoot", "AnimOverrideStance",
  "AudioBreakawayEvent", "AudioClearShootingChanceEvent", "AudioCrossInProgressEvent",
  "AudioDangerousLooseBall", "AudioPreGoalEvent", "AudioTagEvent", "BallBounce", "BallHitBar", "BallHitEnv",
  "BallHitGrass", "BallHitNet", "BallHitPost", "BallOOP", "BallOutOfPitch", "BallTouch", "BalloonsDressing",
  "BlockAttempt", "BlockEvaluation", "BlockFact", "CallForPassFailed", "CallForPassSucceeded",
  "CallPassRequest", "CallShotRequest", "CallThroughPassRequest", "ChangeTeamTacticsPresetSlot", "CheatFoul",
  "CheatGoal", "ChoreoDressing", "ClockSpeedRate", "Collision", "ControllerIds", "CornerEvaluation",
  "CornerKick", "CornerKickCommand", "CreatePlayerEnded", "CrowdEnableCmd", "CustomTeamTacticRequest",
  "DPadStateChanged", "DPadTacticRequest", "DelayedCard", "DestroyGame", "DifficultyChange",
  "DisableCrowdDressings", "DisableInput", "DoneCreatePlayerCmd", "DoubleHoldersDressing", "DrawGame",
  "DribbleEvaluationCmd", "DribbleProgress", "EVENT_MOVE_OBJECT", "EVENT_TELEPORT_PLAYER", "EnableInput",
  "EndHalf", "EndHalfWaitCommand", "EndHalfWaitRequest", "EndPlayEvaluation", "EndStartPlayWait",
  "EndedCreatePlayerCmd", "EntryEvaluation", "EnvironmentCollisionGeo", "Event", "FREE_ROAM_GAME",
  "FenceFlagsDressing", "FocalPlayerChange", "ForceDesync", "Foul", "FoulEvaluation", "FoulType", "FreeKick",
  "FreeKickCommand", "FreeKickEvaluation", "FreeKickRequest", "GameOver", "GameStarted", "GameStateChange",
  "GameUpdated", "GameplayCardTimerExpiryEvent", "Goal", "GoalEvaluation", "GoalEvaluationCmd",
  "GoalKickCommand", "GoalKickEvaluation", "GoalKickRequest", "GoalType", "HLInputUpdate", "HaltSequenceCmd",
  "HugeJerseyDressing", "INVALID_PLAYER_ID", "InPlayLineupChange", "InitShootOutData", "InjuryTime",
  "InputContainer", "JumpFact", "KeeperUpForCorner", "Kickoff", "KickoffCommand", "KickoffRequest",
  "LayoffFreeKickRequest", "NISSequenceComplete", "NisPause", "Offside", "OutOfPlayLineupChange",
  "OutOfPlayLineupChangeComplete", "PASSTYPE_NONE", "PLAYER_POSITION_GK", "PassAttempt", "PassEvaluation",
  "PassEvaluationCmd", "PassFact", "PassResult", "PassType", "Pause", "PenaltyKick", "PenaltyKickCommand",
  "PenaltyKickEvaluation", "PlaySequenceCmd", "PlaySequenceEndCmd", "PlaySequenceReceived",
  "PlaySequenceResetCmd", "PlaySequenceResetReceived", "PlaySequenceSkipCmd", "PlaySequenceStartCmd",
  "PlayerInPosition", "PlayerOutOfPosition", "PlayerPosition", "PlayerReactionTriggeredEvent",
  "PossessionChange", "PostSequenceResume", "PracticeRestart", "PreGoal", "PrepareNewGame", "QuitMatch",
  "RecoverInjury", "ReplayEnd", "ReplayStart", "Reposition", "RepositionCommand", "RepositionRequest",
  "ResetAfterTeamManagement", "ResetPlayablePlayers", "Restart", "RestartPlayerData", "RestartTeamData",
  "ResumeGameStateCmd", "SKILLMOVETYPE_UNKNOWN", "SSDFileReady", "SaveAttempt", "SaveEvaluation", "SaveFact",
  "ScarvesDressing", "SecondCornerKickReceiverRequest", "SequencePlaybackEnded", "SetControllerConfig",
  "SetDrawMode", "SetGoalieStupidity", "SetPiecePlayersInfo", "SetPlayTransitionAck",
  "SetShootoutKickerOrder", "SetSimseedValue", "SetTeamAttackingTactics", "SetTeamDefensiveTactics",
  "SetTeamGameDifficulty", "SetTeamMovementFreeze", "SetTeamStupidity", "SetTime", "SetTimeScale",
  "SetplayActionStart", "SetplayReady", "SetplayReadyPerTeam", "SetplayStarted", "SetplaySync",
  "ShootOutCommand", "ShootOutRequest", "ShotAttempt", "ShotEvaluation", "ShotEvaluationCmd", "ShotFact",
  "ShotMissed", "SideSelect", "SimpleRestart", "SkillMoveEvaluation", "StartCreatePlayerCmd",
  "StartPlayEvaluation", "StartPlayWaitRequest", "Substitution", "SubstitutionForInjury", "SupportInfo",
  "TackleAttempt", "TackleEvaluationCmd", "TackleFact", "TeamEmotionChange", "TeamMentalityRequest",
  "TeamMomentumChange", "TeamSide", "TeamUrgency", "ThrowIn", "ThrowInAttempt", "ThrowInCommand",
  "ThrowInEvaluation", "ThrowInRequest", "TogglePause", "UCCMoveTriggered", "Unpause", "Update",
  "UpdateAttribulatorTuningDataCmd", "UpgradeInjury", "UserPlayerEvalSet", "VerifyGameDatabase", "Vibration",
  "WavingFlagsDressing", "WeatherType",
    };
    static const char* detailed[] = {
      "Goal", "CheatGoal", "PreGoal", "DelayedCard", "Foul", "Substitution", "SubstitutionForInjury",
      "InPlayLineupChange", "OutOfPlayLineupChange", "OutOfPlayLineupChangeComplete", "EndHalf",
      "GameStarted", "Kickoff", "InjuryTime", "Offside", "PenaltyKick", "SetTime", "GameStateChange",
      "RestartTeamData", "SetPiecePlayersInfo", "ResetPlayablePlayers", "GoalEvaluation", "Restart",
      "FoulEvaluation", "Injury", "StartHalf", "GameOver", "Advantage", "FreeKick",
    };
    std::vector<Type> v;
    for (const char* n : names) {
      std::string full = std::string("Gameplay::") + n;
      bool d = false;
      for (const char* x : detailed) d = d || std::strcmp(x, n) == 0;
      v.push_back({Djb2(full.c_str()), n, d});
    }
    // online sync messages (full names; the whole name is hashed)
    for (const char* n : {"FifaOnline::SyncFrame", "FifaOnline::OnlineEvent", "FifaOnline::AddController",
                          "FifaOnline::RemoveController", "Gameplay::InputContainer"})
      v.push_back({Djb2(n), n, true});
    return v;
  }();
  return types;
}

inline const Type* Find(uint32_t hash) {
  for (const auto& t : Types())
    if (t.hash == hash) return &t;
  return nullptr;
}

// True only if [addr, addr+len) is committed, readable guest memory. A payload word
// like 0x3F800000 (the float 1.0) looks like an address but is not mapped: reading it
// crashed the game in the first test.
// The guest heap can say "readable" while the host page is still protected
// (the second test crashed on 0x40010000), so the host pages are checked too.
inline bool HostReadable(const void* p, size_t n) {
#if defined(_WIN32)
  const uint8_t* at = static_cast<const uint8_t*>(p);
  const uint8_t* end = at + n;
  while (at < end) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(at, &mbi, sizeof(mbi))) return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & ok))
      return false;
    at = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
  }
  return true;
#else
  (void)p; (void)n;
  return false;
#endif
}

inline bool Readable(uint32_t addr, uint32_t len) {
  if (addr < 0x10000 || addr + len < addr) return false;
  auto* heap = REX_KERNEL_MEMORY()->LookupHeap(addr);
  if (!heap) return false;
  if (heap->QueryRangeAccess(addr, addr + len - 1) == rex::memory::PageAccess::kNoAccess) return false;
  const uint8_t* host = sesdbg::Ptr(addr);
  return host && HostReadable(host, len);
}

inline bool LooksLikePointer(uint32_t v) {
  // heap / stack / physical ranges; not the game's code and data image (0x82000000..0x84000000)
  if (v < 0x30000000 || v >= 0xFFF00000 || (v >= 0x82000000 && v < 0x84000000) || (v & 3)) return false;
  return Readable(v, 0x200);
}

// Printable ASCII and UTF-16BE runs (player names) in [addr, addr+len).
inline std::string Strings(uint32_t addr, uint32_t len) {
  const uint8_t* p = sesdbg::Ptr(addr);
  if (!p) return "";
  std::string out, run;
  for (uint32_t i = 0; i < len; ++i) {
    uint8_t c = p[i];
    if (c >= 32 && c < 127) run += char(c);
    else { if (run.size() >= 4) out += " '" + run + "'"; run.clear(); }
  }
  if (run.size() >= 4) out += " '" + run + "'";
  run.clear();
  for (uint32_t i = 0; i + 1 < len; i += 2) {
    if (p[i] == 0 && p[i + 1] >= 32 && p[i + 1] < 127) run += char(p[i + 1]);
    else { if (run.size() >= 3) out += " u'" + run + "'"; run.clear(); }
  }
  if (run.size() >= 3) out += " u'" + run + "'";
  return out;
}

inline std::string Hex(uint32_t addr, uint32_t len) {
  std::string s;
  char b[12];
  for (uint32_t i = 0; i + 4 <= len; i += 4) {
    std::snprintf(b, sizeof(b), "%s%08X", i ? " " : "", sesdbg::U32(addr + i));
    s += b;
  }
  return s;
}

// A name as the game stores it: 3..39 bytes of letters (Latin-1 >= 0xC0 too, for
// Ö, Ç...), spaces, '-', '.', '\'', terminated by 0, with at least 2 ASCII letters.
// upper: jersey name (no lower case). !upper: full name (needs lower case).
inline bool NameAt(uint32_t addr, bool upper, std::string* out) {
  if (!Readable(addr, 48)) return false;
  const uint8_t* p = sesdbg::Ptr(addr);
  std::string s;
  int letters = 0, lower = 0;
  for (int i = 0; i < 40; ++i) {
    uint8_t c = p[i];
    if (!c) break;
    bool up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z';
    if (!(up || lo || c == ' ' || c == '-' || c == '.' || c == '\'' || c >= 0xC0)) return false;
    if (upper && lo) return false;
    letters += (up || lo) ? 1 : 0;
    lower += lo ? 1 : 0;
    s += char(c);
  }
  if (s.size() < 3 || s.size() >= 40 || letters < 2 || s[0] == ' ') return false;
  if (!upper && lower < 2) return false;
  if (out) *out = s;
  return true;
}

// ---------------------------------------------------------------------------
// Line-ups.
// Squad: the game keeps one record per squad player, 0x5BC bytes apart, one
// array per team. Each record holds the fifa_ng_db playerid three times at
// +0x348/+0x34C/+0x350 (the face picture is h<playerid>) and the short name at
// +0xE5. Match order: the event player index is 0..17 for the home team (XI in
// formation order, then the bench as on the team sheet) and 18..35 for the away
// team. That order is kept as plain lists of player ids (4 bytes apart); the one
// that occurs most often in memory, starting right after a non-member word, wins.
// Everything is searched once per match, on its own thread (about 0.5 s).
constexpr uint32_t kRecStride = 0x5BC;
constexpr uint32_t kRecId = 0x348;

inline std::string ShortName(uint32_t rec) {
  std::string s;
  if (!Readable(rec + 0xE5, 48)) return s;
  const uint8_t* p = sesdbg::Ptr(rec + 0xE5);
  for (int i = 0; i < 40 && p[i]; ++i) s += char(p[i]);  // UTF-8 as stored
  return s;
}

struct Roster {
  std::mutex mu;
  bool ready = false;
  std::vector<uint32_t> match[2];             // match order, 18 ids per team
  std::unordered_map<uint32_t, std::string> names;  // playerid -> short name
};
inline Roster& TheRoster() { static Roster r; return r; }

inline uint32_t PlayerId(uint32_t idx) {
  Roster& r = TheRoster();
  std::lock_guard<std::mutex> lock(r.mu);
  if (!r.ready) return 0;
  const auto& t = r.match[idx >= 18 ? 1 : 0];
  uint32_t slot = idx >= 18 ? idx - 18 : idx;
  return slot < t.size() ? t[slot] : 0;
}
inline std::string PlayerName(uint32_t idx) {
  uint32_t id = PlayerId(idx);
  Roster& r = TheRoster();
  std::lock_guard<std::mutex> lock(r.mu);
  auto it = r.names.find(id);
  return (it == r.names.end() ? std::string("?") : it->second) + "#" + std::to_string(id);
}

inline uint32_t BE32(const uint8_t* h) {
  return (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) | (uint32_t(h[2]) << 8) | h[3];
}

// Calls fn(addr, host) for every readable 64 KB page of the guest heap.
template <typename F>
inline void ForEachPage(F fn) {
  for (uint64_t base : {0xA0000000ull, 0x40000000ull}) {
    for (uint64_t a = base; a < base + 0x40000000ull; a += 0x10000) {
      uint32_t addr = uint32_t(a);
      if (!Readable(addr, 0x10000)) continue;
      const uint8_t* h = sesdbg::Ptr(addr);
      if (h && !fn(addr, h)) return;
    }
  }
}

// Squad arrays: runs of records (player id triple) exactly kRecStride apart.
inline std::vector<std::pair<uint32_t, uint32_t>> FindSquads() {
  std::vector<uint32_t> recs;
  ForEachPage([&](uint32_t addr, const uint8_t* h) {
    for (uint32_t i = 0; i + 12 <= 0x10000; i += 4) {
      if (h[i] != 0) continue;
      if (std::memcmp(h + i, h + i + 4, 4) != 0 || std::memcmp(h + i, h + i + 8, 4) != 0) continue;
      uint32_t id = BE32(h + i);
      if (id == 0 || id > 300000 || addr + i < kRecId) continue;
      recs.push_back(addr + i - kRecId);
    }
    return recs.size() < 60000;
  });
  std::sort(recs.begin(), recs.end());
  recs.erase(std::unique(recs.begin(), recs.end()), recs.end());
  std::vector<std::pair<uint32_t, uint32_t>> runs;
  for (size_t i = 0; i < recs.size();) {
    size_t j = i;
    while (j + 1 < recs.size() && recs[j + 1] == recs[j] + kRecStride) ++j;
    if (j - i + 1 >= 16) runs.push_back({recs[i], uint32_t(j - i + 1)});
    i = j + 1;
  }
  std::sort(runs.begin(), runs.end(), [](auto& x, auto& y) { return x.second > y.second; });
  if (runs.size() > 2) runs.resize(2);
  std::sort(runs.begin(), runs.end());  // address order: home, away
  return runs;
}

// Most frequent list of 18 distinct squad ids, 4 bytes apart, that starts right
// after a word that is not a squad id.
inline std::vector<uint32_t> FindMatchOrder(const std::vector<uint32_t>& squad) {
  std::unordered_map<uint32_t, int> in;
  for (uint32_t id : squad) in[id] = 1;
  std::map<std::vector<uint32_t>, int> votes;
  ForEachPage([&](uint32_t addr, const uint8_t* h) {
    (void)addr;
    for (uint32_t i = 4; i + 18 * 4 <= 0x10000; i += 4) {
      if (!in.count(BE32(h + i)) || in.count(BE32(h + i - 4))) continue;
      std::vector<uint32_t> v;
      for (uint32_t k = 0; k < 18; ++k) {
        uint32_t id = BE32(h + i + 4 * k);
        if (!in.count(id) || std::find(v.begin(), v.end(), id) != v.end()) break;
        v.push_back(id);
      }
      if (v.size() == 18) ++votes[v];
    }
    return true;
  });
  std::vector<uint32_t> best;
  int n = 0;
  for (auto& [v, c] : votes)
    if (c > n) { n = c; best = v; }
  REXKRNL_INFO("[match] match order: {} candidate lists, best seen {} times", votes.size(), n);
  return best;
}

// ---------------------------------------------------------------------------
// Reporting to the NOBK server: small JSON messages over UDP to the match relay
// (online_relay), wrapped in the relay header with type 5 ("match event") and
// our virtual address. The server only accepts them from the address the
// player's relay traffic comes from. Each message is sent 3 times (now, +1 s,
// +3 s); the server drops repeats by "seq".
class EventSender {
 public:
  static EventSender& Get() { static EventSender s; return s; }

  void Send(std::string json) {
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(mu_);
    auto now = std::chrono::steady_clock::now();
    for (int ms : {0, 1000, 3000}) queue_.push_back({now + std::chrono::milliseconds(ms), json});
    if (!thread_started_) {
      thread_started_ = true;
      std::thread([this] { Run(); }).detach();
    }
  }

 private:
  struct Item { std::chrono::steady_clock::time_point at; std::string json; };

  bool Enabled() {
    static int enabled = -1;
    if (enabled < 0) {
      enabled = 0;
      std::string v = rex::cvar::Query<std::string>("online_relay");
      unsigned a, b, c, d, port = 3658;
      int n = std::sscanf(v.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
      if (n >= 4 && a < 256 && b < 256 && c < 256 && d < 256 && port && port < 65536) {
        addr_[0] = uint8_t(a); addr_[1] = uint8_t(b); addr_[2] = uint8_t(c); addr_[3] = uint8_t(d);
        port_ = uint16_t(port);
        enabled = 1;
      } else if (!v.empty()) {
        REXKRNL_WARN("[match] online_relay '{}' is not an IPv4 address; match events are not reported", v);
      }
    }
    return enabled == 1;
  }

  void Run() {
    for (;;) {
      std::vector<std::string> due;
      {
        std::lock_guard<std::mutex> lock(mu_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = queue_.begin(); it != queue_.end();) {
          if (it->at <= now) { due.push_back(it->json); it = queue_.erase(it); }
          else ++it;
        }
      }
      for (auto& j : due) SendNow(j);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }


 public:
  // Ping to the relay (type 6, echoed back by the server): 8-byte send time in
  // microseconds. Replies are read by a thread of its own; RTTs are summed up
  // per second by NetProbe.
  void Ping(uint64_t t_us) {
    if (!Enabled()) return;
    std::string body(8, '\0');
    for (int i = 0; i < 8; ++i) body[i] = char(t_us >> (56 - 8 * i));
    SendRaw(6, body);
  }
  std::atomic<int64_t> rtt_sum_us{0};
  std::atomic<int64_t> rtt_max_us{0};
  std::atomic<int> rtt_n{0};
  std::atomic<int> sent_pings{0};


 private:
  void SendNow(const std::string& json) { SendRaw(5, json); }

  void SendRaw(uint8_t type, const std::string& json) {
    using rex::system::XSocket;
    using rex::system::N_XSOCKADDR_IN;
    std::lock_guard<std::mutex> slock(send_mu_);
    if (!sock_) {
      sock_ = new XSocket(REX_KERNEL_STATE());
      if (sock_->Initialize(XSocket::X_AF_INET, XSocket::X_SOCK_DGRAM, XSocket::X_IPPROTO_UDP) != 0) {
        REXKRNL_WARN("[match] could not open the event socket");
        return;
      }
      std::thread([this] { Receive(); }).detach();
    }
    uint64_t xuid = 0;
    if (auto* ks = REX_KERNEL_STATE())
      if (auto* up = ks->user_profile()) xuid = up->xuid();
    uint32_t low = uint32_t(xuid & 0xFFFFFF);
    if (!low) low = 1;
    std::vector<uint8_t> pkt(16 + json.size());
    pkt[0] = 'R'; pkt[1] = 'L'; pkt[2] = 1; pkt[3] = type;
    pkt[4] = 10; pkt[5] = uint8_t(low >> 16); pkt[6] = uint8_t(low >> 8); pkt[7] = uint8_t(low);
    std::memcpy(pkt.data() + 16, json.data(), json.size());
    N_XSOCKADDR_IN to;
    to.sin_family = 2;  // AF_INET
    std::memcpy(&to.sin_port, &port_be_()[0], 2);
    std::memcpy(&to.sin_addr, addr_, 4);
    std::memset(to.x_sin_zero, 0, sizeof(to.x_sin_zero));
    int ret = sock_->SendTo(pkt.data(), uint32_t(pkt.size()), 0, &to, 16);
    static int logged = 0;
    if (type == 5 && logged++ < 200) REXKRNL_INFO("[match] sent {} ({} bytes, ret {})", json, pkt.size(), ret);
  }

  void Receive() {
    using rex::system::N_XSOCKADDR_IN;
    uint8_t buf[512];
    for (;;) {
      N_XSOCKADDR_IN from;
      uint32_t from_len = 16;
      int n = sock_->RecvFrom(buf, sizeof(buf), 0, &from, &from_len);
      if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
      if (n < 24 || buf[0] != 'R' || buf[1] != 'L' || buf[3] != 6) continue;
      uint64_t t = 0;
      for (int i = 0; i < 8; ++i) t = (t << 8) | buf[16 + i];
      int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      int64_t rtt = now - int64_t(t);
      if (rtt < 0 || rtt > 5000000) continue;
      rtt_sum_us += rtt;
      ++rtt_n;
      int64_t m = rtt_max_us.load();
      while (rtt > m && !rtt_max_us.compare_exchange_weak(m, rtt)) {}
    }
  }

  const uint8_t* port_be_() {
    pbe_[0] = uint8_t(port_ >> 8);
    pbe_[1] = uint8_t(port_);
    return pbe_;
  }

  std::mutex mu_;
  std::mutex send_mu_;
  std::vector<Item> queue_;
  bool thread_started_ = false;
  rex::system::XSocket* sock_ = nullptr;
  uint8_t addr_[4] = {};
  uint16_t port_ = 0;
  uint8_t pbe_[2] = {};
};

struct MatchState {
  std::mutex mu;
  uint32_t seq = 0;
  uint32_t match = 0;  // random id per match, so the server can tell matches apart
};
inline MatchState& State() { static MatchState s; return s; }

// Adds "m" (match id) and "seq" and sends.
inline void Report(const std::string& body) {
  MatchState& st = State();
  std::string json;
  {
    std::lock_guard<std::mutex> lock(st.mu);
    json = "{\"m\":" + std::to_string(st.match) + ",\"seq\":" + std::to_string(++st.seq) + "," + body + "}";
  }
  EventSender::Get().Send(json);
}

// ---------------------------------------------------------------------------
// Online smoothness probe. The match simulation counts ticks (FifaClock +0x44,
// the clock object is at message bus +0x10). Offline it advances 60 times a
// second, evenly. A thread samples it every millisecond during a match and logs
// once a second: ticks advanced, the longest pause between two ticks, how many
// pauses were longer than 25 / 50 / 100 ms, plus the round trip to the relay
// (ping every 250 ms). Stalls with a good ping point at the game waiting for
// the other player's input; stalls together with ping spikes point at the
// network path.
class NetProbe {
 public:
  static NetProbe& Get() { static NetProbe p; return p; }

  void Start(uint32_t clock_obj) {
    if (running_.exchange(true)) return;
    clock_obj_ = clock_obj;
    std::thread([this] { Run(); }).detach();
  }
  void Stop() { running_ = false; }
  bool Running() const { return running_; }

 private:
  void Run() {
    using clk = std::chrono::steady_clock;
    REXKRNL_INFO("[net] probe started (clock object {:08X})", clock_obj_);
    uint32_t last_tick = 0;
    auto last_change = clk::now(), second = clk::now(), last_ping = clk::now();
    int ticks = 0, g25 = 0, g50 = 0, g100 = 0, jumps = 0, seconds = 0, samples = 0;
    double max_gap = 0;
    bool have = false;
    while (running_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      auto now = clk::now();
      ++samples;
      if (Readable(clock_obj_ + 0x44, 4)) {
        uint32_t t = sesdbg::U32(clock_obj_ + 0x44);
        if (!have) { last_tick = t; last_change = now; have = true; }
        if (t != last_tick) {
          double gap = std::chrono::duration<double, std::milli>(now - last_change).count();
          uint32_t d = t - last_tick;
          if (d < 1000) {
            ticks += int(d);
            if (d > 1) ++jumps;
            max_gap = std::max(max_gap, gap);
            if (gap > 25) ++g25;
            if (gap > 50) ++g50;
            // each stall on its own line: its timestamp can be matched with the packet log
            if (gap > 50) {
              REXKRNL_INFO("[net] stall {:.0f} ms, tick {} -> {}", gap, last_tick, t);
              netdbg::Dump(gap);
            }
            if (gap > 100) ++g100;
          }
          last_tick = t;
          last_change = now;
        }
      }
      if (now - last_ping >= std::chrono::milliseconds(250)) {
        last_ping = now;
        EventSender::Get().Ping(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count()));
      }
      if (now - second >= std::chrono::seconds(1)) {
        second = now;
        auto& es = EventSender::Get();
        int n = es.rtt_n.exchange(0);
        int64_t sum = es.rtt_sum_us.exchange(0), mx = es.rtt_max_us.exchange(0);
        REXKRNL_INFO("[net] t={}s sim {} ticks/s, longest pause {:.0f} ms, pauses >25ms {} >50ms {} >100ms {}, "
                     "multi-tick {} | relay ping avg {} ms max {} ms ({} replies) | {} samples",
                     ++seconds, ticks, max_gap, g25, g50, g100, jumps, n ? sum / n / 1000 : -1, mx / 1000, n, samples);
        ticks = g25 = g50 = g100 = jumps = samples = 0;
        max_gap = 0;
      }
    }
    REXKRNL_INFO("[net] probe stopped");
  }

  std::atomic<bool> running_{false};
  uint32_t clock_obj_ = 0;
};

inline void ScanRosterNow() {
  auto t0 = std::chrono::steady_clock::now();
  auto squads = FindSquads();
  if (squads.size() < 2) {
    REXKRNL_INFO("[match] roster: squads not found");
    return;
  }
  std::vector<uint32_t> squad[2];
  std::unordered_map<uint32_t, std::string> names;
  for (int t = 0; t < 2; ++t) {
    for (uint32_t k = 0; k < squads[t].second && k < 40; ++k) {
      uint32_t rec = squads[t].first + k * kRecStride;
      uint32_t id = sesdbg::U32(rec + kRecId);
      squad[t].push_back(id);
      names[id] = ShortName(rec);
    }
  }
  std::vector<uint32_t> order[2];
  for (int t = 0; t < 2; ++t) {
    order[t] = FindMatchOrder(squad[t]);
    if (order[t].size() != 18) {
      // fall back to the squad order: right for the XI, the bench may differ
      order[t].assign(squad[t].begin(), squad[t].begin() + std::min<size_t>(18, squad[t].size()));
      REXKRNL_INFO("[match] roster: team {} match order not found, using the squad order", t);
    }
  }
  {
    Roster& r = TheRoster();
    std::lock_guard<std::mutex> lock(r.mu);
    r.match[0] = order[0];
    r.match[1] = order[1];
    r.names = names;
    r.ready = true;
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  std::string ids[2];
  for (int t = 0; t < 2; ++t) {
    std::string list;
    for (size_t k = 0; k < order[t].size(); ++k) {
      list += " " + std::to_string(k) + "=" + names[order[t][k]];
      ids[t] += (k ? "," : "") + std::to_string(order[t][k]);
    }
    REXKRNL_INFO("[match] line-up {}:{}", t ? "away" : "home", list);
  }
  REXKRNL_INFO("[match] roster ready in {} ms", ms);
  Report("\"t\":\"lineup\",\"home\":[" + ids[0] + "],\"away\":[" + ids[1] + "]");
}

inline void ScanRoster() {
  {
    Roster& r = TheRoster();
    std::lock_guard<std::mutex> lock(r.mu);
    r.ready = false;
  }
  std::thread([] { ScanRosterNow(); }).detach();
}

}  // namespace matchev

// Hooked inside PostMessage right after the game stamped the payload (0x82607360):
// +0x0C = match clock in seconds (FifaClock 0x82678c50). r26 = message type,
// r30 = payload, r28 = size. Payload data starts at +0x18:
//   Goal:          team, player index, ?            (own goal: index of the other team)
//   Foul:          team, fouler, ?, victim, card    (1 none, 2 yellow, 3 second yellow, 4 red)
//   Substitution:  team, out, in
//   EndHalf:       half number;  InjuryTime: seconds added
void Match_PostMessage(PPCRegister& r26, PPCRegister& r30, PPCRegister& r28, PPCRegister& r31) {
  static std::mutex mu;
  static std::unordered_map<uint32_t, int> seen;
  if (!matchev::Readable(r26.u32, 4)) return;
  uint32_t hash = sesdbg::U32(r26.u32);
  const matchev::Type* t = matchev::Find(hash);
  if (diag::On()) {
    // Online sync diagnostics: every message type seen once (also unknown ones),
    // and the timing of the online sync messages (FifaOnline::SyncFrame ...).
    static std::mutex dmu;
    static std::unordered_map<uint32_t, int> types_seen;
    static std::unordered_map<uint32_t, std::pair<int, std::chrono::steady_clock::time_point>> rate;
    std::lock_guard<std::mutex> dl(dmu);
    int& c = types_seen[hash];
    if (c++ == 0)
      REXKRNL_INFO("[sync] message type {} ({:08X}) size {}", t ? t->name : "?", hash, r28.u32);
    if (t && (std::strncmp(t->name, "FifaOnline::", 12) == 0 || std::strcmp(t->name, "Gameplay::InputContainer") == 0)) {
      auto now = std::chrono::steady_clock::now();
      static auto t0 = now;
      uint32_t p0 = r30.u32, sz = std::min<uint32_t>(r28.u32, 0x60);
      if (c <= 120 && matchev::Readable(p0, std::max<uint32_t>(sz, 4)))
        REXKRNL_INFO("[sync] {} #{} at {} ms: {}", t->name, c,
                     std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count(), matchev::Hex(p0, sz));
      auto& r = rate[hash];
      if (r.first == 0) r.second = now;
      ++r.first;
      if (now - r.second >= std::chrono::seconds(5)) {
        REXKRNL_INFO("[sync] {}: {:.1f} per second", t->name, r.first / 5.0);
        r.first = 0;
        r.second = now;
      }
    }
  }
  static std::atomic<bool> in_match{false};
  static std::atomic<int> pro_sent{0};  // raw events sent this match
  // Be A Pro discovery: the payloads of the messages that should say who controls
  // which footballer (FocalPlayerChange, ControllerIds, SetControllerConfig) and how
  // well a pro plays (UserPlayerEvalSet), plus pass / shot / save facts for assists.
  // Their layout is not known yet: each is logged (hex) a limited number of times,
  // and the two key ones are sent to the server raw ("raw" events) so a single team
  // play match is enough to decode them.
  if (t && !t->detail && in_match) {
    static const struct { const char* name; int log; bool send; } pro[] = {
        {"FocalPlayerChange", 300, true}, {"UserPlayerEvalSet", 300, true}, {"ControllerIds", 40, true},
        {"SetControllerConfig", 40, true}, {"PossessionChange", 60, false}, {"PassResult", 60, false},
        {"PassFact", 40, false}, {"ShotAttempt", 40, false}, {"ShotFact", 40, false}, {"SaveFact", 30, false},
        {"TackleFact", 30, false}};
    for (const auto& pr : pro) {
      if (std::strcmp(pr.name, t->name) != 0) continue;
      static std::mutex pmu;
      static std::unordered_map<uint32_t, int> logged;
      uint32_t p0 = r30.u32;
      uint32_t sz = r28.u32 > 0x18 ? std::min<uint32_t>(r28.u32 - 0x18, 0x40) : 0;
      if (!sz || !matchev::Readable(p0, 0x18 + sz)) break;
      std::lock_guard<std::mutex> pl(pmu);
      int& c = logged[hash];
      uint32_t clock = sesdbg::U32(p0 + 0x0C);
      if (c++ < pr.log)
        REXKRNL_INFO("[pro] {} at {:02}:{:02} size {}: {}", pr.name, clock / 60, clock % 60, r28.u32,
                     matchev::Hex(p0 + 0x18, sz));
      if (pr.send && pro_sent < 600) {
        ++pro_sent;
        std::string w;
        for (uint32_t i = 0; i + 4 <= sz && i < 0x30; i += 4) w += (i ? "," : "") + std::to_string(sesdbg::U32(p0 + 0x18 + i));
        matchev::Report(std::string("\"t\":\"raw\",\"k\":\"") + pr.name + "\",\"clock\":" + std::to_string(clock) +
                        ",\"w\":[" + w + "]");
      }
      break;
    }
  }
  if (!t || !t->detail || std::strncmp(t->name, "FifaOnline::", 12) == 0 ||
      std::strcmp(t->name, "Gameplay::InputContainer") == 0) return;
  std::lock_guard<std::mutex> lock(mu);
  int& n = seen[hash];
  ++n;
  uint32_t p = r30.u32;
  if (!matchev::Readable(p, 0x40)) return;
  const uint32_t clock = sesdbg::U32(p + 0x0C);
  const uint32_t d = p + 0x18;
  auto W = [&](uint32_t off) { return sesdbg::U32(d + off); };
  auto num = [](uint32_t v) { return std::to_string(v); };
  const std::string name = t->name;

  if (name == "GameStarted") {
    in_match = false;
    matchev::NetProbe::Get().Stop();
    return;
  }
  if (name == "Kickoff" && !in_match) {
    in_match = true;
    pro_sent = 0;
    {
      auto& st = matchev::State();
      std::lock_guard<std::mutex> sl(st.mu);
      st.seq = 0;
      st.match = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF);
    }
    {
      std::string tp = tplobby::Json();
      if (!tp.empty()) {
        REXKRNL_INFO("[pro] team play lobby: {}", tp);
        matchev::Report("\"t\":\"tp\",\"users\":" + tp);
      }
    }
    matchev::ScanRoster();
    uint32_t clock_obj = matchev::Readable(r31.u32 + 0x10, 4) ? sesdbg::U32(r31.u32 + 0x10) : 0;
    if (diag::On() && clock_obj && matchev::Readable(clock_obj + 0x44, 4)) matchev::NetProbe::Get().Start(clock_obj);
    return;
  }
  if (!in_match) return;
  auto who = [&](uint32_t idx) { return num(idx) + "=" + matchev::PlayerName(idx); };
  if (name == "Goal") {
    uint32_t idx = W(4);
    REXKRNL_INFO("[match] >> GOAL {:02}:{:02} team {} scorer {}", clock / 60, clock % 60, W(0), who(idx));
    matchev::Report("\"t\":\"goal\",\"clock\":" + num(clock) + ",\"team\":" + num(W(0)) + ",\"idx\":" + num(idx) +
                    ",\"pid\":" + num(matchev::PlayerId(idx)));
  } else if (name == "Foul" && W(0x10) >= 2 && W(0x10) <= 4) {
    uint32_t idx = W(4);
    REXKRNL_INFO("[match] >> CARD {:02}:{:02} team {} player {} card {}", clock / 60, clock % 60, W(0), who(idx), W(0x10));
    matchev::Report("\"t\":\"card\",\"clock\":" + num(clock) + ",\"team\":" + num(W(0)) + ",\"idx\":" + num(idx) +
                    ",\"pid\":" + num(matchev::PlayerId(idx)) + ",\"card\":" + num(W(0x10)));
  } else if (name == "Substitution" || name == "SubstitutionForInjury") {
    uint32_t out = W(4), in = W(8);
    REXKRNL_INFO("[match] >> SUB {:02}:{:02} team {} out {} in {}", clock / 60, clock % 60, W(0), who(out), who(in));
    matchev::Report("\"t\":\"sub\",\"clock\":" + num(clock) + ",\"team\":" + num(W(0)) + ",\"out\":" + num(out) +
                    ",\"in\":" + num(in) + ",\"pout\":" + num(matchev::PlayerId(out)) + ",\"pin\":" +
                    num(matchev::PlayerId(in)) + ",\"injury\":" + (name == "SubstitutionForInjury" ? "1" : "0"));
  } else if (name == "EndHalf") {
    REXKRNL_INFO("[match] >> END OF HALF {} at {:02}:{:02}", W(0), clock / 60, clock % 60);
    matchev::Report("\"t\":\"half\",\"clock\":" + num(clock) + ",\"n\":" + num(W(0)));
  } else if (name == "InjuryTime") {
    matchev::Report("\"t\":\"added\",\"clock\":" + num(clock) + ",\"sec\":" + num(W(0)));
  } else if (name == "GameOver") {
    REXKRNL_INFO("[match] >> FULL TIME at {:02}:{:02}", clock / 60, clock % 60);
    {
      std::string tp = tplobby::Json();  // positions may have changed during the match
      if (!tp.empty()) matchev::Report("\"t\":\"tp\",\"users\":" + tp);
    }
    matchev::Report("\"t\":\"end\",\"clock\":" + num(clock));
    matchev::NetProbe::Get().Stop();
    in_match = false;
  }
}

// ---------------------------------------------------------------------------
// Online sync diagnostics: FifaOnline::SyncFrame is serialised into match
// packets (tag 0x40) by 0x82845d3c and read back by 0x8284606c. Both are hooked
// to log when sync frames go out / come in and what they carry.
namespace syncdbg {
inline void Log(const char* dir, uint32_t a, uint32_t b, uint32_t c) {
  static std::mutex mu;
  static auto t0 = std::chrono::steady_clock::now();
  static int n[2] = {0, 0};
  static int per[2] = {0, 0};
  static auto last = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mu);
  int k = dir[0] == 'o' ? 0 : 1;
  auto now = std::chrono::steady_clock::now();
  ++per[k];
  if (n[k]++ < 400)
    REXKRNL_INFO("[sync] {} SyncFrame at {} ms: {} {} {}", dir,
                 std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count(), a, b, c);
  if (now - last >= std::chrono::seconds(5)) {
    REXKRNL_INFO("[sync] SyncFrame per second: out {:.1f}, in {:.1f}", per[0] / 5.0, per[1] / 5.0);
    per[0] = per[1] = 0;
    last = now;
  }
}
}  // namespace syncdbg

// 0x82845d50: r30 = SyncFrame payload being sent (u32 +0, u16 +4, u8 +6)
void Sync_Send(PPCRegister& r30) {
  uint32_t p = r30.u32;
  if (!matchev::Readable(p, 8)) return;
  syncdbg::Log("out", sesdbg::U32(p), (sesdbg::U32(p + 4) >> 16) & 0xFFFF, sesdbg::U8(p + 6));
}

// 0x82846090: the SyncFrame just read from a packet is at r1+0x78 (u32, u16 +4, u8 +6)
void Sync_Recv(PPCRegister& r1) {
  uint32_t p = r1.u32 + 0x78;
  if (!matchev::Readable(p, 8)) return;
  syncdbg::Log("in ", sesdbg::U32(p), (sesdbg::U32(p + 4) >> 16) & 0xFFFF, sesdbg::U8(p + 6));
}

// 0x82845a00: the online message bus's post (0x828459f0) turns messages into
// match packets. r5 = message type (first word = name hash), r6 = payload,
// r7 = size. Logs the first 300 with time and payload, then counts per type.
void Net_Post(PPCRegister& r5, PPCRegister& r6, PPCRegister& r7) {
  static std::mutex mu;
  static auto t0 = std::chrono::steady_clock::now();
  static auto last = std::chrono::steady_clock::now();
  static std::map<uint32_t, int> per;
  static int n = 0;
  if (!matchev::Readable(r5.u32, 4)) return;
  uint32_t hash = sesdbg::U32(r5.u32);
  std::lock_guard<std::mutex> lock(mu);
  auto now = std::chrono::steady_clock::now();
  ++per[hash];
  if (n++ < 300) {
    uint32_t sz = std::min<uint32_t>(r7.u32, 0x30);
    NOBK_DIAG_INFO("[netmsg] {:08X} size {} at {} ms: {}", hash, r7.u32,
                 std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count(),
                 matchev::Readable(r6.u32, std::max<uint32_t>(sz, 4)) ? matchev::Hex(r6.u32, sz) : std::string());
  }
  if (now - last >= std::chrono::seconds(5)) {
    std::string line;
    for (auto& [h, c] : per) {
      char b[40];
      std::snprintf(b, sizeof(b), " %08X:%.1f/s", h, c / 5.0);
      line += b;
    }
    NOBK_DIAG_INFO("[netmsg] rates{}", line);
    per.clear();
    last = now;
  }
}

// 0x82853340: online session update (0x82853328), r31 = session. Once per call.
void NetDbg_Update(PPCRegister& r31) { netdbg::Push('U', 0); }

// 0x82853410: r29 = result of 0x82840b90; > 1 runs the send / receive loop.
void NetDbg_UpdateLoop(PPCRegister& r29) { netdbg::Push('L', r29.u32); }

// 0x8284bbe0: packet receive loop (0x8284bbc0) entered with a socket.
void NetDbg_RecvLoop(PPCRegister& r31) { netdbg::Push('r', 0); }

// 0x8284bc0c: r27 = packets read (> 0) in this receive loop call.
void NetDbg_RecvPackets(PPCRegister& r27) { netdbg::Push('R', r27.u32); }

// DirtySock socket callbacks of the CommUDP game socket (proc 0x82875470).
// 0x8287e610: fired because the socket thread saw data (r11 = proc).
void NetDbg_CbRecv(PPCRegister& r11) { if (r11.u32 == 0x82875470) netdbg::Push('C', 0); }
// 0x8287e540: fired by the idle timer (r9 = proc).
void NetDbg_CbIdle(PPCRegister& r9) { if (r9.u32 == 0x82875470) netdbg::Push('I', 0); }

// CommUDP registers its socket callback with a 100 ms idle period
// (SocketCallback(sock, CALLB_RECV, 100, ...) at 0x8287582c / 0x8287618c).
// Test: run it every few ms instead. NOBK_COMMUDP_IDLE=<ms> overrides
// (100 = the game's original value).
void Online_CommUdpIdleRate(PPCRegister& r5) {
  g_online_match = true;
  static const uint32_t ms = [] {
    const char* e = std::getenv("NOBK_COMMUDP_IDLE");
    int v = e ? std::atoi(e) : 5;
    return uint32_t(v > 0 && v <= 1000 ? v : 5);
  }();
  REXKRNL_INFO("[online] CommUDP idle period {} ms -> {} ms", r5.u32, ms);
  r5.u64 = ms;
}
void Online_CommUdpIdleRate2(PPCRegister& r5) { Online_CommUdpIdleRate(r5); }

// Online half length. The match clock turns ticks into game time as
//   game seconds = period start + ticks / 60 * 45 / clock[+0x80]
// (0x82677550), where clock+0x80 is the half length in real minutes (float).
// Online matches use 5; this hook makes them NOBK_ONLINE_HALF minutes (default 4,
// 0 = off). It rewrites the field in memory right before the game reads it, so
// every reader (match clock, half time, added time) sees the same value. Both
// players run this build, so the lock-step simulation stays identical.
void Online_HalfLength(PPCRegister& r31) {
  if (!g_online_match) return;
  static const float want = [] {
    const char* e = std::getenv("NOBK_ONLINE_HALF");
    int v = e ? std::atoi(e) : 4;
    return float(v >= 1 && v <= 20 ? v : 0);
  }();
  if (want == 0.0f) return;
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r31.u32 + 0x80);
  if (!p) return;
  uint32_t bits = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  float cur;
  std::memcpy(&cur, &bits, 4);
  if (cur != 5.0f) return;  // only the online default; other lengths stay as chosen
  std::memcpy(&bits, &want, 4);
  p[0] = uint8_t(bits >> 24); p[1] = uint8_t(bits >> 16); p[2] = uint8_t(bits >> 8); p[3] = uint8_t(bits);
  static std::atomic<int> logged{0};
  if (logged++ < 4) REXKRNL_INFO("[online] half length 5 -> {:.0f} minutes (clock {:08X})", want, r31.u32);
}
// The other readers of clock+0x80 (game-time scale 0x82676430, 0x82678b34).
void Online_HalfLength2(PPCRegister& r31) { Online_HalfLength(r31); }
void Online_HalfLength3(PPCRegister& r3) { Online_HalfLength(r3); }

// --- Career crash guard: RenderWare MemoryFramework heap (sub_8235D818) -----------
// 18 Oct: crash at the first career match, "write of guest 0x19AF19BA". The allocator
// takes the first chunk of its unsorted list (r30 = list head, r4 = chunk) and reads
// chunk->fd into r10 (lwz r10,8(r4) at 0x8235d9a4). That fd was 0x19AF19AE: something
// wrote into a freed chunk (use after free), and the next "stw r30,12(r10)" crashed.
// Valid chunks are 8-byte aligned, so a misaligned / unmapped fd is corrupt: cut the
// list there (fd = list head) so the allocator goes on; the damaged chunk is dropped
// (a small leak). Logs the chunk so the writer can be found later.
void Heap_UnsortedFdGuard(PPCRegister& r4, PPCRegister& r10, PPCRegister& r29, PPCRegister& r30) {
  uint32_t fd = r10.u32;
  if (fd == r30.u32) return;
  if ((fd & 7) == 0 && fd >= 0x10000 && sesdbg::Ptr(fd + 12)) return;
  static std::atomic<int> n{0};
  uint32_t c = r4.u32;
  if (n.fetch_add(1) < 20) {
    REXKRNL_WARN("[heapguard] heap {:08X}: unsorted chunk {:08X} has bad fd {:08X} -> list cut. "
                 "chunk words: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                 r29.u32, c, fd, sesdbg::U32(c), sesdbg::U32(c + 4), sesdbg::U32(c + 8),
                 sesdbg::U32(c + 12), sesdbg::U32(c + 16), sesdbg::U32(c + 20), sesdbg::U32(c + 24),
                 sesdbg::U32(c + 28));
  }
  r10.u64 = r30.u32;
}

// --- Team play 3rd player: OSDK heap double-free detector ----------------------------
// 18 Oct 23:48: operator new (sub_82C56FD8) got 0x45580000 back from its allocator, i.e.
// "EX\0\0" of the string "OCALEX" was taken as a free-list link: a block that holds the
// name is still on the free list (double free or write after free). These two probes
// keep a set of blocks freed through the OSDK delete (sub_82C57040) and drop a block
// when operator new hands it out again. A second free of a block in the set is logged
// with the guest call chain: that is the bug.
namespace tpheap {
inline std::mutex mu;
inline std::unordered_set<uint32_t> freed;
}
void Tp_OsdkNewResult(PPCRegister& r3) {
  if (!r3.u32) return;
  std::lock_guard<std::mutex> l(tpheap::mu);
  tpheap::freed.erase(r3.u32);
}
void Tp_OsdkDelete(PPCRegister& r3, PPCRegister& r1) {
  const uint32_t blk = r3.u32 - 4;
  bool dup;
  {
    std::lock_guard<std::mutex> l(tpheap::mu);
    dup = !tpheap::freed.insert(blk).second;
    if (tpheap::freed.size() > 2000000) tpheap::freed.clear();
  }
  if (!dup) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) >= 20) return;
  REXKRNL_WARN("[heapguard] OSDK DOUBLE FREE of {:08X} (block {:08X}) text='{}' stack:{}", r3.u32, blk,
               sesdbg::Str(r3.u32, 24), sesdbg::Chain(r1.u32, 14));
}

// AddRef (0x82CA5B44, r4 = object, refcount word still old) / Release (0x82C80C38,
// r7 = object) / Release reached zero -> destroy (0x82C80C74) for watched objects.
void Tp_RefAdd(PPCRegister& r4, PPCRegister& r1) {
  if (!r4.u32 || !tpref::Is(r4.u32)) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) >= 300) return;
  NOBK_DIAG_INFO("[tpref] AddRef  {:08X} refs {} -> +1 stack:{}", r4.u32, sesdbg::U32(r4.u32 + 4) >> 22,
               sesdbg::Chain(r1.u32, 6));
}
void Tp_RefRelease(PPCRegister& r7, PPCRegister& r1) {
  if (!tpref::Is(r7.u32)) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) >= 300) return;
  NOBK_DIAG_INFO("[tpref] Release {:08X} refs {} -> -1 stack:{}", r7.u32, sesdbg::U32(r7.u32 + 4) >> 22,
               sesdbg::Chain(r1.u32, 6));
}
// 19 Oct, 3-player lobby: when a member's connection drops, ConnApi status 6 ->
// RemoveStreamedPeerConnection -> the PeerConnection's destructor (0x82CEEF30) releases
// its GameUser. By then the game session's user list has already dropped its
// reference, so the count reaches 0 and the GameUser is deleted while the lobby code
// still uses it a moment later: "read of guest 0x00000201" on the host AND on the other
// joiner (both crashed the same way, 1 s apart). Keep that GameUser alive instead
// (jump past the deleting destructor, toml jump_address_on_true = 0x82c80c8c). It is a
// small object and only leaks once per dropped member. NOBK_TP_KEEPUSER=0 turns it off.
bool Tp_RefDestroy(PPCRegister& r7, PPCRegister& r1) {
  if (!tpref::Is(r7.u32)) return false;
  static const bool keep = [] { const char* e = std::getenv("NOBK_TP_KEEPUSER"); return !(e && *e == '0'); }();
  bool from_peer = false;
  uint32_t sp = r1.u32;
  for (int i = 0; i < 3; ++i) {
    uint32_t next = sesdbg::U32(sp);
    if (next == 0 || next == 0xDEADBEEF || next <= sp || next - sp > 0x10000) break;
    if (sesdbg::U32(next - 8) == 0x82CEEF30) { from_peer = true; break; }
    sp = next;
  }
  if (keep && from_peer) {
    REXKRNL_WARN("[tpref] KEEP {:08X}: refs reached 0 in PeerConnection teardown, not deleted", r7.u32);
    return true;
  }
  NOBK_DIAG_WARN("[tpref] DESTROY {:08X} (refs reached 0) stack:{}", r7.u32, sesdbg::Chain(r1.u32, 12));
  return false;
}
void Tp_RefAdd2(PPCRegister& r4, PPCRegister& r1) { Tp_RefAdd(r4, r1); }

// Pool allocator behind the lobby's per-user list (sub_82CE5188 allocates 12-byte entries
// from object+48; the 3rd player crash gets 0x45580000 = "EX\0\0" back from it).
// Log the allocator, its vtable and its first words on 16-byte requests, and each
// list entry block as it is freed (0x82CE5A00: r3 = allocator, r4 = block).
inline std::string TpWords(uint32_t a, int n) {
  std::string s;
  for (int i = 0; i < n; ++i) { char b[12]; std::snprintf(b, sizeof(b), " %08X", sesdbg::U32(a + 4 * i)); s += b; }
  return s;
}
void Tp_PoolNew(PPCRegister& r31, PPCRegister& r4, PPCRegister& r1) {
  if (r4.u32 != 16) return;
  const uint32_t ret = sesdbg::U32(sesdbg::U32(r1.u32) - 8);  // return address into the caller
  if (ret < 0x82CE5188 || ret >= 0x82CE5318) return;           // only the lobby user-list rebuild
  static std::atomic<int> n{0};
  if (n.fetch_add(1) >= 60) return;
  NOBK_DIAG_INFO("[tppool] new 12 alloc={:08X} vt={:08X} fn0={:08X} obj:{} caller:{}", r31.u32,
               sesdbg::U32(r31.u32), TpVfn(r31.u32, 0), TpWords(r31.u32, 8), sesdbg::Chain(r1.u32, 3));
}
void Tp_PoolEntryFree(PPCRegister& r3, PPCRegister& r4) {
  static std::atomic<int> n{0};
  if (n.fetch_add(1) >= 60) return;
  NOBK_DIAG_INFO("[tppool] free entry block={:08X} alloc={:08X} fn4={:08X} block:{} alloc:{}", r4.u32, r3.u32,
               TpVfn(r3.u32, 4), TpWords(r4.u32, 4), TpWords(r3.u32, 8));
}

// --- Team play 3rd player crash: OSDK "max players" = 2 -----------------------------
// FOUND 19 Oct: the lobby's per-user lists live in fixed pools (vtable 821B20B0, tag
// 'mdef') whose size is OSDK config +184 (OSDK_MAXPLAYERS) = 2. The pool pops without a
// bounds check, so the 3rd user got garbage ("EX\0\0" of "OCALEX", 0x4, "mdef" in
// Snow's crash). The same value sizes the peer ConnApi (max clients) and other lists.
// Raise it where it is read as a capacity (not in the game's own UI code):
//   0x82ce4624/465c  user list vector + 12-byte pool (sub_82CE45E0, GameSetup+148 and UI)
//   0x82ce4ec4       GameSetup vector at +108
//   0x82cbe0a8, 0x82cbe880, 0x82cef040  OSDK peer/user arrays
//   0x82d11800       PeerConnectionManager ConnApiCreate max clients
// NOBK_TP_MAXPLAYERS=<n> overrides (default 24, 0 = off).
inline void TpRaiseMax(PPCRegister& r, const char* site) {
  NetActivity();
  static const uint32_t want = [] {
    const char* e = std::getenv("NOBK_TP_MAXPLAYERS");
    int v = e ? std::atoi(e) : 24;
    return uint32_t(v > 0 && v <= 64 ? v : 0);
  }();
  if (!want || r.u32 == 0 || r.u32 >= want) return;
  static std::atomic<int> n{0};
  if (n.fetch_add(1) < 16) REXKRNL_INFO("[tp] OSDK max players {} -> {} at {}", r.u32, want, site);
  r.u64 = want;
}
void Tp_MaxPlayersA(PPCRegister& r4) { TpRaiseMax(r4, "82CE4624 user list"); }
void Tp_MaxPlayersB(PPCRegister& r5) { TpRaiseMax(r5, "82CE465C user pool"); }
void Tp_MaxPlayersC(PPCRegister& r4) { TpRaiseMax(r4, "82CE4EC4 GameSetup list"); }
void Tp_MaxPlayersD(PPCRegister& r29) { TpRaiseMax(r29, "82CBE0A8"); }
void Tp_MaxPlayersE(PPCRegister& r27) { TpRaiseMax(r27, "82CBE880"); }
void Tp_MaxPlayersF(PPCRegister& r28) { TpRaiseMax(r28, "82CEF040"); }
void Tp_MaxPlayersG(PPCRegister& r28) { TpRaiseMax(r28, "82D11800 peer ConnApi"); }

// 19 Oct: with the peer ConnApi at 24 clients the game trapped at boot. ConnApi
// (sub_828794E8) starts voice with VoipStartup(max clients = ConnApi+588) at
// 0x82879578, and the XHV engine (XHVCreateEngine, 0x8309B998) cannot get memory for
// 24 remote talkers from its fixed heap (allocator 0x823156F8 traps on failure).
// Keep voice at its original size; ConnApi itself stays at 24. NOBK_TP_VOIP=<n>
// (default 2).
void Tp_VoipMax(PPCRegister& r3) {
  static const uint32_t cap = [] {
    const char* e = std::getenv("NOBK_TP_VOIP");
    int v = e ? std::atoi(e) : 10;   // 10 (19 Oct test: boots fine, voice to 3+ remote players)
    return uint32_t(v >= 1 && v <= 32 ? v : 10);
  }();
  if (r3.u32 <= cap) return;
  REXKRNL_INFO("[tp] voice startup max talkers {} -> {}", r3.u32, cap);
  r3.u64 = cap;
}


// --- Quick match search length -----------------------------------------------------
// OSDK reads its lobby settings at LoginStageLobbyComplete (sub_82C79748). The
// struct defaults (sub_82C79648) set +52 OSDK_MATCHUP_TIMEOUT = 20000 ms, and the
// server does not send that key, so a quick match search (gqwk MODE=2) gives up and
// cancels itself (MODE=3) after 20 s. Hook right after the key is read (0x82c79808,
// r30 = settings) and raise it. NOBK_MATCHUP_SEC=<s> overrides (default 120, 0 = off).
void Online_MatchupTimeout(PPCRegister& r30) {
  static const uint32_t want = [] {
    const char* e = std::getenv("NOBK_MATCHUP_SEC");
    int v = e ? std::atoi(e) : 120;
    return uint32_t(v > 0 && v <= 3600 ? v * 1000 : 0);
  }();
  if (!want) return;
  uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r30.u32 + 52);
  if (!p) return;
  uint32_t cur = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  if (cur >= want) return;
  p[0] = uint8_t(want >> 24); p[1] = uint8_t(want >> 16); p[2] = uint8_t(want >> 8); p[3] = uint8_t(want);
  REXKRNL_INFO("[online] quick match timeout {} ms -> {} ms", cur, want);
}

// --- Main menu "Quit to Desktop" (was NEWS AND RECENT OFFERS) ----------------------
// The menu text comes from mods\nobk_text\locale.big (tools/py/locpatch.py: "News and
// Recent Offers" and the offline label "Sign in to EA" -> "Quit to Desktop").
// The front end is scripted; script code calls natives registered by name (register
// 0x82D3D280: r4 = name, r5 = native object; the VM calls them at 0x82D28394 with r5 =
// native object). Pressing the item (traced offline and online, 2026-10-19):
//   NotifyMenu 0 -> NotifyScreenChange(-1, ...) -> ~1.2 s later the news screen calls
//   IsLoggedIntoEA and right after it StartLogin.
// NOBK.ONLINE opens the online zone without a screen change or StartLogin, so the quit
// fires on StartLogin only when: the main menu zone is active (last
// NotifyMenuSelectionChange arg0 == 0), the last NotifyMenu was 0, a screen change
// happened in the last 4 s and IsLoggedIntoEA was called just before. It closes the
// window (WM_CLOSE, the same clean shutdown as Alt+F4).
// NOBK_QUIT_MENU_OFF=1 disables it; NOBK_UI_TRACE=1 logs natives and menu notifications.
namespace uiquit {
inline std::mutex mu;
inline std::unordered_map<uint32_t, std::string> names;  // native object -> name (trace only)
inline std::atomic<uint32_t> obj_islogged{0}, obj_startlogin{0}, obj_showinvite{0};
// Play a Friend runs the same script start as the news item (NotifyMenu 0, screen -1 from 12,
// IsLoggedIntoEA, StartLogin) and ~0.2 s later calls ShowGameInvite. So the quit waits
// QUIT_WAIT_MS after StartLogin and is cancelled by ShowGameInvite.
inline std::atomic<int64_t> armed_at{0};
constexpr int64_t QUIT_WAIT_MS = 900;
inline std::atomic<int32_t> zone{-99}, menu{-99};
// Screen ids from NotifyScreenChange: 12 = main menu; the news/quit item opens -1 from 12.
inline std::atomic<int32_t> screen{-99}, prev_screen{-99};
inline std::atomic<uint32_t> screen_key{0}, prev_key{0};
inline std::atomic<int64_t> t_screen{0}, t_islogged{0};
inline int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline bool Trace() {
  static const bool on = [] { const char* e = std::getenv("NOBK_UI_TRACE"); return e && *e && *e != '0'; }();
  return on;
}
inline bool Off() {
  // Off by default since 1.2.4: the guess also fired on Be A Pro lobbies after a match.
  // NOBK_QUIT_MENU=1 turns it back on.
  static const bool off = [] { const char* e = std::getenv("NOBK_QUIT_MENU"); return !(e && *e == '1'); }();
  return off;
}
inline void CloseGameWindow() {
#if defined(_WIN32)
  static std::atomic<bool> once{false};
  if (once.exchange(true)) return;
  REXKRNL_INFO("[ui] Quit to Desktop: closing the game window");
  EnumWindows([](HWND h, LPARAM) -> BOOL {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h)) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
  }, 0);
#endif
}
}  // namespace uiquit

void Ui_RegisterNative(PPCRegister& r4, PPCRegister& r5) {
  std::string n = sesdbg::Str(r4.u32, 64);
  if (n == "IsLoggedIntoEA") uiquit::obj_islogged = r5.u32;
  else if (n == "StartLogin") uiquit::obj_startlogin = r5.u32;
  else if (n == "ShowGameInvite") uiquit::obj_showinvite = r5.u32;
  if (!uiquit::Trace()) return;
  std::lock_guard<std::mutex> l(uiquit::mu);
  uiquit::names[r5.u32] = n;
}

void Ui_CallNative(PPCRegister& r5) {
  const uint32_t o = r5.u32;
  if (o && o == uiquit::obj_islogged.load(std::memory_order_relaxed)) {
    uiquit::t_islogged = uiquit::NowMs();
  } else if (o && o == uiquit::obj_startlogin.load(std::memory_order_relaxed) && !uiquit::Off()) {
    const int64_t now = uiquit::NowMs();
    // Only from the main menu screen (12); Play a Friend is told apart by ShowGameInvite (below).
    const bool ok = uiquit::prev_screen == 12 && uiquit::screen == -1 &&
                    uiquit::zone == 0 && uiquit::menu == 0 && now - uiquit::t_screen < 4000 &&
                    now - uiquit::t_islogged < 250;
    REXKRNL_INFO("[ui] StartLogin (zone {} menu {} screen {} from {}, {} ms ago) -> {}", int32_t(uiquit::zone),
                 int32_t(uiquit::menu), int32_t(uiquit::screen), int32_t(uiquit::prev_screen), now - uiquit::t_screen,
                 ok ? "Quit to Desktop unless ShowGameInvite follows" : "keep");
    if (ok) {
      uiquit::armed_at = now;
      std::thread([now] {
        std::this_thread::sleep_for(std::chrono::milliseconds(uiquit::QUIT_WAIT_MS));
        if (uiquit::armed_at.load() != now) return;
        if (g_sess_change_ms.load() >= now - 20) {  // a session is being created: online room
          uiquit::armed_at = 0;
          REXKRNL_INFO("[ui] online session activity after StartLogin: lobby / room, quit cancelled");
          return;
        }
        uiquit::CloseGameWindow();
      }).detach();
    }
  } else if (o && o == uiquit::obj_showinvite.load(std::memory_order_relaxed)) {
    if (uiquit::armed_at.exchange(0)) REXKRNL_INFO("[ui] ShowGameInvite: Play a Friend, quit cancelled");
  }
  if (!uiquit::Trace()) return;
  std::string n;
  {
    std::lock_guard<std::mutex> l(uiquit::mu);
    auto it = uiquit::names.find(o);
    if (it == uiquit::names.end()) return;
    n = it->second;
  }
  static std::mutex cmu;
  static std::unordered_map<std::string, int> calls;
  std::lock_guard<std::mutex> cl(cmu);
  static const char* noisy[] = {"QuadraticEase", "QuadraticEaseIn", "QuadraticEaseOut", "LocalizeString",
                                "IsFCCEnable", "IsAnyPassivelyLoggedIntoEA", "NotifyActiveZoneTransition",
                                "PlaySound", "IsOnlineTeamPlayDisabled", "GetZPosition", "IsWideScreen"};
  bool quiet = false;
  for (const char* q : noisy) if (n == q) quiet = true;
  if (calls[n]++ < (quiet ? 3 : 400)) REXKRNL_INFO("[ui] native {} (fn {:08X})", n, sesdbg::U32(o + 32));
}

// Menu notifications from the front-end scripts (their string args are not plain text):
// NotifyMenu(int, str) at 0x831C9DFC: r3 = arg0.
// NotifyScreenChange(int, str) at 0x831C9D08: r3 = arg0 (12 = main menu, -1 = news).
// NotifyMenuSelectionChange(int, int, x, str) at 0x831C9E8C: r3 = arg0 (zone: 0 main menu,
// 1 online), r29 = arg1, r30 = arg2.
void Ui_NotifyMenu(PPCRegister& r3, PPCRegister& r31) {
  (void)r31;
  uiquit::menu = int32_t(r3.u32);
  if (uiquit::Trace()) REXKRNL_INFO("[ui] NotifyMenu {}", int32_t(r3.u32));
}
void Ui_NotifyScreen(PPCRegister& r3, PPCRegister& r31) {
  (void)r31;
  uiquit::t_screen = uiquit::NowMs();
  uiquit::prev_screen = uiquit::screen.load();
  uiquit::screen = int32_t(r3.u32);
  {
    // arg1 is a script string object (+4 -> data); its first bytes look like a hashed screen
    // name: logged so the news screen and Play a Friend can be told apart.
    const uint32_t d = sesdbg::U32(r31.u32 + 4);
    uiquit::screen_key = sesdbg::U32(d);
    if (uiquit::Trace()) REXKRNL_INFO("[ui] screen {} key {:08X} {:08X} (from {} key {:08X})", int32_t(r3.u32), sesdbg::U32(d),
                 sesdbg::U32(d + 4), int32_t(uiquit::prev_screen), uint32_t(uiquit::prev_key));
    uiquit::prev_key = uiquit::screen_key.load();
  }
}
void Ui_NotifySelection(PPCRegister& r3, PPCRegister& r29, PPCRegister& r30, PPCRegister& r31) {
  (void)r31;
  uiquit::zone = int32_t(r3.u32);
  if (uiquit::Trace())
    REXKRNL_INFO("[ui] NotifyMenuSelectionChange {} {} {}", int32_t(r3.u32), int32_t(r29.u32), int32_t(r30.u32));
}

// --- Font heap out of memory (Be A Pro crash, 20 Oct) -------------------------------
// After a goal in a 2-player Be A Pro match FontSystem::InitializeInstance (0x82926078)
// asked FontFusion for 148 bytes. FontFusion's allocator (sub_82425CD0, category
// "FontFusion" / TLFontRendering) got NULL from its heap (the heap traps once: "trap
// type 22") and wrote its header to NULL+4 -> crash. When that heap is full we give the
// block from the kernel system heap instead, and the matching free (sub_82425C78)
// returns those blocks to the system heap. NOBK_FONT_OOM_OFF=1 disables it.
namespace fontoom {
inline std::mutex mu;
inline std::unordered_set<uint32_t> blocks;   // header addresses we handed out
inline std::atomic<uint32_t> count{0}, bytes{0};
inline bool Off() {
  static const bool off = [] { const char* e = std::getenv("NOBK_FONT_OOM_OFF"); return e && *e == '1'; }();
  return off;
}
}  // namespace fontoom

// 0x82425d50: right after the allocator call; r1+96 = returned header, r31 = user size.
void Font_AllocFallback(PPCRegister& r1, PPCRegister& r31) {
  if (fontoom::Off()) return;
  uint8_t* slot = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(r1.u32 + 96);
  if (!slot) return;
  uint32_t got = (uint32_t(slot[0]) << 24) | (uint32_t(slot[1]) << 16) | (uint32_t(slot[2]) << 8) | slot[3];
  if (got) return;
  uint32_t want = r31.u32 + 12;
  if (want < 16 || want > 64u * 1024 * 1024) return;
  uint32_t a = REX_KERNEL_MEMORY()->SystemHeapAlloc(want, 0x20);
  if (!a) {
    REXKRNL_ERROR("[fontoom] fallback alloc of {} bytes failed too", want);
    return;
  }
  if (uint8_t* p = REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(a)) std::memset(p, 0, want);
  { std::lock_guard<std::mutex> l(fontoom::mu); fontoom::blocks.insert(a); }
  slot[0] = uint8_t(a >> 24); slot[1] = uint8_t(a >> 16); slot[2] = uint8_t(a >> 8); slot[3] = uint8_t(a);
  uint32_t n = ++fontoom::count;
  fontoom::bytes += want;
  if (n <= 10 || (n % 200) == 0)
    REXKRNL_WARN("[fontoom] font heap full: {} bytes from the system heap at {:08X} (#{} , {} KB total)",
                 want, a, n, fontoom::bytes.load() / 1024);
}

// 0x82425cac: free path, r31 = block header. Our blocks go back to the system heap and
// skip the game's free (jump 0x82425cb8).
bool Font_FreeFallback(PPCRegister& r31) {
  if (fontoom::count.load() == 0) return false;
  uint32_t h = r31.u32;
  {
    std::lock_guard<std::mutex> l(fontoom::mu);
    auto it = fontoom::blocks.find(h);
    if (it == fontoom::blocks.end()) return false;
    fontoom::blocks.erase(it);
  }
  REX_KERNEL_MEMORY()->SystemHeapFree(h);
  return true;
}
