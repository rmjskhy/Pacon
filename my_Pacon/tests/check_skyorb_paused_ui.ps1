$ErrorActionPreference = 'Stop'

$source = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '..\main\fluid_pendant.c')
$failures = [System.Collections.Generic.List[string]]::new()

if ($source -notmatch 'const bool radio_sleeping = s_skyorb_wifi_enabled &&\s*!s_skyorb_network_started &&\s*!s_wifi_radio_requested &&\s*s_wifi_radio_reconnect;') {
    $failures.Add('SkyOrb does not distinguish an intentional STA pause from a real disconnect.')
}

if ($source -notmatch 'const bool show_aircraft = s_skyorb_wifi_connected \|\| radio_sleeping;') {
    $failures.Add('SkyOrb hides cached aircraft while the STA is intentionally paused.')
}

if ($source -notmatch 'else if \(radio_sleeping\) \{[\s\S]{0,500}"RADIO PAUSED"[\s\S]{0,500}"CACHED DATA"') {
    $failures.Add('SkyOrb does not label the intentional radio pause and cached-data state.')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'SkyOrb paused-radio UI checks passed.'
