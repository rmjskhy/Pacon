$ErrorActionPreference = 'Stop'

$firmwarePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$androidPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$cropPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\CropImageView.java'
$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$android = Get-Content -LiteralPath $androidPath -Raw
$crop = Get-Content -LiteralPath $cropPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$checks = @{
    'Firmware status omits the screen timeout.' = $firmware -match 'screen_timeout'
    'Firmware screen-timeout command is missing.' = $firmware -match 'SET SCREEN TIMEOUT 0\|15\|30\|60\|120\|300'
    'Firmware does not persist the screen timeout.' = $firmware -match 'nvs_set_u16\(nvs, "sleep_s"'
    'Firmware does not use the App timeout as the dim threshold.' = $firmware -match 'uint32_t\s+dim_timeout_ms\s*=\s*configured_timeout_ms;'
    'Firmware does not keep a 15-second dim-to-sleep grace period.' = $firmware -match '#define\s+DISPLAY_SLEEP_AFTER_DIM_MS\s+15000U[\s\S]{0,70000}configured_timeout_ms\s*\+\s*DISPLAY_SLEEP_AFTER_DIM_MS'
    'Firmware no longer turns the panel off at idle.' = $firmware -match 'esp_lcd_panel_disp_on_off\(s_lcd_panel, false\)'
    'Android screen-timeout selector is missing.' = $android -match 'screenTimeoutSpinner'
    'Android does not send the screen-timeout command.' = $android -match 'SET SCREEN TIMEOUT '
    'Android ordinary-image flow does not open a crop preview.' = $android -match 'showCropDialog\('
    'Crop preview does not support pinch zoom.' = $crop -match 'ScaleGestureDetector'
    'Crop preview does not support dragging.' = $crop -match 'offsetX \+= x - lastX'
    'Crop preview does not render a native-size result.' = $crop -match 'createCroppedBitmap\(int outputWidth, int outputHeight\)'
}

if ($firmware -match 'DISPLAY_DIM_TIMEOUT_MS') {
    $failures.Add('Firmware still uses a fixed dim timeout instead of the App setting.')
}

foreach ($entry in $checks.GetEnumerator()) {
    if (-not $entry.Value) { $failures.Add($entry.Key) }
}

if ($failures.Count -gt 0) {
    Write-Host 'DISPLAY TIMEOUT / CROP CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'DISPLAY TIMEOUT / CROP CHECK: PASS' -ForegroundColor Green
