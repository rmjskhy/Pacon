$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
$preview = Get-Content (Join-Path $PSScriptRoot '..\tools\ouo-preview\index.html') -Raw
foreach ($pair in @(
    @('FEATURE_SETTINGS_X1', 326), @('FEATURE_SETTINGS_X2', 448),
    @('FEATURE_SETTINGS_Y1', 24), @('FEATURE_SETTINGS_Y2', 132))) {
    if ($main -notmatch ('#define\s+' + $pair[0] + '\s+' + $pair[1])) {
        throw "Missing shared upper-right boundary $($pair[0])"
    }
}
$hit = 'x >= FEATURE_SETTINGS_X1 && x < FEATURE_SETTINGS_X2 &&\s+' +
       'y >= FEATURE_SETTINGS_Y1 && y < FEATURE_SETTINGS_Y2'
if ([regex]::Matches($main, $hit).Count -lt 2) {
    throw 'Fluid and OuO do not share the same upper-right settings region'
}
if ($main -match 'OUO_MENU_HOLD_X|OUO_MENU_HOLD_Y|bottom-left candidate') {
    throw 'Legacy bottom-left OuO settings trigger remains'
}
if ($preview -notmatch 'menuHit=!mouthHit&&q\.x>=326&&q\.x<448&&q\.y>=24&&q\.y<132') {
    throw 'OuO preview is not synchronized to the firmware upper-right trigger'
}
if ($preview -match 'menuHit=!mouthHit&&q\.x<210&&q\.y>276') {
    throw 'Preview still accepts the old bottom-left settings gesture'
}
Write-Host 'FEATURE SETTINGS POSITION CHECK: PASS'
