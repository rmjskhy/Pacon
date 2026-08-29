$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sourcePath = Join-Path $root 'test_Pacon\buzzer_test\main\main.c'
$failures = [System.Collections.Generic.List[string]]::new()

if (-not (Test-Path -LiteralPath $sourcePath)) {
    $failures.Add('Minimal buzzer-only firmware does not exist.')
} else {
    $source = Get-Content -LiteralPath $sourcePath -Raw
    if ($source -notmatch 'GPIO_NUM_48') {
        $failures.Add('Minimal buzzer firmware is not routed to GPIO48.')
    }
    if ($source -notmatch '2000') {
        $failures.Add('Minimal buzzer firmware does not exercise the proven 2 kHz tone.')
    }
    if ($source -match 'i2c|nimble|wifi|lcd|lvgl') {
        $failures.Add('Minimal buzzer firmware contains unrelated peripheral code.')
    }
}

if ($failures.Count -gt 0) {
    Write-Host 'MINIMAL BUZZER CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'MINIMAL BUZZER CHECK: PASS' -ForegroundColor Green
