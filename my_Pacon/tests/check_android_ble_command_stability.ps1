$ErrorActionPreference = 'Stop'

$androidPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$android = Get-Content -LiteralPath $androidPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$connectMatch = [regex]::Match(
    $android,
    '(?s)private void connectSelected\(\)\s*\{(?<body>.*?)\n\s*private void disconnect\(\)')
if (-not $connectMatch.Success -or
    $connectMatch.Groups['body'].Value -notmatch '\bstopScan\(\)') {
    $failures.Add('BLE scan is not stopped before opening the GATT connection.')
}

$writeMatch = [regex]::Match(
    $android,
    '(?s)private void writeCommand\(String command\)\s*\{(?<body>.*?)\n\s*private boolean writeCommandInternal')
if (-not $writeMatch.Success -or
    $writeMatch.Groups['body'].Value -notmatch 'mediaExecutor\.execute') {
    $failures.Add('Interactive commands bypass the single BLE control-command executor.')
}

if ($android -notmatch 'AUTO_MEDIA_REFRESH_DELAY_MS') {
    $failures.Add('Automatic media refresh has no named post-CCCD settling delay.')
}

if ($android -notmatch 'AUTO_MEDIA_REFRESH_ON_CONNECT\s*=\s*false') {
    $failures.Add('Current A/B diagnostic build must leave the new GATT link idle.')
}

if ($android -notmatch 'expectedDeleteResponse' -or
    $android -notmatch 'expectedDeleteResponse\.equals\(response\)') {
    $failures.Add('Android must match the deleted filename, not only the generic response prefix.')
}

if ($android -notmatch 'setPreferredPhy\(BluetoothDevice\.PHY_LE_1M_MASK,\s*BluetoothDevice\.PHY_LE_1M_MASK') {
    $failures.Add('PACON connection does not explicitly retain the robust BLE 1M PHY.')
}

if ($failures.Count -gt 0) {
    Write-Host 'ANDROID BLE COMMAND STABILITY CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'ANDROID BLE COMMAND STABILITY CHECK: PASS' -ForegroundColor Green
