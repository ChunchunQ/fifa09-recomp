# Starts the development build with the online test switches on:
# the console reports a LIVE connection, every EA server lookup points to this
# PC (127.0.0.1) and every network call is written to run.log.
# Start tools\blaze-listen.ps1 in another window first.
#
#   powershell -ExecutionPolicy Bypass -File tools\online-test.ps1
param(
  [string]$GameData = (Join-Path $PSScriptRoot "..\assets"),
  [string]$Server = "127.0.0.1",
  [string]$LogLevel = "info",
  [string]$Disable = "",       # e.g. "signin,priv" keeps those parts offline (debugging)
  [string]$Gamertag = "User",
  [string]$Xuid = "",          # hex; give each test instance its own (1, 2, ...)
  [string]$LogFile = "run.log",
  [string]$Relay = "",         # match relay "host:port" (e.g. your-server:3658)
  [int]$Instance = 0,          # two games on ONE PC: 1 for the first, 2 for the second
                               # (their game ports are shifted so both can run)
  [switch]$Fullscreen,         # default: 1920x1080 window, handy for two instances
  [switch]$Quiet               # no per-packet/import logging (faster; use for real play tests)
)
$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "..\out\build\win-amd64-relwithdebinfo"
$exe = Join-Path $build "fifa09.exe"
if (-not (Test-Path $exe)) { throw "fifa09.exe not found in $build" }
$GameData = (Resolve-Path $GameData).Path

$gameArgs = @(
  "--game_data_root", $GameData,
  "--gpu_plugin", "xenos",
  "--readback_resolve", "full",
  "--edram_ownership_no_wrap=true",
  "--resolution_scale=2",
  "--anisotropic_override=5",
  "--online_enabled=true",
  "--online_server_ip=$Server",
  "--net_packet_log=$(if ($Quiet) { 'false' } else { 'true' })",
  "--trace_imports=$(if ($Quiet) { 'false' } else { 'true' })",
  "--log_level", $LogLevel,
  "--log_max_file_size_mb=100",
  "--log_file", $LogFile,
  "--user_gamertag=$Gamertag"
)
if ($Fullscreen) { $gameArgs += "--fullscreen" }
else { $gameArgs += @("--no-fullscreen", "--window_width", "1920", "--window_height", "1080") }
if ($Xuid) { $gameArgs += "--user_xuid=$Xuid" }
$gameArgs += "--user_country=99"
# An empty "--name=" would swallow the next argument, so only pass it when set.
if ($Disable) { $gameArgs += "--online_disable=$Disable" }
if ($Relay) { $gameArgs += "--online_relay=$Relay" }
if ($Instance -eq 1) { $gameArgs += @("--online_port_offset=0", "--online_peer_port_offset=1") }
elseif ($Instance -eq 2) { $gameArgs += @("--online_port_offset=1", "--online_peer_port_offset=0") }
Push-Location $build
try { & $exe @gameArgs } finally { Pop-Location }
Write-Host "Done. Send me out\build\win-amd64-relwithdebinfo\run.log and tools\blaze-capture.log" -ForegroundColor Green
