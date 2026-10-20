# How the online part works (for other Xbox 360 recomp projects)

FIFA 09 PC talks to a replacement server instead of Xbox LIVE and EA. Most of the work is in the
**ReXGlue SDK** (title independent) and can be reused by any recompiled Xbox 360 title. Only a
small part is FIFA-specific. SDK changes: [`patches/nobk-sdk-1.3.0.patch`](../patches/nobk-sdk-1.3.0.patch)
(one patch on rexglue-sdk `d79d3ac`). Every change is marked with an `// [online]` comment.

## 1. Pretending to be signed in to LIVE (SDK, reusable as is)

| File | What it does |
|---|---|
| `src/kernel/xam/xam_user.cpp` | Profile signed in to LIVE (state 2), every privilege granted (multiplayer, communications…). Test switch `online_disable=signin,priv`. |
| `src/system/xam/user_profile.cpp` | Per-install identity (gamertag, XUID) set by the launcher instead of a fixed default. |
| `src/kernel/xam/xam_enum.cpp`, `xam_info.cpp`, `xam_misc.cpp` | Stubs that used to fail or return 0 (enumerators, title id, XNetLogonGetTitleID…). Titles stop at the first failing call, so these matter. |
| `src/kernel/xam/apps/xlivebase_app.cpp` | LIVE messages: `XOnlineGetServiceInfo` (0x58007) → your server's address; unidentified logon selectors answered so EA clients do not block; `XStringVerify` (profanity check) accepts everything; `XFriendsCreateEnumerator` (0x58020); `XInviteGetAcceptedInfo` (0x58023). |
| `src/kernel/xam/apps/xgi_app.cpp` | XSession* (create/join/start/end/delete, details). Host sessions get a real XSESSION_INFO (random session id, the console's XNADDR, key) and a nonce, because titles publish it to their lobby. Several asserts on buffer sizes replaced by tolerant handling (titles send other sizes than documented). |

## 2. Networking (SDK, reusable)

All in `src/kernel/xam/xam_net.cpp` (+ `src/system/xsocket.cpp` for byte order):

- **Server address**: cvar `online_server_ip` + DNS redirection: titles that resolve their own hosts (EA's
  `*.ea.com`, `gosredirector`…) get the replacement server. See the "EA Nation revival redirect" and
  "Blaze" parts, adapted from the Skate 3 revival.
- **XNet**: title XNADDR, `XNetGetConnectStatus`, `XNetQosLookup` (answered so QoS modules finish),
  stable fake console id, `getpeername`/`getsockname`/`getsockopt` (were stubs; DirtySDK needs them).
- **Match traffic relay** (`online_relay = host:port`): every player gets a virtual IP `10.x.y.z`. UDP to
  another player's 10.x address is wrapped in a 16-byte header (`'R' 'L' 1 type`, own virtual IP) and sent
  to the relay, which forwards it. No port forwarding, works behind any NAT, nobody sees anyone's IP.
- **Direct P2P next to the relay**: when both players allow it, the relay hands out the peers' public (and
  same-router LAN) addresses; traffic switches to a direct path once a packet comes back.
- **Receive pump**: overlapped UDP receives are completed early so the title does not wait a frame per
  packet (less stutter online).
- **Same-PC testing**: port offsets so two instances run on one PC.
- **Diagnostics**: packet logs, per-port traffic summary, wait trace (all switchable).

Relay packet types used: 5 live match events → server, 7/8 P2P, 10/11 friends + invites.

## 3. In-game NOBK.Online panel (SDK, reusable)

`online_social.h`, the social part of `xam_net.cpp`, `xam_ui.cpp` (panel + notifications). Friends,
invites, join a friend's lobby, achievements tab, server status, notices. Porting guide with the protocol:
ask for `social_port.zip` (README inside); short version: section "Friends" of the SDK patch.

## 4. Title specific (FIFA 09, in this repo)

- `game/src/online_hooks.h` + `game/config/online_hooks.toml`: `[[midasm_hook]]` patches inside the
  recompiled code: capacity limits the original servers sent (team play max players), fixes for code paths
  that never ran on PC, match event reporting (goals, cards, line-ups) for the website, half length,
  crash guards. **Each hook is title-specific**, but the pattern (hook at an address, read/patch guest
  registers and memory, optional jump) is how you fix any title's online code.
- `game/config/online_functions.toml`: function boundaries the code generator needed for online code.

## 5. Server (per title family)

FIFA 09 uses EA's **Aries/Plasma lobby protocol** (TCP, text commands like `auth`, `pers`, `gcre`, `gjoi`,
`gget`, `gsta`, `+who`, `+mgm`, `+ses`). Our server implements those commands plus the UDP relay, results
(`rank` report → rating, leaderboard) and the friends channel. Other EA titles of the same era (2007-2010)
use the same protocol with small differences; titles on Blaze (later EA), GameSpy or their own servers need
their own server side. Lessons that apply everywhere:

- Answer **every** command, even with an empty reply: clients block on a missing answer.
- Log every command in and out from day one; most fixes came from reading those logs side by side with
  the client log.
- Lobby state events (`+who`, `+mgm`, `+ses` in Aries) drive the client's state machine: sending one at the
  wrong moment (e.g. "game started" to a member who just joined) breaks the client.

## 6. Launcher

`launcher/`: sign-in with the site account (stores a token, passes gamertag/XUID/server to the game),
game file mods (`ModManager.cs`: patches `.big` archives from `mods/`, re-applies when mod content changes),
self-update, log upload.
