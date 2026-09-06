$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'main\fluid_pendant.c') -Raw
$failures = @()

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $script:failures += $Message }
}

$stepMatch = [regex]::Match(
    $main,
    '(?s)static void step_fluid[(]void[)]\s*\{(?<body>.*?)\r?\n\}'
)
Require $stepMatch.Success 'step_fluid must remain present for tilt-policy validation.'

if ($stepMatch.Success) {
    $body = $stepMatch.Groups['body'].Value
    Require ($body -match 'read_tilt[(]&gravity_x,[ ]*&gravity_y[)]') `
        'Fluid physics must use the measured in-plane gravity vector.'
    Require ($body -notmatch 'gravity_y[ ]*=[ ]*92') `
        'A face-up board must not receive a fixed downward gravity bias.'
    Require ($body -match '(?s)if [(]abs[(]gravity_x[)] [+] abs[(]gravity_y[)] < 12[)]\s*\{\s*gravity_x[ ]*=[ ]*0;\s*gravity_y[ ]*=[ ]*0;') `
        'The accelerometer noise dead zone must be directionally neutral.'
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'FLUID FLAT-TILT NEUTRALITY CHECK: PASS'
