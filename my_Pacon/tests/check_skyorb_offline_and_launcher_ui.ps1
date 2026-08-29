$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$source = Get-Content -LiteralPath $sourcePath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($source -notmatch 'const bool show_aircraft = s_skyorb_wifi_connected') {
    $failures.Add('Radar aircraft rendering is not explicitly disabled while Wi-Fi is offline.')
}

if ($source -notmatch 'if \(show_aircraft && demo\)' -or
    $source -notmatch 'else if \(show_aircraft\)') {
    $failures.Add('Demo and live aircraft branches are not both gated by the online state.')
}

if ($source -notmatch 'static void skyorb_text_centered\(') {
    $failures.Add('Radar status text has no font-measured centering helper.')
}

if ($source -notmatch 'skyorb_text_centered\("OFFLINE"' -or
    $source -notmatch 'skyorb_text_centered\("AUTH FAILED"' -or
    $source -notmatch 'skyorb_text_centered\(live') {
    $failures.Add('Radar network/status labels still rely on hand-tuned X coordinates.')
}

if ($source -notmatch '0u0 launcher icon: official OuO-inspired face') {
    $failures.Add('The 0u0 launcher icon is not the official OuO-inspired face design.')
}

if ($source -match '0u0 launcher icon: soft app tile') {
    $failures.Add('The temporary lavender 0u0 text tile is still present.')
}

if ($failures.Count -gt 0) {
    Write-Host 'SKYORB OFFLINE/UI CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'SKYORB OFFLINE/UI CHECK: PASS' -ForegroundColor Green
