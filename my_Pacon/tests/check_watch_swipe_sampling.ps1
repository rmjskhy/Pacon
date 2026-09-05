$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($main -notmatch 'const uint8_t touch_event = \(point\[0\] >> 6\) & 0x03U;') {
    $failures.Add('FT3168 primary-contact event bits are not decoded.')
}
if ($main -notmatch 's_ui_screen == UI_SCREEN_WATCH && touch_event == 0U &&\s+s_touch_blocked_until_release') {
    $failures.Add('A fresh watch DOWN cannot recover when the release frame was missed.')
}
if ($main -notmatch 's_touch_blocked_until_release = false;\s+s_touch_down = false;\s+s_watch_swipe_handled = false;') {
    $failures.Add('Fresh watch DOWN does not rearm the complete gesture state.')
}
$flush = [regex]::Match(
    $main,
    'static bool flush_canvas_rect\(const dirty_rect_t \*rect\)(?<body>[\s\S]*?)\n\}\s+\n/\* --------------------------------------------------------------------------')
if (-not $flush.Success -or
    $flush.Groups['body'].Value -notmatch 'esp_lcd_panel_draw_bitmap[\s\S]*?if \(s_ui_screen == UI_SCREEN_WATCH\) poll_touch\(\);') {
    $failures.Add('Watch touch is not sampled between full-frame DMA stripes.')
}
if ($main -notmatch '#define WATCH_TOUCH_POLL_PERIOD_MS\s+10') {
    $failures.Add('Watch touch polling is not configured for 100 Hz.')
}
if ($main -notmatch 's_ui_screen == UI_SCREEN_WATCH \?\s+WATCH_TOUCH_POLL_PERIOD_MS') {
    $failures.Add('The watch page does not select its dedicated touch polling period.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCH SWIPE SAMPLING CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'WATCH SWIPE SAMPLING CHECK: PASS' -ForegroundColor Green
