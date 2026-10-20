# Replaces "Xbox LIVE" with "NOBK.Online" in the game's texts (data\loc\locale.big)
# in every FIFA 09 copy under this folder. The original is kept as locale.big.orig;
# undo with:  .\tools\install-text.ps1 -Undo
param([switch]$Undo)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$new = Join-Path $root "mods\nobk_text\locale.big"
$bigs = Get-ChildItem -Path $root -Recurse -Filter "locale.big" -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -eq "locale.big" -and $_.DirectoryName -match '\\data\\loc$' -and $_.FullName -notmatch '\\mods\\' }
if (-not $bigs) { throw "data\loc\locale.big bulunamadi." }
foreach ($b in $bigs) {
  $p = $b.FullName
  Write-Host "-> $p"
  if (-not (Test-Path "$p.orig")) {
    if ($b.Length -ne 4360448) { Write-Host "   beklenmeyen boyut ($($b.Length)), atlandi" -ForegroundColor Yellow; continue }
    Copy-Item $p "$p.orig"
  }
  if ($Undo) { Copy-Item "$p.orig" $p -Force } else { Copy-Item $new $p -Force }
}
Write-Host "Tamam." -ForegroundColor Green
