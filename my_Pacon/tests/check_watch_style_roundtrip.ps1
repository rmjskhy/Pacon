$ErrorActionPreference = 'Stop'

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$firmware = Get-Content -LiteralPath (Join-Path $projectRoot 'main\fluid_pendant.c') -Raw
$android = Get-Content -LiteralPath (Join-Path $projectRoot '..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($firmware -notmatch '\\"style_name\\":\\"%s\\"') {
    $failures.Add('GET CLOCK does not expose an unambiguous style name.')
}
if ($firmware -notmatch 's_watch_rendered_style != frame_style') {
    $failures.Add('Renderer does not detect a requested style transition.')
}
if ($firmware -notmatch 'next full frame replaces previous') {
    $failures.Add('Style transition is not logged as one atomic replacement frame.')
}
if ($android -notmatch 'private void applyWatchStyle\(int requestedStyle\)') {
    $failures.Add('Android does not use a dedicated verified style switch.')
}
if ($android -notmatch 'actual != expected') {
    $failures.Add('Android does not compare the requested and returned styles.')
}
if ($android -notmatch '当前 " \+ styleText') {
    $failures.Add('Android clock status does not show the device-reported style.')
}

if ($failures.Count -gt 0) {
    Write-Host 'WATCH STYLE ROUNDTRIP CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'WATCH STYLE ROUNDTRIP CHECK: PASS' -ForegroundColor Green
