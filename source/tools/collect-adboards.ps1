# Copies the stadium ad boards and banners out of the game data (data3.big) into
# one zip on the desktop (fifa09_adboards.zip). Nothing in the game changes.
#
#   powershell -ExecutionPolicy Bypass -File tools\collect-adboards.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$big = Join-Path $root "assets\data3.big"
if (Test-Path "$big.orig") { $big = "$big.orig" }   # the untouched copy if the launcher patched it
$tmp = Join-Path $env:TEMP "fifa09_adboards"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force $tmp | Out-Null
& (Join-Path $PSScriptRoot "bigextract.ps1") -Big $big -Match "sceneassets/adboard/" -Out $tmp
& (Join-Path $PSScriptRoot "bigextract.ps1") -Big $big -Match "sceneassets/banner/" -Out $tmp
$out = Join-Path ([Environment]::GetFolderPath("Desktop")) "fifa09_adboards.zip"
if (Test-Path $out) { Remove-Item -Force $out }
Compress-Archive -Path (Join-Path $tmp "*") -DestinationPath $out
Write-Host ("Hazir: {0}  ({1:N1} MB)" -f $out, ((Get-Item $out).Length / 1MB)) -ForegroundColor Green
