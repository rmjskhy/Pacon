$ErrorActionPreference = 'Stop'

$source = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw

function Require-Pattern([string]$pattern, [string]$message) {
    if ($source -notmatch $pattern) { throw $message }
}

Require-Pattern '#define\s+SKYORB_FETCH_RETRY_MS\s+15000' `
    'SkyOrb failures must retry after 15 seconds.'
Require-Pattern 's_skyorb_last_fetch\s*=\s*0;[\s\S]{0,180}s_skyorb_fetch_failed\s*=\s*false;' `
    'A fresh Wi-Fi connection must clear the stale fetch deadline and failure state.'
Require-Pattern 'skyorb_disable_network\([\s\S]{0,900}s_skyorb_last_fetch\s*=\s*0;' `
    'Disabling Wi-Fi must clear the stale fetch deadline.'
Require-Pattern 's_skyorb_fetch_failed\s*\?\s*SKYORB_FETCH_RETRY_MS\s*:\s*SKYORB_FETCH_PERIOD_MS' `
    'The network task must use a shorter interval after a failed fetch.'
Require-Pattern '\[SKYORB-FETCH\].*stage=' `
    'Fetch failures must identify their stage in the serial log.'
Require-Pattern 'https://api\.adsb\.lol/v2/point/' `
    'SkyOrb must use the verified public ADSB.lol point endpoint.'
Require-Pattern 'User-Agent[\s\S]{0,180}PaconRadar/1\.0' `
    'SkyOrb must identify the PACON client to the public API.'

Write-Host 'SkyOrb retry/diagnostic regression checks passed.'
