$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'main\fluid_pendant.c') -Raw
$mic = Get-Content -LiteralPath (Join-Path $root 'main\pacon_mic_test.c') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'main\pacon_mic_test.h') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $root 'main\CMakeLists.txt') -Raw
function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
Require ($cmake.Contains('pacon_mic_test.c')) 'CMake does not compile the microphone module'
Require ($mic.Contains('I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG')) 'Microphone must use Philips I2S timing'
Require (-not $mic.Contains('I2S_STD_MSB_SLOT_DEFAULT_CONFIG')) 'MSB timing would sample the microphone one clock early'
Require ($mic.Contains('I2S_SLOT_MODE_STEREO') -and $mic.Contains('I2S_STD_SLOT_BOTH')) 'Microphone requires a 64-SCK stereo frame'
Require ($mic.Contains('GPIO_NUM_39') -and $mic.Contains('GPIO_NUM_40') -and $mic.Contains('GPIO_NUM_47')) 'Verified microphone pins changed'
Require ($mic.Contains('"RIFF"') -and $mic.Contains('"WAVEfmt "') -and $mic.Contains('"data"')) 'WAV header is incomplete'
Require ($mic.Contains('%s.tmp') -and $mic.Contains('rename(')) 'Recording must commit through a temporary WAV file'
Require ($mic.Contains('MIC_TASK_STACK_BYTES') -and
         $mic.Contains('static int32_t raw[MIC_FRAMES_BLOCK * 2U]') -and
         $mic.Contains('static int16_t pcm[MIC_FRAMES_BLOCK]')) `
    'Microphone capture buffers must not consume the recording task stack'
Require ($mic.Contains('#define MIC_DIGITAL_GAIN_X  4') -and
         $mic.Contains('apply_soft_limited_gain') -and
         $mic.Contains('sample = apply_soft_limited_gain(sample);')) `
    'Recording must apply 12 dB digital gain through the soft limiter'
Require ($header.Contains('PACON_MIC_MAX_RECORD_MS   10000U')) 'Diagnostic recording must remain bounded to ten seconds'
Require ($main.Contains('UI_SCREEN_MIC_TEST')) 'Microphone screen is not part of the UI state machine'
Require ($main.Contains('++s_settings_debug_taps >= 3U')) 'Hidden Settings triple-tap entrance is missing'
Require ($main.Contains('pacon_mic_start_recording(MIC_TEST_WAV_PATH)')) 'Record action is not wired to the WAV path'
Require ($main.Contains('pacon_mic_close();')) 'I2S is not released when leaving the diagnostic'
Require ($main.Contains('(int32_t)(now - s_mic_test_last_frame) >= pdMS_TO_TICKS(80)')) 'Live screen refresh cadence is missing'
Write-Host 'Microphone diagnostic wiring OK'
