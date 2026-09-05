$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$transition = [regex]::Match(
    $main,
    'if \(s_watch_rendered_style != frame_style\) \{(?<body>[\s\S]*?)\n    \}\s+\n    const clock_time_t time')
if (-not $transition.Success) {
    $failures.Add('Watch style transition block could not be located.')
} elseif ($transition.Groups['body'].Value -match 'flush_canvas_rect|memset\(s_lcd_canvas') {
    $failures.Add('Watch style transition still flushes a visible black frame.')
}
if ($main -notmatch '#define\s+WATCH_SWIPE_MIN_X\s+42') {
    $failures.Add('Watch swipe minimum distance is not the tested 42 pixels.')
}
if ($main -notmatch '#define\s+WATCH_SWIPE_AXIS_MARGIN\s+6') {
    $failures.Add('Watch swipe direction tolerance is still too strict.')
}
if ($main -notmatch 'render_at_max_cpu\(watch_render_frame\);\s+if \(s_ui_screen == UI_SCREEN_WATCH\) poll_touch\(\);') {
    $failures.Add('Watch rendering does not resample touch after a full-frame transfer.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCH STYLE TRANSITION QUALITY CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'WATCH STYLE TRANSITION QUALITY CHECK: PASS' -ForegroundColor Green
