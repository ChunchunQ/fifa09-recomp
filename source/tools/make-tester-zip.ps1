# Makes the zip for testers from out\package\FIFA09-PC (run package.ps1 first).
#   powershell -ExecutionPolicy Bypass -File tools\make-tester-zip.ps1
# Left out: the game folder (players use their own copy), launcher.json (holds YOUR
# login token and settings), logs, achievements progress, *.orig backups.
param([string]$Dist = "out\package\FIFA09-PC")
$ErrorActionPreference = "Stop"
$dist = (Resolve-Path $Dist).Path.TrimEnd('\')
$exe = Join-Path $dist "FIFA09 Launcher.exe"
if (-not (Test-Path $exe)) { throw "$exe yok. Once package.ps1 calistir." }
$v = [Version](Get-Item $exe).VersionInfo.FileVersion
$ver = "{0}.{1}.{2}" -f $v.Major, $v.Minor, $v.Build
# NOBK mods the launcher applies itself (ad boards, "NOBK.Online" texts): copy them
# from the project's mods folder into the package if package.ps1 did not.
foreach ($m in "nobk_adboards", "nobk_text") {
  $to = Join-Path $dist "mods\$m"
  if (-not (Test-Path $to) -and (Test-Path "mods\$m")) { Copy-Item "mods\$m" $to -Recurse; Write-Host "  + mods\$m" }
  if (-not (Test-Path $to)) { Write-Host "UYARI: mods\$m yok, testerlar bu modu almayacak." -ForegroundColor Yellow }
}
# Visual C++ runtime next to the game (app-local copy, allowed by Microsoft), so
# testers without (or with an old) "VC++ 2015-2022 x64 Redistributable" can start it.
foreach ($dll in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "concrt140.dll") {
  $src = Join-Path $env:SystemRoot "System32\$dll"
  $dst = Join-Path $dist "bin\$dll"
  if ((Test-Path $src) -and -not (Test-Path $dst)) { Copy-Item $src $dst; Write-Host "  + bin\$dll" }
}
$game = "Game"
try { $game = ((Get-Content (Join-Path $dist "launcher.json") -Raw | ConvertFrom-Json).GameFolder) } catch { }

function Skip($rel) {
  if ($rel -ieq "launcher.json" -or $rel -ieq "update.log") { return $true }
  if ($rel -like "Game\*" -or ($game -and -not [IO.Path]::IsPathRooted($game) -and $rel -like "$game\*")) { return $true }
  if ($rel -like "*.log" -or $rel -like "*.orig" -or $rel -like "*.bak*") { return $true }
  if ($rel -like "bin\achievements\*") { return $true }
  # saves, profiles and caches made by playing (yours, not for testers)
  foreach ($d in "content", "cache", "cache0", "cache1", "shader_cache", "shaders", "profiles", "saves", "user") {
    if ($rel -like "bin\$d\*") { return $true }
  }
  return $false
}

$files = Get-ChildItem $dist -Recurse -File | ForEach-Object {
  $rel = $_.FullName.Substring($dist.Length + 1).Replace("/", "\")
  if (-not (Skip $rel)) { [pscustomobject]@{ File = $_; Rel = $rel } }
}
$big = $files | Where-Object { $_.File.Length -gt 200MB }
if ($big) { throw ("Cok buyuk dosya (oyun dosyasi olabilir): " + ($big.Rel -join ", ")) }

$out = Join-Path (Get-Location) "out\release"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$zip = Join-Path $out "NOBK-FIFA09-$ver-test.zip"
if (Test-Path $zip) { Remove-Item $zip }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::Open($zip, "Create")
try {
  foreach ($f in $files) {
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $f.File.FullName, "FIFA09-NOBK/" + $f.Rel.Replace('\', '/'), "Optimal")
  }
  $readme = @"
NOBK Online - FIFA 09 (PC) test surumu $ver

1) Bu klasoru istedigin yere cikar (ornek: C:\Oyunlar\FIFA09-NOBK). Masaustu/Program Files disi bir yer daha iyi.
2) "FIFA09 Launcher.exe" ac. Dosyalar sekmesinde kendi FIFA 09 (Xbox 360) oyun klasorunu sec
   (icinde default.xex ve data0.big olan klasor), "Oyun dosyalarini dogrula" ile kontrol et.
3) https://nobkonline.com/kayit/ adresinden hesap ac.
4) Launcher'da Online sekmesinden ayni kullanici adi ve sifreyle giris yap, OYNA'ya bas.
5) Oyunda EA Sports Online menusune gir ve dereceli ya da dostluk maci ara.

Sorun olursa bin\run.log dosyasini gonder. Oyun dosyasi dagitilmaz; kendi kopyani kullan.
"@
  $e = $z.CreateEntry("FIFA09-NOBK/BENIOKU.txt")
  $w = New-Object IO.StreamWriter($e.Open(), (New-Object Text.UTF8Encoding($true)))
  $w.Write($readme.Replace("`n", "`r`n")); $w.Close()
} finally { $z.Dispose() }

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "Hazir: $zip ($($files.Count) dosya, $mb MB)" -ForegroundColor Green
Write-Host "Icindekiler (kendi kayitlarin / ayarlarin olmadigini kontrol et):" -ForegroundColor Cyan
$files | ForEach-Object { $p = $_.Rel -split '\\'; if ($p.Count -gt 2 -and $p[0] -ieq "bin") { "bin\" + $p[1] } else { $p[0] } } | Group-Object | ForEach-Object { "  {0,-28} {1} dosya" -f $_.Name, $_.Count }
