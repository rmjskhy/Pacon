$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$node = (Get-Command node -ErrorAction SilentlyContinue)?.Source
if (-not $node) {
    $node = 'C:\Users\28518\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
}
if (-not (Test-Path -LiteralPath $node)) { throw "Node.js runtime is missing: $node" }
& $node (Join-Path $PSScriptRoot 'ouo_semicircle_pull_harness.js')
if (-not $?) { throw 'OuO semicircle pull runtime regression failed.' }
Write-Host 'OUO semicircle pull check passed.'
