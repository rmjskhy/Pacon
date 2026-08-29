$ErrorActionPreference = 'Stop'
$node = 'C:\Users\28518\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
& $node (Join-Path $PSScriptRoot 'fluid_hidden_navigation_harness.js')
if ($LASTEXITCODE -ne 0) { throw 'Fluid hidden-navigation regression failed' }
