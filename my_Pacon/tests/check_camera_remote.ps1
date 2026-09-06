$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ble = Get-Content (Join-Path $root 'main\ble_pacon.c') -Raw
$header = Get-Content (Join-Path $root 'main\ble_pacon.h') -Raw
$fluid = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw
$android = Get-Content (Join-Path (Split-Path $root) 'PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java') -Raw
$sdkconfig = Get-Content (Join-Path $root 'sdkconfig.defaults') -Raw

$checks = @(
    @($ble, 'BLE_UUID16_INIT\(0x1812\)', 'HID service UUID'),
    @($ble, '0x09, 0xE9', 'Consumer Control Volume Increment usage'),
    @($ble, 's_hid_report_value_handle', 'HID report value handle'),
    @($ble, 'ble_pacon_is_camera_remote_ready', 'HID subscription readiness'),
    @($ble, 'ble_npl_eventq_put\(nimble_port_get_dflt_eventq\(\)', 'camera shutter dispatch on NimBLE host queue'),
    @($ble, 'ble_npl_callout_init', 'camera report release on NimBLE host queue'),
    @($ble, 'BLE_GATT_CHR_F_READ_ENC', 'encrypted HID reads'),
    @($ble, 'BLE_GATT_CHR_F_WRITE_ENC', 'encrypted HID writes'),
    @($ble, 'ble_hs_cfg\.sm_bonding\s*=\s*1', 'bonding-enabled HID security'),
    @($ble, 'ble_store_config_init\(\)', 'persistent bond store'),
    @($ble, 'BLE_GAP_EVENT_ENC_CHANGE', 'pairing result diagnostics'),
    @($header, 'ble_pacon_camera_shutter', 'camera shutter public API'),
    @($fluid, 'CAMERA SHUTTER', 'camera shutter command'),
    @($fluid, 'SET CAMERA REMOTE', 'camera remote safety switch'),
    @($fluid, 'UI_SCREEN_CAMERA', 'dedicated on-device camera screen'),
    @($fluid, 'camera_render_frame', 'camera page renderer'),
    @($fluid, 'camera_handle_touch', 'camera page shutter interaction'),
    @($fluid, 'PHONE CONNECTED', 'camera connection ring status'),
    @($fluid, 'CAPTURED', 'camera capture feedback animation'),
    @($fluid, 'const int icon_radius = 39', 'uniform launcher icon radius'),
    @($fluid, 'home_in_circle\(x, y, 333, 385, 44\)', 'camera launcher touch target'),
    @($android, 'cameraButton', 'Android camera command button'),
    @($sdkconfig, 'CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2', 'parallel HID and app connections')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "Camera remote check failed: $($check[2])"
    }
}

if ($ble -match 'esp_hidd_dev_init|nimble_hidd') {
    throw 'Camera remote must share the existing NimBLE host; do not start a second HID host.'
}

if ($ble -match 'xTaskCreate\(camera_shutter_task') {
    throw 'Camera HID notifications must not run from a separate FreeRTOS task.'
}

if ($ble -notmatch 'BLE_GAP_EVENT_SUBSCRIBE' -or
    $ble -notmatch 's_hid_notify_handles') {
    throw 'Camera remote must track HID notification subscriptions per connection.'
}

if ($fluid -match 'settings_text\("Camera shutter"' -or
    $fluid -match 'Settings: camera shutter requested') {
    throw 'Camera shutter must be a dedicated app, not a Settings row.'
}

if ($fluid -match 'watch_radius = 34|settings_size = 54|camera_r2 <= 32 \* 32') {
    throw 'Launcher icons must not regress to mixed visible sizes.'
}

if ($ble -notmatch '(?s)s_hid_report_uuid.*?BLE_GATT_CHR_F_READ_ENC' -or
    $ble -notmatch '(?s)s_hid_control_point_uuid.*?BLE_GATT_CHR_F_WRITE_ENC') {
    throw 'HID report and control characteristics must require an encrypted bonded link.'
}

Write-Host 'Camera remote source checks passed.'
