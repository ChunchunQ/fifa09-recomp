# Building from source

The code written for the FIFA 09 PC port. MIT licensed (see [LICENSE](LICENSE)).

| Folder | What |
|---|---|
| `game/` | The port project: app code, crash handler, achievements, online hooks (`src/online_hooks.h`, `config/online_hooks.toml`), CMake files, codegen manifest |
| `launcher/` | WPF launcher (.NET 8): settings, NOBK.Online sign-in, achievements, self-update |
| `patches/` | Patches for the ReXGlue SDK (online services, P2P, input, FSR, build fixes) |
| `tools/` | Build and packaging scripts, `.big` / RefPack / Xbox 360 texture tools, reverse-engineering helpers |

Not included: the game, and the C++ the SDK generates from it (`generated/`). Both come from your own copy.

## Building

Needs Windows, Visual Studio 2022 with Clang (clang-cl), CMake, Ninja, the .NET 8 SDK and your own FIFA 09 Xbox 360 files.

1. Clone [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) next to this repo, check out `d79d3ac` and apply the one SDK patch:
   `tools\apply-patches.ps1 -Sdk <path to rexglue-sdk> nobk-sdk-1.3.0.patch` (it rebuilds the SDK).
   `patches\history\` holds the older step-by-step patches for reference; they are already part of it.
2. Put your game files in `game\assets\` (`default.xex` must match the SHA-256 in the main README).
3. Generate the recompiled code: `tools\regen.ps1` (uses `fifa09_manifest.toml`, `fifa09_overrides.toml` and `config\*.toml`).
4. Build the game with the CMake presets (`CMakePresets.json`), and the launcher with `dotnet build launcher -c Release`.
5. `tools\package.ps1` puts game, launcher and runtime files together in `out\package\FIFA09-PC`.

The scripts default to `%USERPROFILE%\Desktop\rexglue-sdk`; pass `-Sdk` to use another path.
The online hooks talk to the NOBK.Online servers; the server code is not part of this repo.
