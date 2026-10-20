# Makes a launcher update zip from out\package\FIFA09-PC for the in-launcher updater.
# Run after package.ps1, from the fifa09-pc folder:
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1
#
# Only program files go in (launcher exe + images, bin\*.exe / *.dll, mods\, steam\).
# Never the game folder, launcher.json, logs, achievements or saves.
# Prints the two VPS steps: upload the zip, publish launcher_version.json.
param(
  [string]$Dist = "out\package\FIFA09-PC",
  [string]$NotesTr = "",
  [string]$NotesEn = ""
)
$ErrorActionPreference = "Stop"
$dist = (Resolve-Path $Dist).Path
$exe = Join-Path $dist "FIFA09 Launcher.exe"
if (-not (Test-Path $exe)) { throw "$exe yok. Once package.ps1 calistir." }
$v = [Version](Get-Item $exe).VersionInfo.FileVersion
$ver = "{0}.{1}.{2}" -f $v.Major, $v.Minor, $v.Build

$files = @()
$files += Get-ChildItem $dist -File | Where-Object { $_.Extension -in ".exe", ".jpg", ".png", ".ico" }
$files += Get-ChildItem (Join-Path $dist "bin") -File | Where-Object { $_.Extension -in ".exe", ".dll" }
foreach ($sub in "mods", "steam") {
  $p = Join-Path $dist $sub
  if (Test-Path $p) { $files += Get-ChildItem $p -Recurse -File | Where-Object { $_.Extension -ne ".orig" } }
}

$outDir = Join-Path (Get-Location) "out\release"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$name = "NOBK-FIFA09-$ver.zip"
$zip = Join-Path $outDir $name
if (Test-Path $zip) { Remove-Item $zip }

Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::Open($zip, "Create")
try {
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($dist.Length).TrimStart('\').Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z, $f.FullName, $rel, "Optimal")
  }
} finally { $z.Dispose() }

$sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
$json = [ordered]@{
  version = $ver
  url = "https://nobkonline.com/launcher/$name"
  sha256 = $sha
  notes = [ordered]@{ tr = $NotesTr; en = $NotesEn }
} | ConvertTo-Json -Compress

Write-Host ""
Write-Host "Surum $ver  ->  $zip  ($($files.Count) dosya, $mb MB)" -ForegroundColor Green
Write-Host ""
Write-Host "1) VPS SSH (ilk seferde klasoru ac): mkdir -p /var/www/nobkonline/launcher" -ForegroundColor Cyan
Write-Host ""
Write-Host "2) PC PowerShell (zip'i VPS'e yukle):" -ForegroundColor Cyan
Write-Host "   scp `"$zip`" root@YOUR_SERVER:/var/www/YOUR_SITE/launcher/"
Write-Host ""
Write-Host "3) VPS SSH (yeni surumu yayinla; launcher'lar bir sonraki acilista bildirir):" -ForegroundColor Cyan
Write-Host "cat > /opt/fifa09/launcher_version.json <<'EOF'"
Write-Host $json
Write-Host "EOF"
