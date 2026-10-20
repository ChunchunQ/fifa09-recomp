# Adds the P2P part (online-revival-26) to the SDK's xam_net.cpp by text anchors,
# for SDK copies where the .patch file does not apply. Then build with:
#   .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$file = Join-Path $Sdk "src\kernel\xam\xam_net.cpp"
$raw = [IO.File]::ReadAllText($file)
$crlf = $raw.Contains("`r`n")
$s = $raw.Replace("`r`n", "`n")
if ($s.Contains("[online] Direct match traffic (P2P)")) { Write-Host "P2P is already in xam_net.cpp" -ForegroundColor Yellow; exit 0 }

$block1 = @'
// ---------------------------------------------------------------------------
// [online] Direct match traffic (P2P) next to the relay.
// Through the relay every packet makes two trips (player -> server -> player);
// FIFA 09's lock-step simulation then waits for the other side every ~100 ms
// and the match stutters. If BOTH players allow it (account setting on the
// website; the server decides), the server tells each game where the other
// one's game sockets are (public address, plus the LAN address when both are
// behind the same router). Packets keep the relay header and are sent straight
// to those addresses from the same socket (that also opens the NAT); as soon as
// a packet from the peer arrives directly, the relay is no longer used for that
// peer port. No direct packet for 1.5 s -> back to the relay.
// Control channel: a small UDP socket of its own talks to the relay
//   hello (type 8): {"lan":"a.b.c.d"} every 2 s
//   peers (type 7): text, "P2P1" then one line per peer game port:
//                   "<port> <ip>:<port> [<ip>:<port>]"  (empty = relay only)
struct P2PPath {
  std::vector<sockaddr_in> cands;   // where the peer's socket for this port may be reached
  sockaddr_in confirmed = {};       // address direct packets come from (0 = none)
  std::chrono::steady_clock::time_point last_rx{};
};
static std::mutex g_p2p_mutex;
static std::map<uint16_t, P2PPath> g_p2p;  // peer game port -> path
static std::atomic<bool> g_p2p_started{false};

static std::string Ipv4Text(uint32_t nbo) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&nbo);
  char t[20];
  std::snprintf(t, sizeof(t), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return t;
}

static void P2PApply(const std::string& text) {
  std::map<uint16_t, P2PPath> next;
  size_t pos = 0;
  bool first = true;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    pos = eol == std::string::npos ? text.size() : eol + 1;
    if (first) {
      first = false;
      if (line != "P2P1") return;
      continue;
    }
    unsigned port = 0;
    int used = 0;
    if (std::sscanf(line.c_str(), "%u%n", &port, &used) != 1 || !port || port > 65535) continue;
    P2PPath path;
    const char* c = line.c_str() + used;
    unsigned a, b, d, e, pp;
    int n2 = 0;
    while (std::sscanf(c, " %u.%u.%u.%u:%u%n", &a, &b, &d, &e, &pp, &n2) == 5) {
      c += n2;
      if (a > 255 || b > 255 || d > 255 || e > 255 || !pp || pp > 65535) continue;
      sockaddr_in sa = {};
      sa.sin_family = AF_INET;
      uint8_t ip[4] = {uint8_t(a), uint8_t(b), uint8_t(d), uint8_t(e)};
      std::memcpy(&sa.sin_addr.s_addr, ip, 4);
      sa.sin_port = htons(uint16_t(pp));
      path.cands.push_back(sa);
    }
    if (!path.cands.empty()) next[uint16_t(port)] = path;
  }
  std::lock_guard<std::mutex> lock(g_p2p_mutex);
  // keep a confirmed path if the peer address is still offered
  for (auto& [port, path] : next) {
    auto it = g_p2p.find(port);
    if (it != g_p2p.end() && it->second.confirmed.sin_port) {
      path.confirmed = it->second.confirmed;
      path.last_rx = it->second.last_rx;
    }
  }
  bool changed = next.size() != g_p2p.size();
  for (auto& [port, path] : next) {
    auto it = g_p2p.find(port);
    if (it == g_p2p.end() || it->second.cands.size() != path.cands.size()) changed = true;
  }
  g_p2p.swap(next);
  if (changed) {
    REXKRNL_INFO("[p2p] {} peer port(s) offered for direct traffic", g_p2p.size());
    for (auto& [port, path] : g_p2p)
      for (auto& sa : path.cands)
        REXKRNL_INFO("[p2p]   peer port {} -> {}:{}", port, Ipv4Text(sa.sin_addr.s_addr), ntohs(sa.sin_port));
  }
}

