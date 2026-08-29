$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$source = Get-Content -LiteralPath $sourcePath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($required in @(
    'UI_SCREEN_WATCH',
    'ADDR_PCF85063',
    'watch_read_time',
    'watch_compose_canvas',
    'watch_draw_character_layer',
    'watch_draw_time_dials',
    'WATCH_MINUTE_POINTER_ANGLE',
    'WATCH_HOUR_POINTER_ANGLE',
    'watch_render_frame',
    'Apps: entered mechanical Watch'
)) {
    if (!$source.Contains($required)) {
        $failures.Add("Missing watch-face element: $required")
    }
}

if ($source -match 'watch_hand\(center_x,\s*center_y,\s*hour_angle' -or
    $source -match 'watch_hand\(center_x,\s*center_y,\s*minute_angle') {
    $failures.Add('Watch still draws conventional hour/minute hands instead of using the character arms as pointers.')
}

if (!$source.Contains('watch_advance_display_time')) {
    $failures.Add('Watch second ring has no one-step display-time advance helper.')
}
if ($source -match 'second_(?:angle|rotation)\s*=\s*\(float\)time->second') {
    $failures.Add('Raw RTC seconds still drive the visible ring and can skip multiple marks.')
}

if ($source -notmatch 'fill_canvas_round_rect\(358,\s*screen_y,\s*407') {
    $failures.Add('The main Settings switch does not use a rounded outer track.')
}
if ($source -notmatch 'fill_canvas_round_rect\(348,\s*120,\s*407,\s*153') {
    $failures.Add('The Wi-Fi page switch does not use a rounded outer track.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCHFACE / SWITCH CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'WATCHFACE / SWITCH CHECK: PASS' -ForegroundColor Green
