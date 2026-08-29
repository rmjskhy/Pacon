$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw

# The regression is specifically the real pointer-drag seam.  Research buttons
# may use fixed reference silhouettes, but live mouth drags must dispatch the
# continuous primitive for surprise/kiss; center horizontal/vertical pulls
# share the stretch family, while an edge-origin horizontal pull must retain
# the captured filled-3/independent-arc geometry.
$checks = @(
    @($preview, 'function livePullMouth\(', 'live pull primitive'),
    @($preview, 'function liveVerticalPullMouth\(', 'downward capsule primitive'),
    @($preview, 'function liveHorizontalPullMouth\(', 'horizontal reference primitive'),
    @($preview, 'const halfW=21\+t\*42\*h,halfH=Math\.min\(42,20\+t\*\(15\+27\*\(1-h\)\)\)', 'stretch follows direction while growing'),
    @($preview, 'function liveSquarePullMouth\(dx,dy,mag,t\)', 'square variant uses the actual diagonal vector'),
    @($preview, 'function liveDiagonalPullMouth\(', 'diagonal rounded-square primitive'),
    @($preview, 'function pullFamilyForVector\(', 'first-direction contour family classifier'),
    @($preview, 'function liveStretchPullMouth\(', 'horizontal/vertical shared stretch family'),
    @($preview, 'const pullVariants=', 'random pull variant list'),
    @($preview, 'function choosePullVariant\(', 'random pull variant chooser'),
    @($preview, 'const pullVariants=\[\x27round\x27,\x27stretch\x27,\x27square\x27,\x27triangle\x27\]', 'square is a separate random family'),
    @($preview, 'mouthSide:pointerKind===\x27mouth\x27&&Math\.abs\(q\.x-233\)>=10', 'wide side-pull start zone is armed'),
    @($preview, 'const menuHit=!mouthHit&&q\.x>=326&&q\.x<448&&q\.y>=24&&q\.y<132', 'OuO settings uses upper-right feature region'),
    @($preview, 'drag\.mouthVariant=sideGesture\?\x27side\x27:firstFamily===\x27side\x27\?\x27stretch\x27:choosePullVariant\(\)', 'edge side pull and center stretch dispatch'),
    @($preview, 'function liveSidePullMouth\(dx=1,amount=1\)', 'live side pull mirrors left and right with a stable contour'),
    @($preview, 'liveSidePullMouth\(dx,1\)', 'side-pull shape keeps a stable contour while translating'),
    @($preview, 'const currentFamily=pullFamilyForVector\(dx,dy\)', 'live drag follows current contour family'),
    @($preview, 'livePullMouth\(dx,dy,mag,drag&&drag\.mouthVariant\)', 'live drag renders selected random variant'),
    @($preview, "state==='surprised'\)\(drag&&drag\.mouth\?pullMouthByVector\(pullDx,pullDy,'live'\)", 'surprised drag uses live primitive'),
    @($preview, "state==='kiss'\)\(drag&&drag\.mouth\?pullMouthByVector\(pullDx,pullDy,'live'\)", 'kiss drag uses live primitive'),
    @($preview, "state==='pullSide'\)\(drag&&drag\.mouth\?pullMouthByVector\(pullDx,pullDy,'side'\)", 'side drag uses captured side primitive'),
    @($preview, 'mode===\x27side\x27\)\{[\s\S]*?ctx\.save\(\);ctx\.translate\(233\+Math\.sign\(dx\)', 'side mode keeps captured X translation'),
    @($preview, 'sideBulge=edgeLean\*Math\.sign\(dx\)', 'horizontal bulge follows drag direction'),
    @($preview, 'verticalBulge=edgeLean\*Math\.sign\(dy\)', 'vertical bulge follows drag direction'),
    @($preview, 'mode===\x27auto\x27&&ax>ay\*2&&mag>=32', 'obsolete auto side classification removed')
)

foreach ($check in $checks) {
    $matched = $check[0] -match $check[1]
    if ($check[2] -eq 'obsolete auto side classification removed') {
        if ($matched) { throw "OuO live-pull check failed: $($check[2])" }
    } elseif (-not $matched) {
        throw "OuO live-pull check failed: $($check[2])"
    }
}

Write-Host 'OUO live pull check passed.'
