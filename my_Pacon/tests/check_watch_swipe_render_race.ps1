$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$render = [regex]::Match(
    $main,
    'static void watch_render_frame\(void\)(?<body>[\s\S]*?)\n\}\s+\nstatic void watch_enter')
if (-not $render.Success) {
    $failures.Add('Watch render function could not be located.')
} else {
    $body = $render.Groups['body'].Value
    if ($body -notmatch 'const uint8_t frame_style = s_watch_style;') {
        $failures.Add('Watch frame does not snapshot its style before composition.')
    }
    if ($body -notmatch 'watch_compose_canvas\(&display_time, frame_style\);') {
        $failures.Add('Watch composition can change style midway through one frame.')
    }
    if ($body -notmatch 's_watch_dirty = catch_up_pending \|\| s_watch_style != frame_style;') {
        $failures.Add('A swipe captured during DMA is overwritten at frame completion.')
    }
}
if ($main -notmatch 'static void watch_compose_canvas\(const clock_time_t \*time, uint8_t style\)') {
    $failures.Add('Watch compositor has no explicit per-frame style input.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCH SWIPE RENDER RACE CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'WATCH SWIPE RENDER RACE CHECK: PASS' -ForegroundColor Green
