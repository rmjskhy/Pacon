$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$preview = Get-Content (Join-Path $root 'tools\ouo-preview\index.html') -Raw

# The Android recording allows an arbitrary, asymmetric mouth pull.  The
# preview must therefore use one continuous irregular primitive; it must not
# snap from a rotated rectangle to a forced square near 45 degrees.
$checks = @(
    @($preview, 'function irregularPullMouth\(', 'continuous irregular mouth helper'),
    @($preview, 'pullMouthByVector\([\s\S]*?irregularPullMouth\(', 'vector dispatches to irregular helper'),
    @($preview, 'topHalf|bottomHalf|frontHalf|backHalf', 'irregular mouth has independent edges'),
    @($preview, 'mode===\x27side\x27', 'side-pull remains a locked special mode'),
    @($preview, 'ctx\.moveTo\(0,-tip\)', 'upward pull keeps its tip upward'),
    @($preview, 'function referenceHorizontalPullMouth\(', 'horizontal reference silhouette'),
    @($preview, 'function referenceVerticalPullMouth\(', 'vertical reference silhouette'),
    @($preview, 'function referenceSquarePullMouth\(', 'square reference silhouette'),
    @($preview, 'function referenceTrianglePullMouth\(', 'triangle reference silhouette'),
    @($preview, 'function referencePullEye\(', 'reference eye aspect ratio'),
    @($preview, 'p\.mouthY\+20', 'pull mouth reference anchor'),
    @($preview, 'const square=', 'forced square removed')
)

foreach ($check in $checks) {
    $matched = $check[0] -match $check[1]
    if ($check[2] -eq 'forced square removed') {
        if ($matched) {
            throw "OuO mouth continuity check failed: $($check[2])"
        }
    } elseif (-not $matched) {
        throw "OuO mouth continuity check failed: $($check[2])"
    }
}

Write-Host 'OUO mouth continuity check passed.'
