$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw

# Regression seam for mouth gesture mode switching: once a drag is classified
# as side-pull or surprise, later samples must not silently reclassify it;
# the state label remains locked; surprise uses the live contour while a
# horizontal side pull uses the captured filled-3/independent-arc primitive.
$checks = @(
    @($preview, 'mouthMode', 'preview locks mouth gesture mode'),
    @($preview, "setState\(drag\.mouthMode==='side'\?'pullSide':'surprised'\)", 'preview side/surprise dispatch'),
    @($preview, "pullMouthByVector\(pullDx,pullDy,'side'\)", 'preview side-pull rendering mode'),
    @($preview, 'mode===\x27side\x27\)[\s\S]*?ctx\.translate\(233\+Math\.sign\(dx\)', 'preview side reference keeps its baseline Y'),
    @($preview, 'function irregularPullMouth\([\s\S]*?topHalf[\s\S]*?bottomHalf', 'preview diagonal mouth remains continuous')
)

foreach ($check in $checks) {
    if ($check[0] -notmatch $check[1]) {
        throw "OuO gesture transition check failed: $($check[2])"
    }
}

Write-Host 'OUO gesture transition check passed.'
