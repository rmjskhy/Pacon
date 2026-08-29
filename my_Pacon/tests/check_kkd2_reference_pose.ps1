$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwarePath = Join-Path $projectRoot 'main\fluid_pendant.c'
$xmlPath = Join-Path (Split-Path -Parent $projectRoot) '_apk_reference\kkd2_watchface.xml'
$converterPath = Join-Path $projectRoot 'tools\make_kkd2_watch_asset.py'

$firmware = Get-Content -LiteralPath $firmwarePath -Raw
$xml = Get-Content -LiteralPath $xmlPath -Raw
$converter = Get-Content -LiteralPath $converterPath -Raw

function Assert-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Match $xml '\[HOUR_0_23\].*\* 30' 'KKD2 XML no longer defines the character as the integer-hour layer.'
Assert-Match $xml '\[MINUTE\].*\* 6' 'KKD2 XML no longer defines the separate arm as the minute layer.'
Assert-Match $xml '(?s)\[HOUR_0_23\].*\* 30.*\[MINUTE\].*\* 6' `
    'KKD2 source XML must draw the complete-character hour layer before the separate minute arm.'

# The APK rotates the complete character by HOUR_0_23 * 30 exactly.  It does
# not interpolate the character angle with the current minute.
Assert-Match $firmware 'hour_rotation\s*=\s*\(float\)\(time->hour % 12\)\s*\*\s*tau\s*/\s*12\.0f' `
    'Firmware adds minute interpolation to the KKD2 character, unlike the source APK.'

# Preserve the APK scene order: the complete character/hour layer is the body,
# and the independently rotating minute arm is its foreground limb.
Assert-Match $firmware '(?s)static void watch_compose_kkd2.*?kkd2_background_start.*?kkd2_complication_start.*?kkd2_hour_start.*?kkd2_minute_start' `
    'KKD2 embedded composition must draw the complete character before the minute arm so the body does not cut off the shoulder-side arm pixels.'
Assert-Match $converter '"kkd2_hour\.rgb565a"[\s\S]*?-34, -35' `
    'KKD2 complete-character canvas origin must match the APK group offset.'
Assert-Match $converter '"kkd2_minute\.rgb565a"[\s\S]*?0, 0' `
    'KKD2 separate-arm canvas origin must match the 450x450 APK canvas.'

$hour = 16
$minute = 2
$characterDegrees = ($hour % 12) * 30
$armDegrees = $minute * 6
if ($characterDegrees -ne 120 -or $armDegrees -ne 12) {
    throw '16:02 KKD2 reference pose calculation changed unexpectedly.'
}

Write-Host "PASS: KKD2 16:02 reference pose is character=120 deg, separate arm=12 deg."
