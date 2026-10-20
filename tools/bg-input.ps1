# Controller input while the game window is in the background (another window
# focused): sets SDL's JOYSTICK_ALLOW_BACKGROUND_EVENTS hint before the SDL
# input driver starts. Patches the SDK's sdl_input_driver.cpp, then build with:
#   .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$file = Join-Path $Sdk "src\input\sdl\sdl_input_driver.cpp"
$raw = [IO.File]::ReadAllText($file)
$crlf = $raw.Contains("`r`n")
$s = $raw.Replace("`r`n", "`n")
if ($s.Contains("[online] Background input")) { Write-Host "Background input is already in sdl_input_driver.cpp" -ForegroundColor Yellow; exit 0 }
$a = @'
      // Initialize SDL events subsystem
      if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
'@.Replace("`r`n", "`n")
$b = @'
      // [online] Background input: keep reading the controller when another
      // window has the focus (Discord, browser...). SDL drops pad events
      // for unfocused windows unless this hint is set before init.
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

      // Initialize SDL events subsystem
      if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
'@.Replace("`r`n", "`n")
$i = $s.IndexOf($a)
if ($i -lt 0) { throw "anchor not found in sdl_input_driver.cpp (nothing was changed)" }
if ($s.IndexOf($a, $i + 1) -ge 0) { throw "anchor found twice (nothing was changed)" }
$s = $s.Replace($a, $b)
if ($crlf) { $s = $s.Replace("`n", "`r`n") }
Copy-Item $file "$file.before-bginput" -Force
[IO.File]::WriteAllText($file, $s, (New-Object Text.UTF8Encoding($false)))
Write-Host "Background input added (backup: sdl_input_driver.cpp.before-bginput). Now run: .\tools\apply-patches.ps1" -ForegroundColor Green
