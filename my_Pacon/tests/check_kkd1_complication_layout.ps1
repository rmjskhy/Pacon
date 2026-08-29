$ErrorActionPreference = 'Stop'

$project = Split-Path -Parent $PSScriptRoot
$asset = Join-Path $project 'main\assets\kkd1_complication.rgb565a'
$cmake = Get-Content -Raw (Join-Path $project 'main\CMakeLists.txt')
$source = Get-Content -Raw (Join-Path $project 'main\fluid_pendant.c')
$converter = Get-Content -Raw (Join-Path $project 'tools\make_kkd1_watch_asset.py')

if (-not (Test-Path $asset)) {
    throw 'KKD1 APK complication background is not embedded'
}

$bytes = [System.IO.File]::ReadAllBytes($asset)
if ($bytes.Length -lt 12 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'PCA1') {
    throw 'KKD1 complication asset does not have a PCA1 header'
}
$x = [BitConverter]::ToUInt16($bytes, 4)
$y = [BitConverter]::ToUInt16($bytes, 6)
$w = [BitConverter]::ToUInt16($bytes, 8)
$h = [BitConverter]::ToUInt16($bytes, 10)
if ($x -ne 106 -or $y -ne 121 -or $w -ne 110 -or $h -ne 110) {
    throw "KKD1 complication asset must match XML x=106 y=121 w=110 h=110; got $x,$y $w x $h"
}

if ($cmake -notmatch 'assets/kkd1_complication\.rgb565a') {
    throw 'KKD1 complication asset is not in EMBED_FILES'
}
if ($source -notmatch 'watch_draw_asset_layer\(kkd1_complication_start,\s*kkd1_complication_end,\s*0\.0f\)') {
    throw 'KKD1 compositor does not draw the APK complication background'
}
if ($source -match 'fill_canvas_round_rect\(74,\s*158,\s*170,\s*231') {
    throw 'obsolete hand-drawn KKD1 complication is still present'
}
if ($source -notmatch '#define\s+WATCH_KKD1_COMPLICATION_CENTER_X\s+161') {
    throw 'KKD1 complication centre constant no longer matches XML slot'
}

if ($source -notmatch 'watch_text_optically_centered\(digital,\s*WATCH_KKD1_COMPLICATION_CENTER_X,') {
    throw 'KKD1 digital time is not centred on the XML complication slot'
}
if ($converter -notmatch 'COMPLICATION_BOX\s*=\s*\(106,\s*121,\s*216,\s*231\)') {
    throw 'asset converter does not pin the XML complication destination box'
}

Write-Host 'KKD1 complication layout regression check passed.'
