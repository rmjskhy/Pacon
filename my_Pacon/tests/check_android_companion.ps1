$ErrorActionPreference = 'Stop'
$workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$android = Join-Path $workspace 'PaconBleTool'
$source = Join-Path $android 'app\src\main\java\com\pacon\bletool'
$activity = Get-Content (Join-Path $source 'MainActivity.java') -Raw
$firmware = Get-Content (Join-Path $workspace 'my_Pacon\main\fluid_pendant.c') -Raw

foreach ($pattern in @('getBoolean\("debug_mode", false\)',
        'debugPanel.setVisibility\(checked \? View.VISIBLE : View.GONE\)',
        'debugPanel.addView\(testRow\)', 'debugPanel.addView\(commandEdit',
        'debugPanel.addView\(logScroll', 'card\(debugPanel, "遥控快门测试"',
        'UiModeProtocol.completed\(state, index\)')) {
    if ($activity -notmatch $pattern) { throw "Companion UI regression: $pattern" }
}
if ($activity -match 'card\(devicePage, "遥控快门') {
    throw 'Camera test must not be exposed on the everyday device page'
}
$commandBody = $firmware.Substring($firmware.IndexOf('static esp_err_t fluid_ble_command('))
$commandBody = $commandBody.Substring(0, $commandBody.IndexOf('static ', 20))
if ($commandBody -match 'enter_(fluid|ouo)_screen\(') { throw 'BLE callback must not render/switch screens directly' }
foreach ($pattern in @('xQueueSend\(s_ble_ui_requests', 'xQueueReceive\(s_ble_ui_requests',
        'xQueueCreate\(1, sizeof\(ui_screen_t\)\)', 'apply_ble_ui_request\(\);',
        'ERR UI expects HOME\|FLUID\|OUO', 's_ble_ui_state = busy \? BLE_UI_BUSY : BLE_UI_DONE')) {
    if ($firmware -notmatch $pattern) { throw "Remote screen queue regression: $pattern" }
}
$jbr = 'C:\Program Files\Android\Android Studio\jbr\bin'
$output = Join-Path $android 'build\protocol-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& (Join-Path $jbr 'javac.exe') -encoding UTF-8 -d $output (Join-Path $source 'UiModeProtocol.java') (Join-Path $android 'tests\UiModeProtocolTest.java')
if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
& (Join-Path $jbr 'java.exe') -cp $output com.pacon.bletool.UiModeProtocolTest
if ($LASTEXITCODE -ne 0) { throw 'Protocol test failed' }
Write-Host 'ANDROID COMPANION CHECK: PASS'
