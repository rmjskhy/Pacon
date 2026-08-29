$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$defaults = Get-Content -Raw -LiteralPath (Join-Path $root 'sdkconfig.defaults')

if ($defaults -notmatch '(?m)^CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y$') {
    throw 'PACON HTTPS must allocate mbedTLS state from PSRAM.'
}
if ($defaults -notmatch '(?m)^CONFIG_MBEDTLS_DYNAMIC_BUFFER=y$') {
    throw 'PACON HTTPS must release TLS RX/TX buffers dynamically.'
}
if ($defaults -match '(?m)^CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y$') {
    throw 'Internal-only mbedTLS allocation exhausts fragmented DRAM after Wi-Fi starts.'
}

Write-Host 'TLS memory configuration check passed.'
