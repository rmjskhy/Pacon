$ErrorActionPreference = 'Stop'

$androidPath = Join-Path $PSScriptRoot '..\..\PaconBleTool\app\src\main\java\com\pacon\bletool\MainActivity.java'
$android = Get-Content -LiteralPath $androidPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($android -notmatch 'private ScrollView logScroll;') {
    $failures.Add('The diagnostic log has no retained ScrollView for automatic scrolling.')
}
if ($android -match 'logScroll\.fullScroll\(View\.FOCUS_DOWN\)') {
    $failures.Add('Log updates still use focus-based fullScroll and can drag the whole page.')
}
if ($android -notmatch 'stickLogToBottom') {
    $failures.Add('Log follow mode is not gated by the user already being at the bottom.')
}
if ($android -notmatch 'logScroll\.scrollTo\(0, maxScroll\)') {
    $failures.Add('Bottom following does not use focus-free child scrolling.')
}
if ($android -notmatch 'WindowInsets\.Type\.systemBars\(\)') {
    $failures.Add('The activity does not reserve modern system-bar insets.')
}
if ($android -notmatch 'getSystemWindowInsetBottom\(\)') {
    $failures.Add('The activity has no pre-Android-30 system-bar fallback.')
}

if ($failures.Count -gt 0) {
    Write-Host 'ANDROID LOG VISIBILITY CHECK: FAIL' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'ANDROID LOG VISIBILITY CHECK: PASS' -ForegroundColor Green
