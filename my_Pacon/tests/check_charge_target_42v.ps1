$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw

if ($source -notmatch 'axp2101_update_register\(0x64, 0x07, 0x03\)') {
    throw 'AXP2101 charge target register is not configured for 4.2 V.'
}
if ($source -notmatch 'target=4200 mV') {
    throw 'AXP2101 startup log does not report the 4.2 V target.'
}
if ($source -notmatch '\{0, 4000, 4100, 4200, 4350, 4400, 0, 0\}') {
    throw 'AXP2101 charge-target status decoder is incomplete.'
}

Write-Output 'AXP2101 4.2 V CHARGE TARGET CHECK: PASS'
