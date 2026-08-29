$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw
$fluid = Get-Content (Join-Path $root 'main\fluid_pendant.c') -Raw

# These checks intentionally target the two reported visual regressions:
# the kiss mouth must use the thicker capture-matched 3-stroke, and a side
# pull must fill its left 3/centre bridge while retaining a separate arc.
$checks = @(
    @($preview, 'function kissMouth\(\)\{ctx\.lineWidth=9', 'preview kiss stroke width'),
    @($preview, 'function pullSideMouth\(', 'preview side-pull helper'),
    @($preview, 'function liveSidePullMouth\([\s\S]*?ctx\.fill\(\)', 'preview side-pull fill'),
    @($preview, 'function liveSidePullMouth\([\s\S]*?ctx\.lineWidth=8', 'preview side-pull capture stroke'),
    @($preview, "else if\(state==='pullSide'\).*pullMouthByVector\(pullDx,pullDy,'side'\)", 'preview side-pull dispatch'),
    @($fluid, 'static bool ouo_kiss_mouth_pixel', 'firmware kiss mouth helper'),
    @($fluid, 'static bool ouo_pull_side_mouth_pixel', 'firmware side-pull helper'),
    @($fluid, 'typedef enum \{[\s\S]*?OUO_MOUTH_VARIANT_SIDE', 'firmware mouth variant families'),
    @($fluid, 'static bool ouo_stretch_mouth_pixel', 'firmware stretch family helper'),
    @($fluid, 'static bool ouo_square_mouth_pixel', 'firmware axis-aligned square helper'),
    @($fluid, 'const int radius_x = 21 \+ t \* 32 / 256;', 'firmware round mouth expanded travel'),
    @($fluid, 'const int radius_y = 20 \+ t \* 26 / 256;', 'firmware round mouth expanded depth'),
    @($fluid, 'static bool ouo_triangle_mouth_pixel[\s\S]*?const int half = 18 \+ ax \* 48 / 84;', 'firmware triangle width follows horizontal travel'),
    @($fluid, 'static bool ouo_triangle_mouth_pixel[\s\S]*?const int tip = 18 \+ ay \* 12 / 84;', 'firmware upright triangle height follows vertical travel'),
    @($fluid, 'left = -21;[\s\S]*?right = left \+ half_width \* 2', 'firmware opposite-edge anchor'),
    @($fluid, 's_ouo_mouth_variant == OUO_MOUTH_VARIANT_SIDE[\s\S]*?s_ouo_mouth_y_offset = clamp_int\(travel_y / 8, -6, 6\)', 'firmware side-pull fixed height'),
    @($fluid, 's_ouo_mouth_variant == OUO_MOUTH_VARIANT_SIDE[\s\S]*?s_ouo_mouth_stretch_pixels = 0;', 'firmware side-pull fixed contour size'),
    @($fluid, 'ouo_drag_mouth_pixel\(mouth_dx, mouth_dy,', 'firmware vector mouth dispatch')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "OuO mouth-shape check failed: $($check[2])"
    }
}

if ($preview -match 'ax>ay\*2&&mag>=32') {
    throw 'OuO mouth-shape check failed: obsolete preview horizontal threshold'
}

Write-Host 'OUO mouth shape check passed.'
