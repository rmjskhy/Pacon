$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw
$fluid = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw

# This is the red-capable regression seam for the reported dead gestures:
# mouth drags must stay armed after the initial touch, preview drags must use
# their vector instead of a kiss-only width tweak, and every named expression
# needs a real trigger path rather than only a renderer branch.
$checks = @(
    @($preview, 'function pullMouthByVector\(', 'preview vector mouth helper'),
    @($preview, "winkLeft:'左眼眨'", 'preview left wink state'),
    @($preview, "winkRight:'右眼眨'", 'preview right wink state'),
    @($preview, "angry:'生气'", 'preview angry state'),
    @($preview, "sleepy:'困倦'", 'preview sleepy state'),
    @($preview, "dizzy:'眩晕'", 'preview dizzy state'),
    @($preview, 'state===\x27kiss\x27\)\(drag&&drag\.mouth\?pullMouthByVector\(', 'preview drag dispatch'),
    @($preview, 'onpointerup=(?:\(\)|e)=>\{[\s\S]*?drag=null', 'preview drag release'),
    @($fluid, 's_ouo_mouth_touch_active = true;', 'firmware mouth drag armed'),
    @($fluid, 's_ouo_mouth_variant_selected = false;', 'firmware mouth variant reset'),
    @($fluid, 's_ouo_mouth_side = abs\(x - OUO_FACE_CENTER_X\) >= 10;', 'firmware side-half hit'),
    @($fluid, 'esp_random\(\) % 4U', 'firmware random mouth family'),
    @($fluid, 'static bool ouo_drag_mouth_pixel\(', 'firmware vector mouth helper'),
    @($fluid, 'ouo_drag_mouth_pixel\(mouth_dx, mouth_dy,\s*s_ouo_mouth_x_offset', 'firmware drag dispatch'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_DIZZY,', 'shake dizzy trigger'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_WINK_LEFT,', 'left wink trigger'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_WINK_RIGHT,', 'right wink trigger'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_HEAD_PAT,', 'recorded forehead hold trigger'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_DELIGHTED, 6000\)', 'recorded forehead release response'),
    @($fluid, 'ouo_set_expression\(OUO_EXPRESSION_SAD,', 'sad trigger')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "OuO gesture trigger check failed: $($check[2])"
    }
}

Write-Host 'OUO gesture trigger check passed.'
