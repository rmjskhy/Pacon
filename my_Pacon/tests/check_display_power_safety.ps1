$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$firmware = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw
$plan = Get-Content (Join-Path $root 'POWER_SAVING_PLAN.md') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($pattern in @(
        'DISPLAY_POWER_AWAKE',
        'DISPLAY_POWER_DIMMED',
        'DISPLAY_POWER_SLEEPING',
        'DISPLAY_POWER_WAKING',
        'DISPLAY_SLEEP_OUT_SETTLE_MS\s+120U',
        'display_service_power\(now\)',
        'display_restore_brightness_after_frame\(\)',
        'display_power_inactive\(\)')) {
    if ($firmware -notmatch $pattern) {
        $failures.Add("Missing display power state-machine pattern: $pattern")
    }
}

$sleepStart = $firmware.IndexOf('static esp_err_t display_enter_sleep(void)')
$sleepEnd = $firmware.IndexOf('static void display_request_wake', $sleepStart)
if ($sleepStart -lt 0 -or $sleepEnd -lt 0) {
    $failures.Add('Cannot locate display_enter_sleep()')
} else {
    $sleep = $firmware.Substring($sleepStart, $sleepEnd - $sleepStart)
    $brightnessOff = $sleep.IndexOf('lcd_set_brightness(0)')
    $displayOff = $sleep.IndexOf('esp_lcd_panel_disp_on_off(s_lcd_panel, false)')
    $sleepIn = $sleep.IndexOf('lcd_tx_dcs(0x10')
    if ($brightnessOff -lt 0 -or $displayOff -le $brightnessOff -or $sleepIn -le $displayOff) {
        $failures.Add('Sleep order must be brightness 0 -> DISPOFF -> SLPIN')
    }
}

$wakeStart = $firmware.IndexOf('static void display_request_wake')
$wakeEnd = $firmware.IndexOf('static void display_service_power', $wakeStart)
if ($wakeStart -lt 0 -or $wakeEnd -lt 0) {
    $failures.Add('Cannot locate display_request_wake()')
} else {
    $wake = $firmware.Substring($wakeStart, $wakeEnd - $wakeStart)
    if ($wake -notmatch 'lcd_tx_dcs\(0x11' -or
        $wake -notmatch 'DISPLAY_POWER_WAKING' -or
        $wake -notmatch 'DISPLAY_SLEEP_OUT_SETTLE_MS') {
        $failures.Add('Wake request must send SLPOUT and enter timed WAKING state')
    }
}

$serviceStart = $firmware.IndexOf('static void display_service_power')
$serviceEnd = $firmware.IndexOf('static void display_restore_brightness_after_frame', $serviceStart)
if ($serviceStart -lt 0 -or $serviceEnd -lt 0) {
    $failures.Add('Cannot locate display_service_power()')
} else {
    $service = $firmware.Substring($serviceStart, $serviceEnd - $serviceStart)
    $restore = $service.IndexOf('lcd_restore_runtime_config_after_sleep()')
    $displayOn = $service.IndexOf('esp_lcd_panel_disp_on_off(s_lcd_panel, true)')
    $redraw = $service.IndexOf('display_mark_current_screen_dirty()')
    if ($restore -lt 0 -or $displayOn -le $restore -or $redraw -le $displayOn) {
        $failures.Add('Wake service must restore registers -> DISPON -> request redraw')
    }
}

if ($firmware -match 's_display_(sleeping|dimmed)') {
    $failures.Add('Legacy display sleeping/dimmed booleans must not remain')
}
if ($plan.Length -lt 1000) {
    $failures.Add('Power saving plan is incomplete')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'DISPLAY POWER SAFETY CHECK: PASS'
