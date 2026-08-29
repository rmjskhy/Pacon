$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$source = Get-Content -LiteralPath $sourcePath -Raw

function Require-Pattern([string]$pattern, [string]$message) {
    if ($source -notmatch $pattern) {
        throw $message
    }
}

Require-Pattern 'static bool wifi_request_profile_connection\s*\(' `
    'Missing the shared Wi-Fi profile switch sequencer.'
Require-Pattern 's_wifi_connect_after_scan\s*=\s*s_skyorb_wifi_enabled[\s\S]*?s_wifi_scan_requested\s*=\s*s_skyorb_wifi_enabled' `
    'A profile switch must defer connection until a fresh scan completes.'
Require-Pattern 'if \(s_wifi_connect_after_scan\)[\s\S]*?s_wifi_connect_requested\s*=\s*true' `
    'The network task must promote scan completion into a connect request.'

$callCount = ([regex]::Matches($source, 'wifi_request_profile_connection\s*\(')).Count
if ($callCount -lt 3) {
    throw "Expected the shared sequencer definition plus board and BLE callers; found $callCount occurrences."
}

Write-Host 'Wi-Fi switch sequence check passed.'
