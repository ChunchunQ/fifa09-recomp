# Copies the player database and a few sample face pictures out of the game data
# into one zip on the desktop (fifa09_db_heads.zip). Nothing in the game changes.
#
#   powershell -ExecutionPolicy Bypass -File tools\collect-db-heads.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$assets = Join-Path $root "assets"
$tmp = Join-Path $env:TEMP "fifa09_db_heads"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force $tmp | Out-Null
& (Join-Path $PSScriptRoot "bigextract.ps1") -Big (Join-Path $assets "data0.big") -Match "data/db/fifa_ng_db" -Out $tmp
& (Join-Path $PSScriptRoot "bigextract.ps1") -Big (Join-Path $assets "data\ui\artAssets\heads\2dheads.big") -Match "heads/h1001" -Out $tmp
$out = Join-Path ([Environment]::GetFolderPath("Desktop")) "fifa09_db_heads.zip"
if (Test-Path $out) { Remove-Item -Force $out }
Compress-Archive -Path (Join-Path $tmp "*") -DestinationPath $out
Write-Host ("Hazir: {0}  ({1:N1} MB)" -f $out, ((Get-Item $out).Length / 1MB)) -ForegroundColor Green
