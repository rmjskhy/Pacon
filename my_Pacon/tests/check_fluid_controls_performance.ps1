$ErrorActionPreference = 'Stop'
$main = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw
if ($main -notmatch 'if \(s_ui_screen == UI_SCREEN_COLOUR_PICKER\) \{\s+fluid_colour_picker_touch\(x, y\);') {
    throw 'Colour drag must run outside the initial-press-only branch'
}
if ($main -notmatch 'fluid_service_preferences\(\);\s+fluid_controls_report_perf\(\);') {
    throw 'Deferred persistence service must run after rendering'
}
if ($main -notmatch 's_ui_screen != UI_SCREEN_FLUID_SETTINGS && s_ui_screen != UI_SCREEN_COLOUR_PICKER\) \{\s+s_fluid_controls_canvas_screen = UI_SCREEN_HOME;') {
    throw 'Other screens must invalidate the shared controls canvas'
}
& 'C:\Users\28518\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' (Join-Path $PSScriptRoot 'fluid_controls_harness.js')
if ($LASTEXITCODE -ne 0) { throw 'Fluid controls regression failed' }
