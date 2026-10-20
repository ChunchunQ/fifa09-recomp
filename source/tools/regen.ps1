# Re-runs the ReXGlue code generator (after config changes such as
# config\online_functions.toml) and rebuilds the game.
#
#   powershell -ExecutionPolicy Bypass -File tools\regen.ps1
param(
  [string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$manifest = Get-ChildItem $root -Filter "*_manifest.toml" -File | Select-Object -First 1
if (-not $manifest) { throw "No *_manifest.toml found in $root" }
Write-Host "Manifest: $($manifest.FullName)"

# Make sure the entrypoint includes our config files (online_functions.toml,
# and online_hooks.toml when present).
$text = [IO.File]::ReadAllText($manifest.FullName)
$wanted = @('"config/online_functions.toml"')
if (Test-Path (Join-Path $root "config\online_hooks.toml")) { $wanted += '"config/online_hooks.toml"' }
$changed = $false
foreach ($include in $wanted) {
  if ($text -match [regex]::Escape($include)) { continue }
  $rx = [regex]'(?s)(\[entrypoint\].*?includes\s*=\s*\[)([^\]]*)(\])'
  if (-not $rx.IsMatch($text)) { throw "Could not find 'includes = [...]' under [entrypoint] in $($manifest.Name)" }
  $text = $rx.Replace($text, [Text.RegularExpressions.MatchEvaluator]{
    param($m)
    $inner = $m.Groups[2].Value.Trim()
    $new = if ($inner) { "$inner, $include" } else { $include }
    $m.Groups[1].Value + $new + $m.Groups[3].Value
  }, 1)
  Write-Host "Added $include to the [entrypoint] includes"
  $changed = $true
}
if ($changed) {
  Copy-Item $manifest.FullName "$($manifest.FullName).bak" -Force
  [IO.File]::WriteAllText($manifest.FullName, $text, (New-Object Text.UTF8Encoding $false))
}

# The debug probes need their code compiled into the game: include
# src\online_hooks.h from src\fifa09_app.h (once).
$hooks = Join-Path $root "src\online_hooks.h"
$app = Join-Path $root "src\fifa09_app.h"
if ((Test-Path $hooks) -and (Test-Path $app)) {
  $a = [IO.File]::ReadAllText($app)
  if ($a -notmatch 'online_hooks\.h') {
    $line = '#include "crash_handler.h"'
    if ($a.Contains($line)) { $a = $a.Replace($line, "$line`r`n#include `"online_hooks.h`"") }
    else { $a = "#include `"online_hooks.h`"`r`n" + $a }
    [IO.File]::WriteAllText($app, $a, (New-Object Text.UTF8Encoding $false))
    Write-Host "Added #include `"online_hooks.h`" to src\fifa09_app.h"
  }
}

$exe = @(
  "$Sdk\out\install\win-amd64\bin\rexglue.exe",
  "$Sdk\out\build\win-amd64\RelWithDebInfo\rexglue.exe",
  "$Sdk\out\build\win-amd64\Release\rexglue.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { $exe = (Get-Command rexglue -ErrorAction SilentlyContinue).Source }
if (-not $exe) { throw "rexglue.exe not found under $Sdk" }
Write-Host "Code generator: $exe"

Push-Location $root
try {
  & $exe codegen $manifest.FullName
  if ($LASTEXITCODE -ne 0) { throw "codegen failed ($LASTEXITCODE)" }
  cmake --build (Join-Path $root "out\build\win-amd64-relwithdebinfo")
  if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
} finally { Pop-Location }
Write-Host "Done." -ForegroundColor Green