static void P2PControlThread(uint32_t relay_nbo, uint16_t relay_port, uint32_t vip) {
#if REX_PLATFORM_WIN32
  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    REXKRNL_WARN("[p2p] no control socket; relay only");
    return;
  }
  DWORD tmo = 500;
  ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
  sockaddr_in relay = {};
  relay.sin_family = AF_INET;
  relay.sin_addr.s_addr = relay_nbo;
  relay.sin_port = htons(relay_port);
  auto last_hello = std::chrono::steady_clock::now() - std::chrono::seconds(10);
  for (;;) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_hello >= std::chrono::seconds(2)) {
      last_hello = now;
      std::string body = "{\"lan\":\"" + Ipv4Text(CachedLocalIPv4NBO()) + "\"}";
      std::vector<uint8_t> pkt(kRelayHdr + body.size(), 0);
      pkt[0] = 'R'; pkt[1] = 'L'; pkt[2] = 1; pkt[3] = 8;
      std::memcpy(&pkt[4], &vip, 4);
      std::memcpy(&pkt[kRelayHdr], body.data(), body.size());
      ::sendto(s, reinterpret_cast<const char*>(pkt.data()), int(pkt.size()), 0,
               reinterpret_cast<sockaddr*>(&relay), sizeof(relay));
    }
    char buf[2048];
    sockaddr_in from = {};
    int flen = sizeof(from);
    int n = ::recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &flen);
    if (n < int(kRelayHdr) || from.sin_addr.s_addr != relay_nbo) continue;
    if (buf[0] != 'R' || buf[1] != 'L' || buf[2] != 1 || buf[3] != 7) continue;
    P2PApply(std::string(buf + kRelayHdr, size_t(n) - kRelayHdr));
  }
#else
  (void)relay_nbo; (void)relay_port; (void)vip;
#endif
}

static void P2PStart(uint32_t relay_nbo, uint16_t relay_port, uint32_t vip) {
  if (g_p2p_started.exchange(true)) return;
  std::thread([=] { P2PControlThread(relay_nbo, relay_port, vip); }).detach();
}

// Direct destinations for a packet to peer port `port`: the confirmed address,
// or every candidate while none is confirmed. *also_relay: send through the
// relay as well (until the direct path is confirmed).
static std::vector<sockaddr_in> P2PTargets(uint16_t port, bool* also_relay) {
  *also_relay = true;
  std::lock_guard<std::mutex> lock(g_p2p_mutex);
  auto it = g_p2p.find(port);
  if (it == g_p2p.end()) return {};
  P2PPath& p = it->second;
  auto now = std::chrono::steady_clock::now();
  if (p.confirmed.sin_port) {
    if (now - p.last_rx < std::chrono::milliseconds(1500)) {
      *also_relay = false;
      return {p.confirmed};
    }
    REXKRNL_INFO("[p2p] direct path to peer port {} lost, back to the relay", port);
    p.confirmed = {};
  }
  return p.cands;
}

