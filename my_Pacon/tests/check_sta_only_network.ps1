$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$cmakePath = Join-Path $PSScriptRoot '..\main\CMakeLists.txt'
$source = Get-Content -LiteralPath $sourcePath -Raw
$cmake = Get-Content -LiteralPath $cmakePath -Raw

$forbiddenSourcePatterns = @(
    'esp_http_server\.h',
    '\bhttpd_',
    'SKYORB_AP_SSID',
    'SKYORB_AP_PASSWORD',
    's_skyorb_ap_ready',
    'WIFI_EVENT_AP_START',
    'WIFI_EVENT_AP_STOP',
    'WIFI_MODE_APSTA',
    'WIFI_MODE_AP',
    'PACON-Sky'
)

foreach ($pattern in $forbiddenSourcePatterns) {
    if ($source -match $pattern) {
        throw "Formal firmware still contains the retired SoftAP/HTTP path: $pattern"
    }
}

if ($cmake -match '\besp_http_server\b') {
    throw 'Formal firmware still links esp_http_server.'
}
if ($source -notmatch '#include\s+"esp_http_client\.h"') {
    throw 'Radar and IP geolocation must retain the HTTP client.'
}
if ($cmake -notmatch '\besp_http_client\b') {
    throw 'The component manifest must retain esp_http_client.'
}
if ($source -notmatch 'esp_wifi_set_mode\(WIFI_MODE_STA\)') {
    throw 'The formal firmware must retain STA-only Wi-Fi startup.'
}
if ($source -notmatch 'GET WIFI LIST' -or $source -notmatch 'SET WIFI') {
    throw 'BLE Wi-Fi provisioning commands must remain available.'
}

Write-Host 'STA-only network check passed.'
