$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw
$fluid = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw

# Regression seam for the two remaining reports: the previewer must exercise
# the same touch regions as the firmware, and dizzy eyes must actually rotate
# instead of remaining a static spiral.
$checks = @(
    @($fluid, 's_ouo_dizzy_phase', 'firmware dizzy phase state'),
    @($fluid, 'ouo_dizzy_eye_pixel\(int dx, int dy, float phase\)', 'firmware phase-aware dizzy helper'),
    @($fluid, 's_ouo_dizzy_phase\s*\+=', 'firmware advances dizzy phase'),
    @($fluid, 's_ouo_expression == OUO_EXPRESSION_DIZZY[\s\S]*?s_ouo_dirty = true;', 'firmware redraws spinning dizzy eyes'),
    @($preview, '\bdizzyPhase\b', 'preview dizzy phase state'),
    @($preview, 'requestAnimationFrame\(', 'preview animation loop'),
    @($preview, 'function dizzyEye\(cx,phase', 'preview phase-aware dizzy helper'),
    @($preview, 'pointerKind', 'preview synchronized gesture classification'),
    @($preview, "pointerKind==='eyeLeft'", 'preview left-eye region'),
    @($preview, "pointerKind==='eyeRight'", 'preview right-eye region'),
    @($preview, "pointerKind==='head'", 'preview head region'),
    @($preview, "pointerKind==='mouth'", 'preview mouth region'),
    @($preview, "pointerKind==='cheek'", 'preview cheek region'),
    @($preview, 'if\(drag\)return;', 'second pointer cannot replace the active gesture'),
    @($preview, 'pointerReversals', 'preview shake reversal trigger'),
    @($preview, 'winkLeft', 'preview left-eye wink trigger'),
    @($preview, 'winkRight', 'preview right-eye wink trigger'),
    @($preview, "setState\('headPat'\)", 'preview recorded forehead hold'),
    @($preview, "releaseResponse\('delighted',6000\)", 'preview recorded forehead release'),
    @($preview, 'dizzy', 'preview shake dizzy trigger'),
    @($preview, 'activeEyePointers', 'preview single-pointer eye tracking'),
    @($preview, 'drag\.mouth', 'preview single-pointer mouth drag')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "OuO expression animation check failed: $($check[2])"
    }
}

if ($preview -match 'activeSqueezePointers|squeezeTest|startSqueezeDemo|updateBilateralSqueeze|pointerKind=''squeeze''') {
    throw 'OuO expression animation check failed: preview still exposes a two-pointer squeeze path'
}
if ($fluid -match 'ouo_handle_two_touches\(x, y, x2, y2\)') {
    throw 'OuO expression animation check failed: firmware still dispatches two-pointer squeeze'
}

# A phase variable alone is not an animation.  At the 16 ms firmware update
# cadence the increment must be large enough to be visible and to match the
# previewer's roughly 0.18 rad/frame motion.  Keep this numeric seam red when
# a future edit accidentally restores an imperceptibly small coefficient.
$phaseMatch = [regex]::Match($fluid,
    's_ouo_dizzy_phase\s*\+=\s*\(float\)phase_delta_us\s*\*\s*([0-9.eE+-]+)f')
if (-not $phaseMatch.Success) {
    throw 'OuO expression animation check failed: dizzy phase rate is not parseable'
}
$phasePerFrame = [double]$phaseMatch.Groups[1].Value * 16000.0
if ($phasePerFrame -lt 0.08) {
    throw "OuO expression animation check failed: dizzy phase increment is too small ($phasePerFrame rad/16ms)"
}

$ouoPeriodMatch = [regex]::Match($fluid, '#define\s+OUO_FRAME_PERIOD_MS\s+(\d+)')
if (-not $ouoPeriodMatch.Success) {
    throw 'OuO expression animation check failed: no dedicated OuO frame period'
}
$ouoPeriodMs = [int]$ouoPeriodMatch.Groups[1].Value
if ($ouoPeriodMs -gt 20) {
    throw "OuO expression animation check failed: OuO frame period is too slow ($ouoPeriodMs ms)"
}
if ($fluid -notmatch 's_ui_screen\s*==\s*UI_SCREEN_OUO[\s\S]*?OUO_FRAME_PERIOD_MS') {
    throw 'OuO expression animation check failed: main loop does not use the OuO frame period'
}

$dizzyBodyMatch = [regex]::Match($fluid,
    'static bool ouo_dizzy_eye_pixel\(int dx, int dy, float phase\)\s*\{([\s\S]*?)\n\}')
if (-not $dizzyBodyMatch.Success) {
    throw 'OuO expression animation check failed: dizzy helper body is not parseable'
}
$dizzyBody = $dizzyBodyMatch.Groups[1].Value
$distancePos = $dizzyBody.IndexOf('const int distance2')
$sqrtPos = $dizzyBody.IndexOf('sqrtf(')
if ($distancePos -lt 0 -or $dizzyBody.IndexOf('if (distance2 > 1089)', $distancePos) -lt 0 -or
    $sqrtPos -lt 0 -or $distancePos -gt $sqrtPos) {
    throw 'OuO expression animation check failed: dizzy helper lacks an early integer radius clip'
}

Write-Host 'OUO expression animation check passed.'
