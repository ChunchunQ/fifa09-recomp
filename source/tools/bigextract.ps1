# Extracts files from an EA BIG4/BIGF archive without unpacking all of it.
#   .\tools\bigextract.ps1 -Big assets\data1.big -Match "bootflow/" -Out extracted
# -Match is a plain substring of the path inside the archive (case-insensitive).
# Files are written as stored (still RefPack-compressed if they were).
param(
  [Parameter(Mandatory)][string]$Big,
  [Parameter(Mandatory)][string]$Match,
  [string]$Out = "extracted"
)
$ErrorActionPreference = "Stop"
function RdBE([byte[]]$buf, [int]$at) {
  return ([int64]$buf[$at] -shl 24) -bor ([int64]$buf[$at+1] -shl 16) -bor ([int64]$buf[$at+2] -shl 8) -bor [int64]$buf[$at+3]
}
$bigPath = (Resolve-Path $Big).Path
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location).Path $Out }
$fs = [IO.File]::OpenRead($bigPath)
try {
  $head = New-Object byte[] 16
  [void]$fs.Read($head, 0, 16)
  $count = RdBE $head 8
  $hlen = RdBE $head 12
  $dir = New-Object byte[] $hlen
  $fs.Position = 0
  [void]$fs.Read($dir, 0, $hlen)
  $pos = 16; $hits = 0
  for ($n = 0; $n -lt $count; $n++) {
    $off = RdBE $dir $pos
    $size = RdBE $dir ($pos + 4)
    $end = $pos + 8
    while ($dir[$end] -ne 0) { $end++ }
    $name = [Text.Encoding]::ASCII.GetString($dir, $pos + 8, $end - $pos - 8)
    $pos = $end + 1
    if ($name.ToLowerInvariant().Contains($Match.ToLowerInvariant())) {
      $dest = Join-Path $Out ($name.Replace('/', '\'))
      New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
      $buf = New-Object byte[] $size
      $fs.Position = $off
      [void]$fs.Read($buf, 0, $size)
      [IO.File]::WriteAllBytes($dest, $buf)
      Write-Host ("  {0}  ({1} bytes)" -f $name, $size)
      $hits++
    }
  }
  Write-Host "$hits file(s) extracted to $Out" -ForegroundColor Green
}
finally { $fs.Close() }
