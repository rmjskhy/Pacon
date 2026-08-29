$ErrorActionPreference = 'Stop'
$source = Get-Content "$PSScriptRoot\..\main\fluid_pendant.c" -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($source -notmatch 'case UI_SCREEN_SKYORB:\s*s_skyorb_dirty = true') {
    $failures.Add('Waking the OLED must force the active SkyOrb page to redraw.')
}
if ($source -notmatch 'NO AIRCRAFT' -or $source -notmatch 'IN SELECTED RANGE') {
    $failures.Add('A successful empty aircraft response needs an explicit on-screen state.')
}
if ($source -notmatch '\[SKYORB-RENDER\].*flush=') {
    $failures.Add('SkyOrb render/flush diagnostics are missing.')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}
Write-Host 'SkyOrb empty-state and wake redraw checks passed.'
