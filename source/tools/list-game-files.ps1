# Lists every file of the FIFA 09 game data, including the entries inside the
# BIG archives, into one text file on the desktop (player faces, ad boards...).
# Nothing is changed or extracted.
#
#   powershell -ExecutionPolicy Bypass -File tools\list-game-files.ps1
param(
  [string]$GameData = "",
  [string]$Out = (Join-Path ([Environment]::GetFolderPath("Desktop")) "fifa09_files.txt")
)
$ErrorActionPreference = "Stop"
if (-not $GameData) {
  foreach ($c in @((Join-Path (Get-Location) "assets"), (Join-Path $PSScriptRoot "..\assets"),
                   "$env:USERPROFILE\Desktop\fifa09-pc\assets")) {
    if (Test-Path (Join-Path $c "data")) { $GameData = (Resolve-Path $c).Path; break }
  }
}
if (-not $GameData -or -not (Test-Path $GameData)) { throw "Oyun klasoru bulunamadi. -GameData ile assets klasorunu ver." }
$GameData = (Resolve-Path $GameData).Path.TrimEnd('\', '/')
Write-Host "Oyun verisi: $GameData"

function RdBE([byte[]]$b, [int]$at) {
  return ([int64]$b[$at] -shl 24) -bor ([int64]$b[$at+1] -shl 16) -bor ([int64]$b[$at+2] -shl 8) -bor [int64]$b[$at+3]
}
$lines = New-Object System.Collections.Generic.List[string]
$entries = 0
foreach ($f in Get-ChildItem -Path $GameData -Recurse -File -ErrorAction SilentlyContinue) {
  $rel = $f.FullName.Substring($GameData.Length).TrimStart('\')
  $lines.Add(("{0}`t{1}" -f $rel, $f.Length))
  if ($f.Extension -notin '.big', '.viv') { continue }
  $fs = [IO.File]::OpenRead($f.FullName)
  try {
    if ($fs.Length -lt 16) { continue }
    $head = New-Object byte[] 16
    [void]$fs.Read($head, 0, 16)
    $magic = [Text.Encoding]::ASCII.GetString($head, 0, 4)
    if ($magic -ne "BIGF" -and $magic -ne "BIG4") { continue }
    $count = RdBE $head 8; $hlen = RdBE $head 12
    if ($hlen -le 16 -or $hlen -gt $fs.Length -or $count -gt 500000) { continue }
    $dir = New-Object byte[] $hlen
    $fs.Position = 0; [void]$fs.Read($dir, 0, $hlen)
    $pos = 16
    for ($n = 0; $n -lt $count -and $pos -lt $hlen - 9; $n++) {
      $size = RdBE $dir ($pos + 4)
      $end = $pos + 8
      while ($end -lt $hlen -and $dir[$end] -ne 0) { $end++ }
      $name = [Text.Encoding]::ASCII.GetString($dir, $pos + 8, $end - $pos - 8)
      $pos = $end + 1
      $lines.Add(("{0} :: {1}`t{2}" -f $rel, $name, $size))
      $entries++
    }
  } finally { $fs.Close() }
}
$lines | Set-Content $Out -Encoding UTF8
Write-Host ("Hazir: {0}  ({1} satir, arsiv ici {2} dosya)" -f $Out, $lines.Count, $entries) -ForegroundColor Green
