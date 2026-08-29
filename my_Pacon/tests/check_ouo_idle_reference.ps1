$ErrorActionPreference = 'Stop'
$runtime = 'C:/Users/28518/.cache/codex-runtimes/codex-primary-runtime/dependencies/node'
$previousNodePath = $env:NODE_PATH
try {
    $env:NODE_PATH = Join-Path $runtime 'node_modules'
    & (Join-Path $runtime 'bin/node.exe') (Join-Path $PSScriptRoot 'ouo_idle_reference_harness.js')
    if ($LASTEXITCODE -ne 0) { throw 'OuO idle reference regression failed.' }
} finally {
    $env:NODE_PATH = $previousNodePath
}
