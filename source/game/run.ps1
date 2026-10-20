# Dev build start with the launcher's settings: 180 FPS, readback fast, fullscreen.
#   .\run.ps1                 -> render scale 2 (2560x1440), bilinear
#   .\run.ps1 -Scale 1 -Fsr   -> 1280x720 upscaled to the screen with FSR (launcher's 720p / 1080p)
#   .\run.ps1 -Scale 1 -Effect fsr3  -> experimental temporal FSR 3 (also fsr2, cas)
#   .\run.ps1 -Window         -> 2560x1440 window
#   .\run.ps1 -Fps 0          -> no vsync / no FPS cap
#   .\run.ps1 -Readback full  -> old behaviour
param([int]$Fps = 180, [int]$Scale = 2, [string]$Readback = "fast", [switch]$Window, [switch]$Fsr,
      [ValidateSet("", "bilinear", "fsr", "fsr2", "fsr3", "cas")][string]$Effect = "",
      [string[]]$Extra = @(), [switch]$Wait)
$run = Join-Path $PSScriptRoot "out\build\win-amd64-relwithdebinfo"
$assets = Join-Path $PSScriptRoot "assets"
Remove-Item (Join-Path $run "run.log") -ErrorAction SilentlyContinue
$sync = if ($Fps -le 0) { @("--no-vsync") } else { @("--vsync", "--video_mode_refresh_rate=$Fps") }
$screen = if ($Window) { @("--no-fullscreen", "--window_width=2560", "--window_height=1440") } else { @("--fullscreen") }
# One --present_effect only: the SDK refuses a repeated option and then drops the whole command line.
$pe = if ($Effect) { $Effect } elseif ($Fsr) { "fsr" } else { "bilinear" }
$args0 = @("--game_data_root=$assets", "--gpu_plugin=xenos", "--graphics_api=d3d12",
           "--readback_resolve=$Readback", "--resolution_scale=$Scale", "--present_effect=$pe",
           "--anisotropic_override=5", "--edram_ownership_no_wrap=true") + $screen + $sync +
         @("--log_level=info", "--log_file=run.log")
Start-Process "$run\fifa09.exe" -WorkingDirectory $run -ArgumentList ($args0 + $Extra) -Wait:$Wait
