$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$firmware = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($pattern in @(
        '#define BATTERY_MAX_AWAKE_MS\s+300000U',
        '#define WATCH_BURNIN_SHIFT_MS\s+90000U',
        '#define OUO_BURNIN_SHIFT_MS\s+90000U')) {
    if ($firmware -notmatch $pattern) {
        $failures.Add("Missing static display protection constant: $pattern")
    }
}

if ($firmware -notmatch 'uint32_t\s+dim_timeout_ms\s*=\s*configured_timeout_ms' -or
    $firmware -notmatch 'configured_timeout_ms\s*\+\s*DISPLAY_SLEEP_AFTER_DIM_MS' -or
    $firmware -notmatch '!s_vbus_present &&\s+\(sleep_timeout_ms == 0U \|\| sleep_timeout_ms > BATTERY_MAX_AWAKE_MS\)') {
    $failures.Add('Persisted timeout handling or the battery-powered five-minute cap is missing.')
}

$usbLoop = [regex]::Match(
    $firmware,
    'static void run_usb_msc_screen_loop\(void\)(?<body>[\s\S]*?)\n\}\s+\n\s*void app_main')
if (-not $usbLoop.Success -or
    $usbLoop.Groups['body'].Value -notmatch 'display_update_idle\(now\)' -or
    $usbLoop.Groups['body'].Value -notmatch 'display_service_power\(now\)' -or
    $usbLoop.Groups['body'].Value -notmatch 'render_at_max_cpu\(render_usb_disk_frame\)' -or
    $usbLoop.Groups['body'].Value -notmatch 'display_restore_brightness_after_frame\(\)') {
    $failures.Add('Dedicated USB MSC loop does not sleep, wake-redraw and restore brightness.')
}
if ($firmware -notmatch 'if \(display_power_inactive\(\)\) \{[\s\S]*?display_note_activity\(\);[\s\S]*?s_usb_msc_exit_touch_down = true;[\s\S]*?return;') {
    $failures.Add('First USB touch is not reserved exclusively for display wake.')
}

if ($firmware -notmatch 'static void display_update_feature_burnin_offsets\(TickType_t now\)' -or
    $firmware -notmatch 's_watch_burnin_shift_x = kWatchOffsets' -or
    $firmware -notmatch 's_ouo_burnin_shift_x = kOuoOffsets' -or
    $firmware -notmatch 'display_update_feature_burnin_offsets\(now\)') {
    $failures.Add('Watch/OuO periodic burn-in offset service is incomplete.')
}
if ($firmware -notmatch 's_ouo_gaze_x \+ s_ouo_shake_x \+ s_ouo_burnin_shift_x' -or
    $firmware -notmatch 's_ouo_gaze_y \+ s_ouo_shake_y \+ s_ouo_burnin_shift_y') {
    $failures.Add('OuO burn-in drift is not applied to the complete face geometry.')
}
if ($firmware -notmatch 'static void watch_apply_burnin_shift\(void\)[\s\S]*?memmove\(' -or
    $firmware -notmatch 'watch_compose_canvas\(&display_time, frame_style\);\s+watch_apply_burnin_shift\(\);') {
    $failures.Add('Watch burn-in drift is not applied to the completed watch canvas.')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'STATIC DISPLAY PROTECTION CHECK: PASS'