// A relay-format packet that came straight from the peer: accepted only from
// an address the server offered (same IP; the port may differ behind some NATs).
static bool P2PAccept(const sockaddr_in& src, uint16_t peer_port) {
  std::lock_guard<std::mutex> lock(g_p2p_mutex);
  for (auto& [port, p] : g_p2p) {
    for (auto& c : p.cands) {
      if (c.sin_addr.s_addr != src.sin_addr.s_addr) continue;
      if (port != peer_port && c.sin_port != src.sin_port) continue;
      if (!p.confirmed.sin_port || p.confirmed.sin_addr.s_addr != src.sin_addr.s_addr ||
          p.confirmed.sin_port != src.sin_port) {
        p.confirmed = src;
        REXKRNL_INFO("[p2p] direct path to peer port {} open via {}:{}", port, Ipv4Text(src.sin_addr.s_addr),
                     ntohs(src.sin_port));
      }
      p.last_rx = std::chrono::steady_clock::now();
      return true;
    }
  }
  return false;
}

'@
$block2 = @'
  P2PStart(r.addr_nbo, r.port, vip);
  bool also_relay = true;
  if (type == 1 && to) {
    for (const sockaddr_in& d : P2PTargets(RawPort(&to->sin_port), &also_relay)) {
      N_XSOCKADDR_IN direct;
      direct.sin_family = AF_INET;
      std::memcpy(&direct.sin_addr, &d.sin_addr.s_addr, 4);
      std::memcpy(&direct.sin_port, &d.sin_port, 2);
      std::memset(direct.x_sin_zero, 0, sizeof(direct.x_sin_zero));
      int dret = sock->SendTo(pkt.data(), uint32_t(pkt.size()), flags, &direct, 16);
      LogSendOnce("p2p", &direct.sin_addr, &direct.sin_port, dret);
    }
    if (!also_relay) return int(len);
  }
'@
$block1 = $block1.Replace("`r`n", "`n") + "`n`n"
$block2 = $block2.Replace("`r`n", "`n") + "`n"

# 1) P2P helpers right before RelaySend
$i = $s.IndexOf("static int RelaySend(")
if ($i -lt 0) { throw "anchor 1 (static int RelaySend) not found" }
$lineStart = $s.LastIndexOf("`n// Sends", $i)
if ($lineStart -lt 0 -or $i - $lineStart -gt 200) { $lineStart = $s.LastIndexOf("`n", $i) }
$s = $s.Substring(0, $lineStart + 1) + $block1 + $s.Substring($lineStart + 1)

# 2) direct sending inside RelaySend, after the payload copy
$a2 = "  if (len) std::memcpy(&pkt[kRelayHdr], data, len);`n"
$j = $s.IndexOf($a2)
if ($j -lt 0) { throw "anchor 2 (payload copy in RelaySend) not found" }
$s = $s.Substring(0, $j + $a2.Length) + $block2 + $s.Substring($j + $a2.Length)

# 3) RelayUnwrap: also accept packets straight from the offered peer addresses
$src = "  if (src.sin_addr.s_addr != r.addr_nbo || ntohs(src.sin_port) != r.port) return 0;`n"
$hdr = "  if (data[0] != 'R' || data[1] != 'L' || data[2] != 1 || data[3] != 1) return 0;`n"
if ($s.IndexOf($src) -lt 0) { throw "anchor 3 (relay source check in RelayUnwrap) not found" }
if ($s.IndexOf($hdr) -lt 0) { throw "anchor 4 (header check in RelayUnwrap) not found" }
$s = $s.Replace($src, "")
$s = $s.Replace($hdr, $hdr + "  const bool from_relay = src.sin_addr.s_addr == r.addr_nbo && ntohs(src.sin_port) == r.port;`n" +
                "  if (!from_relay && !P2PAccept(src, RawPort(data + 8))) return 0;`n")

if ($crlf) { $s = $s.Replace("`n", "`r`n") }
Copy-Item $file "$file.before-p2p" -Force
[IO.File]::WriteAllText($file, $s, (New-Object Text.UTF8Encoding($false)))
Write-Host "P2P added to xam_net.cpp (backup: xam_net.cpp.before-p2p). Now run: .\tools\apply-patches.ps1" -ForegroundColor Green
