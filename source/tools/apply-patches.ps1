# Applies SDK patches from fifa09-pc\patches, rebuilds and installs the SDK,
# then rebuilds the game.
#
#   powershell -ExecutionPolicy Bypass -File tools\apply-patches.ps1 online-revival-9.patch online-revival-10.patch
[CmdletBinding(PositionalBinding = $false)]
param(
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Patches,
  [string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
foreach ($p in $Patches) {
  $path = if (Test-Path $p) { (Resolve-Path $p).Path } else { Join-Path $root "patches\$p" }
  cmd /c "git -C `"$Sdk`" apply --check `"$path`" 2>nul"
  if ($LASTEXITCODE -eq 0) {
    git -C $Sdk apply $path
    Write-Host "applied $p" -ForegroundColor Green
  } else {
    cmd /c "git -C `"$Sdk`" apply --reverse --check `"$path`" 2>nul"
    if ($LASTEXITCODE -eq 0) { Write-Host "already applied: $p" -ForegroundColor Yellow }
    else {
      # The SDK differs a little from the patch base: let git merge it.
      cmd /c "git -C `"$Sdk`" apply --3way `"$path`""
      if ($LASTEXITCODE -ne 0) { throw "patch does not apply: $p (send me the output above)" }
      Write-Host "applied $p (3-way merge)" -ForegroundColor Green
    }
  }
}
cmake --build "$Sdk\out\build\win-amd64" --config RelWithDebInfo; if ($LASTEXITCODE) { throw "SDK build failed" }
cmake --install "$Sdk\out\build\win-amd64" --config RelWithDebInfo; if ($LASTEXITCODE) { throw "SDK install failed" }
cmake --build "$root\out\build\win-amd64-relwithdebinfo"; if ($LASTEXITCODE) { throw "game build failed" }
Write-Host "Done." -ForegroundColor Green
