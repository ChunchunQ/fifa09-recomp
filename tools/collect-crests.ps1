# Collects the team crest files (teamLogos*) from the FIFA 09 game data into
# one zip on the desktop, so they can be converted to PNG for the website and
# Discord cards. Nothing in the game folder is changed.
#
#   powershell -ExecutionPolicy Bypass -File collect-crests.ps1
#   powershell -ExecutionPolicy Bypass -File collect-crests.ps1 -GameData C:\...\fifa09-pc\assets
param(
  [string]$GameData = "",
  [string]$Out = (Join-Path ([Environment]::GetFolderPath("Desktop")) "fifa09_crests.zip")
)
$ErrorActionPreference = "Stop"

if (-not $GameData) {
  foreach ($c in @((Join-Path (Get-Location) "assets"), (Join-Path $PSScriptRoot "assets"),
                   (Join-Path $PSScriptRoot "..\assets"), "$env:USERPROFILE\Desktop\fifa09-pc\assets")) {
    if (Test-Path (Join-Path $c "data")) { $GameData = (Resolve-Path $c).Path; break }
  }
}
if (-not $GameData -or -not (Test-Path $GameData)) { throw "Oyun klasoru bulunamadi. -GameData ile assets klasorunu ver." }
$GameData = (Resolve-Path $GameData).Path.TrimEnd('\', '/')
Write-Host "Oyun verisi: $GameData"

$tmp = Join-Path $env:TEMP "fifa09_crests"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force $tmp | Out-Null

function RdBE([byte[]]$b, [int]$at) {
  return ([int64]$b[$at] -shl 24) -bor ([int64]$b[$at+1] -shl 16) -bor ([int64]$b[$at+2] -shl 8) -bor [int64]$b[$at+3]
}

# 1) loose files whose path mentions teamlogo / crest / badge
$loose = Get-ChildItem -Path $GameData -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '(?i)teamlogo|crest|badge' }
foreach ($f in $loose) {
  $rel = $f.FullName.Substring($GameData.Length).TrimStart('\')
  $dest = Join-Path $tmp ("loose\" + $rel)
  New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
  Copy-Item $f.FullName $dest
}
Write-Host ("Klasorde bulunan dosya: {0}" -f @($loose).Count)

# 2) entries inside BIG archives
$inBig = 0
$listing = New-Object System.Collections.Generic.List[string]
foreach ($big in Get-ChildItem -Path $GameData -Recurse -File -Include *.big, *.viv -ErrorAction SilentlyContinue) {
  $fs = [IO.File]::OpenRead($big.FullName)
  try {
    if ($fs.Length -lt 16) { continue }
    $head = New-Object byte[] 16
    [void]$fs.Read($head, 0, 16)
    $magic = [Text.Encoding]::ASCII.GetString($head, 0, 4)
    if ($magic -ne "BIGF" -and $magic -ne "BIG4") { continue }
    $count = RdBE $head 8; $hlen = RdBE $head 12
    if ($hlen -le 16 -or $hlen -gt $fs.Length -or $count -gt 200000) { continue }
    $dir = New-Object byte[] $hlen
    $fs.Position = 0; [void]$fs.Read($dir, 0, $hlen)
    $pos = 16
    for ($n = 0; $n -lt $count -and $pos -lt $hlen - 9; $n++) {
      $off = RdBE $dir $pos; $size = RdBE $dir ($pos + 4)
      $end = $pos + 8
      while ($end -lt $hlen -and $dir[$end] -ne 0) { $end++ }
      $name = [Text.Encoding]::ASCII.GetString($dir, $pos + 8, $end - $pos - 8)
      $pos = $end + 1
      if ($name -match '(?i)teamlogo|crest|badge') {
        $listing.Add("$($big.Name) :: $name ($size)")
        $dest = Join-Path $tmp ("big\" + $big.BaseName + "\" + $name.Replace('/', '\'))
        New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
        $buf = New-Object byte[] $size
        $fs.Position = $off; [void]$fs.Read($buf, 0, $size)
        [IO.File]::WriteAllBytes($dest, $buf)
        $inBig++
      }
    }
  } finally { $fs.Close() }
}
Write-Host ("Arsivlerin icinde bulunan dosya: {0}" -f $inBig)

# 3) a listing of the UI art folders, so we know what else is there
$ui = Join-Path $GameData "data\ui"
if (Test-Path $ui) {
  Get-ChildItem -Path $ui -Recurse -Directory | ForEach-Object {
    "{0}  ({1} dosya)" -f $_.FullName.Substring($GameData.Length), @(Get-ChildItem $_.FullName -File).Count
  } | Set-Content (Join-Path $tmp "ui_dirs.txt") -Encoding UTF8
}
$listing | Set-Content (Join-Path $tmp "big_entries.txt") -Encoding UTF8

if ((@($loose).Count + $inBig) -eq 0) {
  Write-Host "Arma dosyasi bulunamadi; yine de klasor listesini ziple gonderiyorum." -ForegroundColor Yellow
}
if (Test-Path $Out) { Remove-Item -Force $Out }
Compress-Archive -Path (Join-Path $tmp "*") -DestinationPath $Out
$mb = (Get-Item $Out).Length / 1MB
Write-Host ("Hazir: {0}  ({1:N1} MB)" -f $Out, $mb) -ForegroundColor Green
