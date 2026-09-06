$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'main\fluid_pendant.c') -Raw
$failures = @()

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $script:failures += $Message }
}

Require ($main -match '#define OUO_IDLE_FRAME_PERIOD_MS\s+33') `
    '0u0 idle cadence must target about 30 FPS.'
Require ($main -match '#define FLUID_IDLE_FRAME_PERIOD_MS\s+50') `
    'Fluid idle cadence must target 20 FPS.'
Require ($main -match '(?s)static uint32_t ui_frame_period_ms[(]void[)].*?UI_SCREEN_OUO.*?s_ouo_touch_active.*?s_ouo_expression != OUO_EXPRESSION_IDLE.*?OUO_FRAME_PERIOD_MS.*?OUO_IDLE_FRAME_PERIOD_MS') `
    '0u0 must retain its interactive cadence and select the idle cadence only when calm.'
Require ($main -match '(?s)static uint32_t ui_frame_period_ms[(]void[)].*?UI_SCREEN_FLUID.*?s_touch_down.*?s_touch_energy.*?FRAME_PERIOD_MS.*?FLUID_IDLE_FRAME_PERIOD_MS') `
    'Fluid must retain its interactive cadence until touch energy has settled.'
Require ($main -match '(?s)const uint32_t fluid_physics_steps =.*?FLUID_IDLE_FRAME_PERIOD_MS.*?3U.*?2U;.*?for [(]uint32_t step = 0; step < fluid_physics_steps; [+][+]step[)].*?step_fluid[(][)];') `
    'Fluid idle rendering must preserve roughly 60 physics steps per second.'
Require ($main -match 'const uint32_t frame_period_ms = ui_frame_period_ms[(][)];') `
    'The main scheduler must use the centralized adaptive frame-period policy.'

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'ADAPTIVE FRAME RATE CHECK: PASS'
