$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\ble_pacon.c'
$commandPath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$source = Get-Content -LiteralPath $sourcePath -Raw
$commands = Get-Content -LiteralPath $commandPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($source -match 'ble_gatts_notify_custom\([^;]+;\s*\r?\n\s*if\s*\(rc\s*!=\s*0\)\s*os_mbuf_free_chain') {
    $failures.Add('notify_response double-frees an mbuf consumed by ble_gatts_notify_custom().')
}

if ($source -notmatch 'ble_att_mtu\((s_connection_handle|conn_handle)\)') {
    $failures.Add('Notifications are not bounded by the negotiated ATT MTU.')
}

$settingsMatch = [regex]::Match(
    $commands,
    '(?s)if\s*\(strcasecmp\(text,\s*"GET SETTINGS"\)\s*==\s*0\)\s*\{(?<body>.*?)\n\s*\}')
if (-not $settingsMatch.Success -or
    $settingsMatch.Groups['body'].Value -notmatch '\\"brightness\\"' -or
    $settingsMatch.Groups['body'].Value -notmatch '\\"screen_timeout\\"') {
    $failures.Add('GET SETTINGS is not a dedicated compact response.')
}

if ($commands -match 'strcasecmp\(text,\s*"GET STATUS"\)\s*==\s*0\s*\|\|\s*strcasecmp\(text,\s*"GET SETTINGS"\)') {
    $failures.Add('GET SETTINGS still shares the oversized GET STATUS response.')
}

if ($failures.Count -gt 0) {
    Write-Host 'BLE NOTIFICATION SAFETY CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'BLE NOTIFICATION SAFETY CHECK: PASS' -ForegroundColor Green
