# FIFA 09 PC — Setup guide

The Xbox 360 version of FIFA 09 runs natively on PC (static recompilation); online
matches are played through the [nobkonline.com](https://nobkonline.com) servers. It is free.
**No game files are included:** you need the files from your own Xbox 360 copy of FIFA 09.

## Requirements

- Windows 10 or 11 (64-bit)
- A DirectX 12 graphics card (most cards from the last 7–8 years)
- A controller (Xbox / PlayStation) or keyboard + mouse
- The game folder extracted from your own FIFA 09 Xbox 360 disc
  (with `default.xex`, `data0.big` … `data6.big`, `audiodata\`, `data\`)

## Setup

1. **Download** the zip of the latest version from [Releases](https://github.com/ChunchunQ/fifa09-recomp/releases/latest).
2. **Extract** it to a folder that is easy to find, e.g. `C:\Games\FIFA09`. Avoid
   `Program Files`, OneDrive or the Desktop (protected / synced folders).
3. **Add your game files:** copy them into the `Game\` folder (so that `Game\default.xex`
   exists). The folder contains `PUT_YOUR_GAME_FILES_HERE.txt`.
4. **Start** `FIFA09 Launcher.exe`. If Windows says "Windows protected your PC", choose
   **More info → Run anyway** (the launcher is not code-signed).
5. **Verify:** on the **Files** tab press **Verify game files**. The port was built from
   one specific `default.xex`; other regions or title updates crash. Verify tells you.
6. **Create an account** at [nobkonline.com/register](https://nobkonline.com/register/). Your
   username is your in-game name; matches, rating and stats are saved to it.
7. **Sign in** on the launcher's **Online** tab with the same username and password and keep
   **Connect NOBK.Online services** ticked.
8. **Play:** press **PLAY**, open **NOBK.Online** from the game's main menu and pick a
   **ranked 1v1** match. You are paired with whoever is searching (a search lasts 2 minutes).

The result shows up on the site and the leaderboard a few seconds after the match.

## Launcher settings

| Tab | What |
|---|---|
| **Display** | **Resolution:** 720p / 1080p (drawn at 720p, AMD FSR sharpens and upscales it, fast on any PC) or 2K / 4K (native, sharpest, costs FPS). Fullscreen or window, window size, **Monitor**, **Frame rate limit**. |
| **Graphics** | Texture filtering, graphics API (Direct3D 12 recommended). |
| **Controls** | A controller just works. For keyboard turn on **Play with keyboard**; keys can be rebound. Button prompts: Xbox or PlayStation. |
| **Files** | Game folder, verify, **Add to Steam** (with artwork, works on Steam Deck too), **Report a problem** and **Detailed log**. |
| **Achievements** | Your unlocked achievements (the list appears after the first start). |

The launcher updates itself: when a new version is out a bar appears at the top, press
**Update now**. Your settings and sign-in are kept.

## Troubleshooting

- **Game does not start, error 0xc0000142:** install Microsoft's "Visual C++ 2015–2022
  Redistributable (x64)".
- **Low FPS / stutter:** pick **1080p** or **720p** under Display. On a laptop plug in the
  charger, set the Windows power mode to "Best performance" and pick the high-performance
  GPU for `bin\fifa09.exe` in the NVIDIA / AMD panel.
- **Cannot connect online:** make sure you are signed in and "Connect NOBK.Online services"
  is ticked. Allow `fifa09.exe` if the firewall asks. No port forwarding is needed.
- **Connection dropped mid-match:** the result is still saved, the side that dropped is marked **DC**.
- **Crash or bug:** turn on **Files → Detailed log**, make it happen once more, write what
  happened and press **Send log**. Turn the detailed log off for normal play.
- **Antivirus warning:** the launcher and the game are not code-signed, so some antivirus
  programs give false alarms. Files downloaded from the official [Releases](https://github.com/ChunchunQ/fifa09-recomp/releases) page are safe.

NOBK Online is a non-profit community project and is not affiliated with EA Sports.
Game names and artwork belong to their owners.

Support and news: [nobkonline.com](https://nobkonline.com) · report a problem: [Account → Report a problem](https://nobkonline.com/account/#support)
