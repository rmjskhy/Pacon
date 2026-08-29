$ErrorActionPreference = 'Stop'

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$firmwarePath = Join-Path $projectRoot 'main\fluid_pendant.c'
$cmakePath = Join-Path $projectRoot 'main\CMakeLists.txt'
$androidPath = Join-Path $projectRoot '..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw
$android = Get-Content -LiteralPath $androidPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($firmware -match 'current\.second\s*<\s*2') {
    $failures.Add('Alarm still depends on the fragile first-two-seconds trigger window.')
}
if ($firmware -notmatch 'TEST ALARM') {
    $failures.Add('Firmware has no direct alarm/buzzer test command.')
}
if ($android -notmatch 'TEST ALARM') {
    $failures.Add('Android UI has no direct alarm/buzzer test control.')
}
if ($firmware -notmatch 'kkd2_background_start' -or
    $firmware -notmatch 'kkd2_hour_start' -or
    $firmware -notmatch 'kkd2_minute_start') {
    $failures.Add('Watch style 1 is not backed by independent KKD2 image layers.')
}
if ($cmake -notmatch 'assets/kkd2_background\.rgb565a' -or
    $cmake -notmatch 'assets/kkd2_hour\.rgb565a' -or
    $cmake -notmatch 'assets/kkd2_minute\.rgb565a') {
    $failures.Add('KKD2 watch assets are not embedded in the firmware image.')
}
if ($android -notmatch 'KKD1' -or $android -notmatch 'KKD2') {
    $failures.Add('Android watch style choices do not identify the two distinct watch faces.')
}
if ($firmware -match 's_watch_style\s*\^=\s*1U') {
    $failures.Add('Watch face still toggles style on an ordinary screen touch.')
}

if ($failures.Count -gt 0) {
    Write-Host 'ALARM / WATCHFACE RUNTIME CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'ALARM / WATCHFACE RUNTIME CHECK: PASS' -ForegroundColor Green
