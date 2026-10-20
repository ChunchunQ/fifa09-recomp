param([int]$Max = 12, [int]$RunSeconds = 45, [uint32]$MaxSize = 0x100)
$ErrorActionPreference = "Continue"
$root   = $PSScriptRoot
$ov     = Join-Path $root "fifa09_overrides.toml"
$reg    = Join-Path $root "generated\default\fifa09_register.cpp"
$run    = Join-Path $root "out\build\win-amd64-relwithdebinfo"
$assets = Join-Path $root "assets"
$pat    = "invalid or unregistered function at guest address 0x([0-9A-Fa-f]+)"
$textLo = [Convert]::ToUInt32("822E0000", 16)
$textHi = [Convert]::ToUInt32("833442C4", 16)
Set-Location $root

function Get-Funcs {
  Select-String -Path $reg -Pattern "SetFunction\(0x([0-9A-Fa-f]{8})," |
    ForEach-Object { [Convert]::ToUInt32($_.Matches[0].Groups[1].Value, 16) } | Sort-Object
}

function Set-Func([uint32]$a, [uint32]$size) {
  $key  = "0x{0:X8}" -f $a
  $line = "{0} = {{ size = 0x{1:X} }}" -f $key, $size
  $text = Get-Content $ov -Raw
  $re   = "(?m)^" + [regex]::Escape($key) + "[ \t]*=[^\r\n]*"
  if ($text -match $re) { $text = [regex]::Replace($text, $re, $line) }
  else { $text = $text.TrimEnd() + "`r`n" + $line + "`r`n" }
  Set-Content $ov $text -Encoding ascii -NoNewline
  Write-Host "  override: $line"
}

function Shrink-Overlaps([uint32]$a) {
  $text = Get-Content $ov -Raw
  $rx = [regex]"(?m)^0x([0-9A-Fa-f]{8})[ \t]*=[ \t]*\{[ \t]*size[ \t]*=[ \t]*0x([0-9A-Fa-f]+)[ \t]*\}"
  foreach ($m in $rx.Matches($text)) {
    $s = [Convert]::ToUInt32($m.Groups[1].Value, 16)
    $z = [Convert]::ToUInt32($m.Groups[2].Value, 16)
    if ($s -lt $a -and ($s + $z) -gt $a) { Set-Func $s ($a - $s) }
  }
}

for ($i = 1; $i -le $Max; $i++) {
  Write-Host "=== Tur $i ===" -ForegroundColor Cyan
  Copy-Item $ov "$ov.bak" -Force
  cmake --build --preset win-amd64-relwithdebinfo -j 6 *> "$root\build.log"
  if ($LASTEXITCODE -ne 0) { Write-Host "Build hatasi (tur basi):" -ForegroundColor Red; Get-Content "$root\build.log" -Tail 20; return }
  Get-Process fifa09 -ErrorAction SilentlyContinue | Stop-Process -Force
  Remove-Item "$run\run.log" -ErrorAction SilentlyContinue
  $p = Start-Process "$run\fifa09.exe" -WorkingDirectory $run -PassThru -ArgumentList @("--game_data_root", $assets, "--gpu_plugin", "xenos", "--log_level", "info", "--log_file", "run.log")

  $hit = $null
  for ($s = 0; $s -lt $RunSeconds; $s++) {
    Start-Sleep 1
    $hit = Select-String -Path "$run\run.log" -Pattern $pat -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($hit -or $p.HasExited) { break }
  }
  Start-Sleep 1
  if (-not $hit) { $hit = Select-String -Path "$run\run.log" -Pattern $pat -ErrorAction SilentlyContinue | Select-Object -Last 1 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
  if (-not $hit) {
    Write-Host "Eksik-fonksiyon hatasi yok. run.log son satirlari:" -ForegroundColor Yellow
    Get-Content "$run\run.log" -Tail 15
    return
  }

  $addr  = [Convert]::ToUInt32($hit.Matches[0].Groups[1].Value, 16)
  $funcs = Get-Funcs
  $prev  = $funcs | Where-Object { $_ -le $addr } | Select-Object -Last 1
  $next  = $funcs | Where-Object { $_ -gt $addr } | Select-Object -First 1
  Write-Host ("Cokus: 0x{0:X8} (onceki 0x{1:X8}, sonraki 0x{2:X8})" -f $addr, $prev, $next)

  if ($addr -lt $textLo -or $addr -gt $textHi) { Write-Host "Adres .text disinda. Durdum."; return }
  if ($prev -eq $addr) { Write-Host "Adres zaten kayitli, farkli bir sorun. Durdum."; return }  if ($prev -eq $addr) { Write-Host "Adres kayitli ama oyun yine de cokuyor. Exe guncel mi, fonksiyonun govdesi mi sorunlu? Elle bakin. Durdum."; return }
  if (-not $next -or ($next - $addr) -gt $MaxSize) { Write-Host "Boyut cok buyuk ya da bilinmiyor, elle bakin. Durdum."; return }

  Shrink-Overlaps $addr
  Set-Func $addr ($next - $addr)

  & rexglue --force codegen --ignore-stamp fifa09_manifest.toml *> "$root\codegen.log"
  if (-not (Select-String -Path "$root\codegen.log" -Pattern "Codegen summary" -Quiet) -or
      (Select-String -Path "$root\codegen.log" -Pattern "overlap|Failed to parse|Missing required" -Quiet)) {
    Write-Host "Codegen sorunu. codegen.log son satirlari:" -ForegroundColor Red
    Get-Content "$root\codegen.log" -Tail 20
    Copy-Item "$ov.bak" $ov -Force
    return
  }
  cmake --build --preset win-amd64-relwithdebinfo -j 6 *> "$root\build.log"
  if ($LASTEXITCODE -ne 0) {
    Write-Host "Build hatasi. build.log son satirlari:" -ForegroundColor Red
    Get-Content "$root\build.log" -Tail 20
    return
  }
}
Write-Host "Max tur ($Max) doldu." -ForegroundColor Yellow
