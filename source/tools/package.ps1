# Builds the launcher and assembles a distributable folder:
#   out\package\FIFA09-PC\        (default; inside out\, so nobody plays from it)
#     FIFA09 Launcher.exe
#     bin\fifa09.exe + runtime DLLs
#     Game\            (empty; players put their own game files here)
#
# Game files are never copied. Run from the repository root.
#   .\tools\package.ps1                              -> fresh out\package\FIFA09-PC (zip this one)
#   .\tools\package.ps1 -Update dist\FIFA09-PC-new   -> also refresh the exe, DLLs and launcher of a
#                                                       folder you play from (launcher.json and Game\ stay)
param([string]$Out = "out\package\FIFA09-PC", [string]$Update = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "out\build\win-amd64-relwithdebinfo"
$dist = Join-Path $root $Out

if (-not (Test-Path "$build\fifa09.exe")) { throw "fifa09.exe not built. Run the cmake build first." }
# Never delete a player's login token or game files while repackaging.
if ((Test-Path "$dist\launcher.json") -or (Test-Path "$dist\Game\default.xex")) {
  throw "Destination $Out contains player data (launcher.json / game files). Use the default -Out, and -Update for folders you play from."
}

Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$dist\bin", "$dist\Game" | Out-Null

# Game executable and the runtime DLLs next to it (no PDBs, no logs, no shader cache).
Copy-Item "$build\fifa09.exe" "$dist\bin\"
Get-ChildItem $build -Filter *.dll | Copy-Item -Destination "$dist\bin\"

# Achievement sound (royalty-free, ours to ship).
if (Test-Path "$root\resources\sounds") {
  New-Item -ItemType Directory -Force "$dist\bin\sounds" | Out-Null
  Copy-Item "$root\resources\sounds\*" "$dist\bin\sounds\"
}

# Game-file mods the launcher applies at play time (PS prompts, skip intro, signature).
if (Test-Path "$root\mods") { Copy-Item "$root\mods" "$dist\mods" -Recurse }

# Launcher: single self-contained exe.
dotnet publish "$root\Launcher\Fifa09Launcher\Fifa09Launcher.csproj" -c Release -o "$root\out\launcher"
if ($LASTEXITCODE -ne 0) { throw "Launcher build failed." }
Copy-Item "$root\out\launcher\FIFA09 Launcher.exe" "$dist\"

# Steam library artwork (cover.png, banner.png, wide.png, logo.png, icon.png/.ico).
if (Test-Path "$root\resources\steam") {
  New-Item -ItemType Directory -Force "$dist\steam" | Out-Null
  Copy-Item "$root\resources\steam\*" "$dist\steam\"
}

# background / logo are built into the launcher exe (Fifa09Launcher.csproj), not copied.

Set-Content "$dist\Game\PUT_YOUR_GAME_FILES_HERE.txt" -Encoding ascii -Value @"
Copy the contents of your own extracted Xbox 360 FIFA 09 disc here,
so that default.xex, data0.big ... data6.big, audiodata\ and data\ are in this folder.
Game files are not included with this port.
"@

Write-Host "Done: $dist" -ForegroundColor Green

if ($Update) {
  $u = Join-Path $root $Update
  if (-not (Test-Path $u)) { throw "Update folder not found: $Update" }
  if (Get-Process -Name "FIFA09 Launcher", "fifa09" -ErrorAction SilentlyContinue) {
    throw "Close the launcher and the game first, then run this again."
  }
  New-Item -ItemType Directory -Force "$u\bin" | Out-Null
  Copy-Item "$dist\bin\*" "$u\bin\" -Recurse -Force
  Copy-Item "$dist\FIFA09 Launcher.exe" "$u\" -Force
  if (Test-Path "$dist\mods") { Copy-Item "$dist\mods" $u -Recurse -Force }
  # art is inside the launcher exe now
  foreach ($old in "background.jpg", "background.png", "logo.png") { Remove-Item "$u\$old" -ErrorAction SilentlyContinue }
  Write-Host "Updated: $u (launcher.json and Game\ kept)" -ForegroundColor Green
}
Get-ChildItem $dist -Recurse -File | Select-Object @{n='File';e={$_.FullName.Replace("$dist\","")}}, Length
