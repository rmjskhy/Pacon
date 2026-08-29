$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$ble = Get-Content (Join-Path $PSScriptRoot '..\main\ble_pacon.c') -Raw
foreach ($pattern in @(
    'settings_load_preferences\(\);\s+clock_load_preferences\(\);\s+feature_load_preferences\(\);',
    's_fluid_shape = FLUID_SHAPE_SIMPLE;\s+fluid_queue_preferences\(\);',
    's_fluid_shape = FLUID_SHAPE_BLOCKS;\s+fluid_queue_preferences\(\);',
    's_fluid_shape = FLUID_SHAPE_MATRIX;\s+fluid_queue_preferences\(\);',
    'set_custom_palette\(hue, saturation\);\s+fluid_queue_preferences\(\);',
    'if \(fluid_save_preferences\(\)\) \{\s+s_fluid_preferences_pending = false;',
    's_ouo_preferred_mood = \(uint8_t\)s_ouo_mood;\s+ouo_save_preferences\(\);',
    's_ouo_auto_expressions = !s_ouo_auto_expressions;\s+ouo_save_preferences\(\);',
    's_ouo_tilt_reactions = !s_ouo_tilt_reactions;\s+ouo_save_preferences\(\);',
    'pacon_save_switch\("wifi_on", false\)', 'pacon_save_switch\("wifi_on", true\)',
    'pacon_load_switch\("wifi_on", false\)', 'pacon_load_switch\("wifi_join", false\)')) {
    if ($main -notmatch $pattern) { throw "Missing preferences integration: $pattern" }
}
foreach ($pattern in @('pacon_load_switch\("ble_on", true\)',
    'pacon_save_switch\("ble_on", enabled\)')) {
    if ($ble -notmatch $pattern) { throw "Missing Bluetooth preference: $pattern" }
}
if ($ble -match 'pacon_(load|save)_switch\("camera_on"') {
    throw 'Camera diagnostic enable flag must not be persisted'
}
if ($ble -notmatch 'static bool s_camera_remote_enabled = true;') {
    throw 'Camera remote must default to enabled after reboot'
}
$cameraSetter = [regex]::Match($ble, 'esp_err_t ble_pacon_set_camera_remote_enabled\(bool enabled\)\s*\{([^}]+)\}').Groups[1].Value
if ($cameraSetter -notmatch 's_camera_remote_enabled = enabled;' -or $cameraSetter -notmatch 'return ESP_OK;' -or
    $cameraSetter -match 'nvs_|pacon_save_') {
    throw 'Camera diagnostic switch must remain a RAM-only toggle'
}
if ($ble -match 'nvs_flash_erase\(') { throw 'Bluetooth startup must never erase all user preferences' }
if ($main.IndexOf('pacon_load_switch("wifi_on", false)') -lt $main.IndexOf('init_home_external_media())')) {
    throw 'Restore networking only after display/media allocations'
}
& 'C:\Users\28518\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' (Join-Path $PSScriptRoot 'preferences_roundtrip_harness.js')
if ($LASTEXITCODE -ne 0) { throw 'Preference roundtrip test failed' }
Write-Host 'Feature preference integration checks passed.'
