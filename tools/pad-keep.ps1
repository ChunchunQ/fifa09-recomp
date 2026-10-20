# Sticky pad: once a controller answered, a dropped pad (wireless off, battery,
# Bluetooth hiccup, cable) reads as "connected, nothing pressed" instead of
# "not connected". FIFA 09 crashed (call to guest address 0) when the pad
# disappeared in some online menus. Patches the SDK's input_system.cpp by text
# anchors, then build with:  .\tools\apply-patches.ps1
param([string]$Sdk = "$env:USERPROFILE\Desktop\rexglue-sdk")
$ErrorActionPreference = "Stop"
$file = Join-Path $Sdk "src\input\input_system.cpp"
$raw = [IO.File]::ReadAllText($file)
$crlf = $raw.Contains("`r`n")
$s = $raw.Replace("`r`n", "`n")
if ($s.Contains("[online] Sticky pad")) { Write-Host "Sticky pad is already in input_system.cpp" -ForegroundColor Yellow; exit 0 }
$pairs = @()
$pairs += ,@(@'
X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
'@, @'
// [online] Sticky pad: after a controller has answered for a user, losing it
// (wireless off, battery, Bluetooth) reads as "connected, nothing pressed".
// The game never sees a disconnect: FIFA 09 crashed on one in online menus.
namespace {
struct StickyPad {
  bool has_caps = false, has_state = false;
  X_INPUT_CAPABILITIES caps = {};
  uint32_t packet = 0;
};
StickyPad g_sticky_pad[4];
}  // namespace

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
'@)
$pairs += ,@(@'
  assignment_->DevicesForUser(user_index, ids);
  if (ids.empty()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Prefer the pad in hand
'@, @'
  assignment_->DevicesForUser(user_index, ids);
  if (ids.empty()) {
    if (user_index < 4 && g_sticky_pad[user_index].has_caps) {
      *out_caps = g_sticky_pad[user_index].caps;
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Prefer the pad in hand
'@)
$pairs += ,@(@'
  auto* driver = DriverForDevice(chosen);
  if (!driver) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return driver->GetDeviceCapabilities(chosen, flags, out_caps);
}
'@, @'
  auto* driver = DriverForDevice(chosen);
  X_RESULT caps_result = driver ? driver->GetDeviceCapabilities(chosen, flags, out_caps)
                                : X_RESULT(X_ERROR_DEVICE_NOT_CONNECTED);
  if (user_index < 4) {
    if (caps_result == X_ERROR_SUCCESS) {
      g_sticky_pad[user_index].caps = *out_caps;
      g_sticky_pad[user_index].has_caps = true;
    } else if (g_sticky_pad[user_index].has_caps) {
      *out_caps = g_sticky_pad[user_index].caps;
      return X_ERROR_SUCCESS;
    }
  }
  return caps_result;
}
'@)
$pairs += ,@(@'
  if (!any) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;
'@, @'
  if (!any) {
    if (user_index < 4 && g_sticky_pad[user_index].has_state) {
      if (out_state) {
        *out_state = X_INPUT_STATE{};  // nothing pressed, sticks centred
        out_state->packet_number = g_sticky_pad[user_index].packet;
      }
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (user_index < 4) {
    g_sticky_pad[user_index].has_state = true;
    g_sticky_pad[user_index].packet = merged.packet_number;
  }
  if (out_state) {
    *out_state = merged;
  }
  return X_ERROR_SUCCESS;
'@)
$n = 0
foreach ($p in $pairs) {
  $a = $p[0].Replace("`r`n", "`n"); $n++
  $i = $s.IndexOf($a)
  if ($i -lt 0) { throw "anchor $n not found in input_system.cpp (nothing was changed)" }
  if ($s.IndexOf($a, $i + 1) -ge 0) { throw "anchor $n found twice (nothing was changed)" }
}
foreach ($p in $pairs) { $s = $s.Replace($p[0].Replace("`r`n", "`n"), $p[1].Replace("`r`n", "`n")) }
if ($crlf) { $s = $s.Replace("`n", "`r`n") }
Copy-Item $file "$file.before-padkeep" -Force
[IO.File]::WriteAllText($file, $s, (New-Object Text.UTF8Encoding($false)))
Write-Host "Sticky pad added (backup: input_system.cpp.before-padkeep). Now run: .\tools\apply-patches.ps1" -ForegroundColor Green
