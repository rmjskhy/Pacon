$ErrorActionPreference = 'Stop'

$firmwarePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$androidPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$android = Get-Content -LiteralPath $androidPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($firmware -notmatch 'strcasecmp\(text,\s*"GET RADAR"\)') {
    $failures.Add('Firmware has no compact GET RADAR command.')
}
if (!$firmware.Contains('\"location_valid\":%s,\"location_auto\":%s,\"location_state\":\"%s\",\"location_error\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,\"range\":%u')) {
    $failures.Add('GET RADAR does not expose validity, source, coordinates and range together.')
}
if ($android -notmatch 'sendCommandAndWait\("GET RADAR"') {
    $failures.Add('Android still reads radar coordinates from the oversized GET SETTINGS response.')
}
if ($android -notmatch 'parseJsonBoolean\(response,\s*"location_valid"') {
    $failures.Add('Android does not distinguish an unset location from coordinate parsing failure.')
}
if ($android -notmatch 'parseJsonBoolean\(response,\s*"location_auto"') {
    $failures.Add('Android does not display whether location came from IP or manual input.')
}
if (!$firmware.Contains('\"location_state\":\"%s\"') -or
    !$firmware.Contains('\"location_error\":\"%s\"')) {
    $failures.Add('GET RADAR cannot distinguish pending, failed, offline and unset automatic location states.')
}
if ($android -notmatch 'pollAutoLocation') {
    $failures.Add('Android does not wait for the asynchronous automatic-location request to finish.')
}
if ($android -notmatch 'location_state' -or $android -notmatch 'location_error') {
    $failures.Add('Android cannot show why automatic location failed.')
}

if ($failures.Count -gt 0) {
    Write-Host 'RADAR SETTINGS BLE CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'RADAR SETTINGS BLE CHECK: PASS' -ForegroundColor Green
