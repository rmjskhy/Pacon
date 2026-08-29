$ErrorActionPreference = 'Stop'
$runtime = 'C:/Users/28518/.cache/codex-runtimes/codex-primary-runtime/dependencies/node'
$previousNodePath = $env:NODE_PATH
try {
    $env:NODE_PATH = Join-Path $runtime 'node_modules'
    & (Join-Path $runtime 'bin/node.exe') (Join-Path $PSScriptRoot 'ouo_touch_recording_harness.js')
    if ($LASTEXITCODE -ne 0) { throw 'OuO touch recording regression failed.' }
} finally {
    $env:NODE_PATH = $previousNodePath
}
