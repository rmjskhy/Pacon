$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fluid = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw

$checks = @(
    @($fluid, '#define OUO_LEFT_EYE_X\s+120', 'left eye centre'),
    @($fluid, '#define OUO_RIGHT_EYE_X\s+346', 'right eye centre'),
    @($fluid, '#define OUO_EYE_Y\s+216', 'eye height'),
    @($fluid, '#define OUO_MOUTH_Y\s+264', 'mouth height'),
    @($fluid, '#define OUO_EYE_RADIUS\s+30', 'eye radius'),
    @($fluid, 's_ouo_expression == OUO_EXPRESSION_IDLE\s*\|\|', 'idle solid eyes'),
    @($fluid, 's_ouo_expression == OUO_EXPRESSION_BLINK', 'blink U smile branch'),
    @($fluid, 'ouo_curve_mouth_pixel\(mouth_dx, mouth_dy, 29, 17, 4, true\)', 'source U smile geometry'),
    @($fluid, 'return dy >= -8', 'captured blink clip'),
    @($fluid, 'ouo_curve_mouth_pixel\(mouth_dx, mouth_dy, 29, 17, 4, true\)', 'happy U smile'),
    @($preview, 'const defaults=\{eyeGap:226,eyeY:216,eyeRadius:30,mouthY:264,mouthWidth:31,mouthHeight:29', 'preview geometry'),
    @($preview, "state==='idle'\|\|state==='surprised'", 'preview default round eyes'),
    @($preview, "state==='happy'\|\|state==='kiss'\|\|state==='closedNeutral'", 'preview happy closed lids'),
    @($fluid, 's_ouo_expression == OUO_EXPRESSION_KISS \|\|\s*s_ouo_expression == OUO_EXPRESSION_HAPPY', 'firmware original happy closed lids preserved'),
    @($preview, 'bottomClippedEye\(cx\).*?p\.eyeY-8', 'preview blink clip'),
    @($preview, 'sourceSmileMouth', 'preview source smile')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "OuO geometry check failed: $($check[2])"
    }
}

Write-Host 'OUO geometry consistency check passed.'
