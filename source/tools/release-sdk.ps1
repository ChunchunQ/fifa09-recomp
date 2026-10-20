# Turns off the SDK-side diagnostics added while hunting the online stutter
# (wait trace: "[wait] lr ..." lines every 5 s). Safe to run more than once.
# Then rebuild with:  .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$old = "if (timeout >= 0) return;  // none, or an absolute time"
$new = "if (true) return;  // wait trace off (release build)"
$n = 0
foreach ($f in Get-ChildItem (Join-Path $Sdk "src") -Recurse -Include *.cpp, *.h, *.inc -File) {
  $raw = [IO.File]::ReadAllText($f.FullName)
  if ($raw.Contains($old)) {
    [IO.File]::WriteAllText($f.FullName, $raw.Replace($old, $new))
    Write-Host "  wait trace off: $($f.FullName.Substring($Sdk.Length + 1))"
    $n++
  }
}
if ($n -eq 0) { Write-Host "Wait trace already off (or never added)." -ForegroundColor Yellow }
else { Write-Host "Done ($n file(s)). Now run .\tools\apply-patches.ps1" -ForegroundColor Green }
