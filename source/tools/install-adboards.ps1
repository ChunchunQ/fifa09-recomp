# Puts the NOBK Online ad boards into every FIFA 09 copy under this folder
# (assets\data3.big and the launcher's game folder). The untouched archive is
# kept as data3.big.orig; undo with:  .\tools\install-adboards.ps1 -Undo
#
#   powershell -ExecutionPolicy Bypass -File tools\install-adboards.ps1
param([switch]$Undo)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$mod = Join-Path $root "mods\nobk_adboards\data3"
$bigs = Get-ChildItem -Path $root -Recurse -Filter "data3.big" -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -eq "data3.big" -and $_.FullName -notmatch '\\out\\' }
if (-not $bigs) { throw "data3.big bulunamadi." }
$done = @{}
foreach ($b in $bigs) {
  $real = (Get-Item $b.FullName).FullName
  try { $real = (Resolve-Path $b.FullName).ProviderPath } catch { }
  if ($done.ContainsKey($real)) { continue }
  $done[$real] = 1
  Write-Host "-> $($b.FullName)"
  if ($Undo) { & (Join-Path $PSScriptRoot "bigpatch.ps1") -Big $b.FullName -Restore }
  else { & (Join-Path $PSScriptRoot "bigpatch.ps1") -Big $b.FullName -ModDir $mod }
}
Write-Host "Tamam." -ForegroundColor Green
