$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
if ($main -notmatch '#define WATCH_EDGE_SWIPE_MIN_X\s+10' -or
    $main -notmatch '#define WATCH_EDGE_SWIPE_ZONE\s+150' -or
    $main -notmatch 'const bool inward_edge_flick =[\s\S]*?s_watch_swipe_origin_x <= WATCH_EDGE_SWIPE_ZONE[\s\S]*?dx >= WATCH_EDGE_SWIPE_MIN_X[\s\S]*?s_watch_swipe_origin_x >= LCD_WIDTH - WATCH_EDGE_SWIPE_ZONE[\s\S]*?dx <= -WATCH_EDGE_SWIPE_MIN_X') {
    Write-Host 'WATCH STYLE GESTURE CHECK: FAIL' -ForegroundColor Red
    Write-Host ' - Inward edge flick recognition is missing.' -ForegroundColor Red
    exit 1
}
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($main -notmatch '#define FT3168_REG_GESTURE_ENABLE\s+0xD0' -or
    $main -notmatch '#define FT3168_REG_GESTURE_MASK\s+0xD1' -or
    $main -notmatch '#define FT3168_REG_GESTURE_ID\s+0xD3' -or
    $main -notmatch '#define FT3168_GESTURE_SWIPE_LEFT\s+0x20' -or
    $main -notmatch '#define FT3168_GESTURE_SWIPE_RIGHT\s+0x21') {
    $failures.Add('FT3168 hardware left/right gesture registers or IDs are missing.')
}
if ($main -notmatch 'static void touch_service_watch_gesture\(void\)[\s\S]*?FT3168_REG_GESTURE_MASK[\s\S]*?FT3168_GESTURE_LEFT_RIGHT[\s\S]*?FT3168_REG_GESTURE_ENABLE, 0x01' -or
    $main -notmatch 's_touch_watch_gesture_active && s_ui_screen != UI_SCREEN_WATCH[\s\S]*?FT3168_REG_GESTURE_ENABLE, 0x00') {
    $failures.Add('Hardware gestures are not enabled only on Watch and disabled on page exit.')
}
if ($main -notmatch 'static void touch_poll_watch_gesture\(void\)[\s\S]*?gesture_id == FT3168_GESTURE_SWIPE_LEFT[\s\S]*?gesture_id == FT3168_GESTURE_SWIPE_RIGHT[\s\S]*?s_watch_style = \(uint8_t\)\(1U - s_watch_style\);[\s\S]*?watch_schedule_style_save\(\);') {
    $failures.Add('Hardware left/right gesture IDs do not switch and persist the watch style.')
}
if ($main -notmatch 'if \(s_touch_watch_gesture_active\) \{\s+touch_poll_watch_gesture\(\);\s+return;' -or
    $main -notmatch 'else if \(x < 104 && y < 104\) \{\s+s_ui_screen = UI_SCREEN_HOME;') {
    $failures.Add('Hardware gesture polling does not preserve coordinate-based top-left return.')
}
if ($main -match 'DEBUG-WATCH-GESTURE|DEBUG-WATCH-SWIPE|gesture_probe') {
    $failures.Add('Temporary watch gesture diagnostics remain in production code.')
}
if ($main -notmatch 'static void watch_handle_touch_move\(int x, int y\)') {
    $failures.Add('Watch page has no touch-move handler.')
}
if ($main -notmatch 'watch_handle_touch\(x, y\);\s+\} else \{\s+watch_handle_touch_move\(x, y\);') {
    $failures.Add('Held watch contacts are not routed to the move handler.')
}
if ($main -notmatch '\(!inward_edge_flick && abs\(dx\) < WATCH_SWIPE_MIN_X\) \|\|\s+abs\(dx\) <= abs\(dy\) \+ WATCH_SWIPE_AXIS_MARGIN') {
    $failures.Add('Watch swipe lacks the horizontal distance/direction guard.')
}
if ($main -notmatch 's_watch_style = \(uint8_t\)\(1U - s_watch_style\);') {
    $failures.Add('A qualified swipe does not switch KKD1/KKD2.')
}
if ($main -notmatch 's_watch_style = \(uint8_t\)\(1U - s_watch_style\);\s+watch_schedule_style_save\(\);') {
    $failures.Add('Local watch style changes do not schedule persistence.')
}
if ($main -notmatch '#define WATCH_STYLE_SAVE_IDLE_MS\s+2000' -or
    $main -notmatch 's_watch_style_save_pending = true;[\s\S]*?s_watch_style_save_after = xTaskGetTickCount\(\) \+[\s\S]*?WATCH_STYLE_SAVE_IDLE_MS' -or
    $main -notmatch 'if \(!s_watch_style_save_pending \|\| s_touch_down \|\|[\s\S]*?now - s_watch_style_save_after[\s\S]*?return;[\s\S]*?s_watch_style_save_pending = false;\s+clock_save_preferences\(\);') {
    $failures.Add('Watch style persistence is not coalesced until the swipe burst is idle.')
}
if ($main -notmatch 's_watch_dirty = true;\s+s_watch_last_frame = 0;\s+s_watch_display_seconds = -1;') {
    $failures.Add('Style swipe does not request a deterministic redraw.')
}
if ($main -notmatch 's_watch_swipe_handled = true;\s+block_touch_until_release\(\);') {
    $failures.Add('One contact can switch the watch style more than once.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCH STYLE GESTURE CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'WATCH STYLE GESTURE CHECK: PASS' -ForegroundColor Green
