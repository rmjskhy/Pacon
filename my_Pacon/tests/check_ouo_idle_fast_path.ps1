$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Get-Content (Join-Path $root 'main/fluid_pendant.c') -Raw
$render = [regex]::Match($source, 'static void render_ouo_frame\(void\)\s*\{([\s\S]*?)\n\}').Groups[1].Value
if ($render -notmatch 'if \(idle_fast\)\s*\{\s*ouo_blit_idle_tiles\(') {
    throw 'Idle playback still traverses the general per-pixel expression renderer for every background pixel.'
}
if ($render -notmatch 'const int64_t render_start_us = esp_timer_get_time\(\);[\s\S]*clear_canvas_rect') {
    throw 'OuO timing still excludes canvas painting.'
}
if ($source -notmatch 'static void ouo_blit_idle_tiles\(' -or
    $source -notmatch 's_ouo_idle_palette\[source\[x\]\]') {
    throw 'Missing direct grayscale-tile/LUT render path.'
}
Write-Host 'OUO idle fast-path call-site and timing coverage checks passed.'
$node = 'C:/Users/28518/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
& $node (Join-Path $PSScriptRoot 'ouo_idle_blit_harness.js')
if ($LASTEXITCODE -ne 0) { throw 'OuO idle tile pixel equivalence failed.' }
