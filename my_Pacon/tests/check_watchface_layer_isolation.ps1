$ErrorActionPreference = 'Stop'

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$firmwarePath = Join-Path $projectRoot 'main\fluid_pendant.c'
$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$composeMatch = [regex]::Match(
    $firmware,
    'static void watch_compose_canvas\(const clock_time_t \*time\)\s*\{(?<body>.*?)\r?\n\}\r?\n\r?\nstatic void watch_render_frame',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)

if (-not $composeMatch.Success) {
    $failures.Add('Could not locate watch_compose_canvas for layer-isolation checks.')
} else {
    $body = $composeMatch.Groups['body'].Value
    if ($body -notmatch 'watch_compose_kkd2\(time\)') {
        $failures.Add('KKD2 is not routed through its dedicated compositor.')
    }
    if ($body -notmatch 'watch_compose_kkd1\(time\)') {
        $failures.Add('KKD1 is not routed through its own dedicated compositor.')
    }
    if ($body -match 'watch_gear\(' -or $body -match 'watch_draw_time_dials\(') {
        $failures.Add('Top-level watch compositor still mixes face-specific drawing layers.')
    }
}

$kkd1Match = [regex]::Match(
    $firmware,
    'static void watch_compose_kkd1\(const clock_time_t \*time\)\s*\{(?<body>.*?)\r?\n\}\r?\n\r?\nstatic void watch_compose_canvas',
    [System.Text.RegularExpressions.RegexOptions]::Singleline)

if (-not $kkd1Match.Success) {
    $failures.Add('Could not locate a dedicated KKD1 compositor.')
} else {
    $body = $kkd1Match.Groups['body'].Value
    if ($body -notmatch 'memset\(s_lcd_canvas,\s*0,') {
        $failures.Add('KKD1 compositor does not clear the complete frame before drawing.')
    }
    if ($body -match 'watch_gear\(') {
        $failures.Add('KKD1 compositor still overlays procedural gears on APK assets.')
    }
    if ($body -notmatch 'watch_draw_time_dials\(time\)' -or
        $body -notmatch 'watch_draw_character_layer\(\)') {
        $failures.Add('KKD1 compositor is not built from the isolated APK dial and character layers.')
    }
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCHFACE LAYER ISOLATION CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'WATCHFACE LAYER ISOLATION CHECK: PASS' -ForegroundColor Green
