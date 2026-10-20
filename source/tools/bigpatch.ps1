# Replaces files inside an EA BIG4 archive (FIFA 09 data*.big + .bh) without
# rebuilding it: new data is appended at the end of the archive and only the
# directory entries in the .big header and in the .bh index are updated. All
# other files stay byte-identical, so the game's hash index keeps working.
#
# Always starts from the pristine copies (<file>.orig, created on first run),
# so running it again simply re-applies the patch set.
#
#   One file:
#     .\tools\bigpatch.ps1 -Big assets\data0.big -Entry data/ui/x.big -NewFile mods\x.big
#   A whole folder (paths inside the folder = paths inside the archive):
#     .\tools\bigpatch.ps1 -Big assets\data0.big -ModDir mods\ps_buttons\data0
#   Undo:
#     .\tools\bigpatch.ps1 -Big assets\data0.big -Restore
#
# NOTE: PowerShell variable names are case-insensitive; keep them unique.
param(
  [Parameter(Mandatory)][string]$Big,
  [string]$Entry,
  [string]$NewFile,
  [string]$ModDir,
  [switch]$Restore
)
$ErrorActionPreference = "Stop"

function RdBE([byte[]]$buf, [int]$at) {
  return ([int64]$buf[$at] -shl 24) -bor ([int64]$buf[$at+1] -shl 16) -bor ([int64]$buf[$at+2] -shl 8) -bor [int64]$buf[$at+3]
}
function WrBE([byte[]]$buf, [int]$at, [int64]$val) {
  $buf[$at]   = [byte](($val -shr 24) -band 0xFF)
  $buf[$at+1] = [byte](($val -shr 16) -band 0xFF)
  $buf[$at+2] = [byte](($val -shr 8) -band 0xFF)
  $buf[$at+3] = [byte]($val -band 0xFF)
}
function AlignUp([int64]$val, [int64]$to) { return [int64]([math]::Ceiling($val / $to) * $to) }

$bigPath = (Resolve-Path $Big).Path
$indexPath = [IO.Path]::ChangeExtension($bigPath, ".bh")
foreach ($pf in $bigPath, $indexPath) {
  if (-not (Test-Path "$pf.orig")) { Copy-Item $pf "$pf.orig" }
  Copy-Item "$pf.orig" $pf -Force          # start from the original every time
}
if ($Restore) { Write-Host "Restored $bigPath from .orig" -ForegroundColor Green; return }

# Collect (archive entry name, replacement file) pairs.
$jobs = @()
if ($ModDir) {
  $root = (Resolve-Path $ModDir).Path.TrimEnd('\') + '\'
  foreach ($mf in Get-ChildItem $root -Recurse -File) {
    $jobs += ,@($mf.FullName.Substring($root.Length).Replace('\', '/'), $mf.FullName)
  }
} elseif ($Entry -and $NewFile) {
  $jobs += ,@($Entry.Replace('\', '/'), (Resolve-Path $NewFile).Path)
} else { throw "Give -ModDir, or -Entry with -NewFile, or -Restore." }

$fs = [IO.File]::Open($bigPath, 'Open', 'ReadWrite')
$moves = @()   # (old offset, new offset, stored size, unpacked size)
try {
  $head = New-Object byte[] 16
  [void]$fs.Read($head, 0, 16)
  if ([Text.Encoding]::ASCII.GetString($head, 0, 4) -ne "BIG4") { throw "Not a BIG4 archive" }
  $count = RdBE $head 8
  $hlen  = RdBE $head 12
  $dir = New-Object byte[] $hlen
  $fs.Position = 0
  [void]$fs.Read($dir, 0, $hlen)

  # name -> position of its directory entry
  $entries = @{}
  $pos = 16
  for ($n = 0; $n -lt $count; $n++) {
    $end = $pos + 8
    while ($dir[$end] -ne 0) { $end++ }
    $entries[[Text.Encoding]::ASCII.GetString($dir, $pos + 8, $end - $pos - 8).ToLowerInvariant()] = $pos
    $pos = $end + 1
  }

  foreach ($job in $jobs) {
    $key = $job[0].ToLowerInvariant()
    if (-not $entries.ContainsKey($key)) { throw "Entry '$($job[0])' not found in $bigPath" }
    $epos = $entries[$key]
    $payload = [IO.File]::ReadAllBytes($job[1])
    $unpacked = [int64]$payload.Length
    if ($payload.Length -gt 5 -and $payload[1] -eq 0xFB) {
      if ($payload[0] -eq 0x10) { $unpacked = ([int64]$payload[2] -shl 16) -bor ([int64]$payload[3] -shl 8) -bor $payload[4] }
      elseif ($payload[0] -eq 0x90) { $unpacked = RdBE $payload 2 }
    }
    $oldOff = RdBE $dir $epos
    $newOff = AlignUp $fs.Length 16
    $fs.SetLength($newOff)
    $fs.Position = $newOff
    $fs.Write($payload, 0, $payload.Length)
    WrBE $dir $epos $newOff
    WrBE $dir ($epos + 4) $payload.Length
    $moves += ,@($oldOff, $newOff, $payload.Length, $unpacked)
    Write-Host ("  {0}  ->  0x{1:X}  ({2} bytes)" -f $job[0], $newOff, $payload.Length)
  }

  $dataEnd = AlignUp $fs.Length 16
  $fs.SetLength((AlignUp $fs.Length 0x8000))
  [BitConverter]::GetBytes([uint32]$fs.Length).CopyTo($dir, 4)   # total size, little endian
  $fs.Position = 0
  $fs.Write($dir, 0, $hlen)
}
finally { $fs.Close() }

$index = [IO.File]::ReadAllBytes($indexPath)
$icount = RdBE $index 8
$doneOffs = @{}
foreach ($mv in $moves) {
  $found = $false
  for ($n = 0; $n -lt $icount; $n++) {
    $at = 16 + $n * 20
    if ((RdBE $index $at) -eq $mv[0]) {
      WrBE $index $at $mv[1]
      WrBE $index ($at + 4) (AlignUp $mv[2] 16)
      WrBE $index ($at + 8) $mv[3]
      $found = $true
    }
  }
  # Files with identical content share one copy in the archive; the first move
  # already re-pointed every index entry of that copy.
  if (-not $found -and -not $doneOffs.ContainsKey($mv[0])) { throw ("Offset 0x{0:X} not found in {1}" -f $mv[0], $indexPath) }
  $doneOffs[$mv[0]] = 1
}
WrBE $index (16 + $icount * 20) $dataEnd   # end of data
[IO.File]::WriteAllBytes($indexPath, $index)

Write-Host ("Patched {0}: {1} file(s)" -f (Split-Path $bigPath -Leaf), $moves.Count) -ForegroundColor Green
