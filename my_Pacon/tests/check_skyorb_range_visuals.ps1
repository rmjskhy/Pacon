$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$source = Get-Content -LiteralPath $sourcePath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($source -notmatch '5\.0f,\s*10\.0f,\s*15\.0f,\s*25\.0f,\s*35\.0f,\s*50\.0f') {
    $failures.Add('The physical radar ranges do not match the 5/10/15/25/35/50 km choices.')
}

if ($source -notmatch 'SKYORB_RANGE_COUNT') {
    $failures.Add('Range rotation and validation do not use a shared range count.')
}

if ($source -notmatch 'demo_distance_km\[\]') {
    $failures.Add('Demo aircraft are not defined by physical distance in kilometres.')
}

if ($source -notmatch 'demo_distance_km\[index\]\s*\*\s*\(float\)radar_radius\s*/\s*outer_km') {
    $failures.Add('Demo aircraft positions are not scaled by the selected outer range.')
}

if ($source -match 'const float demo_radius\[\]') {
    $failures.Add('Legacy normalized demo radii still make every range look identical.')
}

if ($source -notmatch 'RADIUS %u KM') {
    $failures.Add('The radar has no prominent radius readout.')
}

if ($source -notmatch 'ring_label') {
    $failures.Add('Radar rings have no distance label tied to the selected range.')
}

if ($failures.Count -gt 0) {
    Write-Host 'SKYORB RANGE VISUAL CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'SKYORB RANGE VISUAL CHECK: PASS' -ForegroundColor Green
