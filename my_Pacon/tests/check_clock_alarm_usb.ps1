$ErrorActionPreference = 'Stop'

$firmwarePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$androidPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$android = Get-Content -LiteralPath $androidPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

$firmwareChecks = @{
    'RTC read support is missing.' = 'clock_read_rtc\s*\('
    'RTC write support is missing.' = 'clock_write_rtc\s*\('
    'Clock query command is missing.' = 'GET CLOCK'
    'Custom/BLE time command is missing.' = 'SET TIME yyyy-mm-dd hh:mm:ss CUSTOM\|BLE'
    'Wi-Fi time-sync command is missing.' = 'SYNC WIFI TIME'
    'Alarm command is missing.' = 'SET ALARM hh:mm'
    'Alarm buzzer is not routed to GPIO48.' = 'PIN_SPKOUT\s+GPIO_NUM_48'
    'USB page has no explicit OFF state.' = 'USB DISK OFF'
    'USB page has no explicit ON state.' = 'USB DISK ON'
}

foreach ($entry in $firmwareChecks.GetEnumerator()) {
    if ($firmware -notmatch $entry.Value) {
        $failures.Add($entry.Key)
    }
}

if ($firmware -notmatch 's_usb_msc_start_requested\s*=\s*false;') {
    $failures.Add('Entering the USB page no longer proves that MSC starts only after user confirmation.')
}

$androidChecks = @{
    'Android clock read control is missing.' = 'refreshClockSettings\s*\('
    'Android custom time control is missing.' = 'setCustomClock\s*\('
    'Android Bluetooth time sync is missing.' = 'syncClockFromPhone\s*\('
    'Android Wi-Fi time sync is missing.' = 'syncClockFromWifi\s*\('
    'Android alarm control is missing.' = 'setAlarm\s*\('
}

foreach ($entry in $androidChecks.GetEnumerator()) {
    if ($android -notmatch $entry.Value) {
        $failures.Add($entry.Key)
    }
}

if ($failures.Count -gt 0) {
    Write-Host 'CLOCK / ALARM / USB CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'CLOCK / ALARM / USB CHECK: PASS' -ForegroundColor Green
