$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'main\fluid_pendant.c') -Raw
$mic = Get-Content -LiteralPath (Join-Path $root 'main\pacon_mic_test.c') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'main\pacon_mic_test.h') -Raw
$manifest = Get-Content -LiteralPath (Join-Path $root 'main\idf_component.yml') -Raw
$partitions = Get-Content -LiteralPath (Join-Path $root 'partitions.csv') -Raw
$defaults = Get-Content -LiteralPath (Join-Path $root 'sdkconfig.defaults') -Raw
function Require([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

Require ($main.Contains('"RECORDER"')) 'Recorder page title is missing'
Require ($main.Contains('mic_test_enter();') -and
         $main.Contains('home_in_circle(x, y, 142, 385, 44)')) `
    'Recorder must have a visible launcher touch target'
Require ($main.Contains('pacon_mic_start_recording(s_mic_record_path)')) `
    'Tap-to-record action is not wired to the selected WAV path'
Require ($main.Contains('pacon_mic_set_voice_commands_enabled(!status.voice_commands_enabled)')) `
    'Voice-command switch does not control the microphone engine'
Require ($main.Contains('pacon_mic_take_voice_command()') -and
         $main.Contains('PACON_MIC_VOICE_START') -and
         $main.Contains('PACON_MIC_VOICE_STOP')) `
    'Start/stop voice commands are not wired to the Recorder UI'
Require ($header.Contains('voice_commands_enabled') -and
         $header.Contains('voice_commands_ready')) `
    'Voice-command state is not exposed to the UI'
Require ($mic.Contains('esp_mn_handle_from_name') -and
         $mic.Contains('s_multinet->detect') -and
         $mic.Contains('"wo cao"') -and -not $mic.Contains('"ei you"') -and
         $mic.Contains('s_status.state == PACON_MIC_RECORDING')) `
    'MultiNet Chinese record toggle recognition is incomplete'
Require ($manifest.Contains('espressif/esp-sr')) 'ESP-SR dependency is missing'
Require ($partitions -match '(?m)^model,\s*data,') 'ESP-SR model partition is missing'
Require ($defaults.Contains('CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y') -and
         $defaults.Contains('CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH=y')) `
    'ESP-SR must preserve Wi-Fi internal RAM headroom'
Require ($defaults.Contains('CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y') -and
         $main.Contains('EXT_RAM_BSS_ATTR static fluid_particle_t s_particles')) `
    'Fluid workspace must stay out of Wi-Fi internal RAM'
Require ($defaults.Contains('CONFIG_SR_MN_CN_MULTINET6_QUANT=y')) `
    'Chinese MultiNet6 model is not enabled'
Require ($partitions -match '(?m)^model,\s*data,\s*,\s*,\s*4500K') `
    'Model partition is too small for MultiNet6'
Write-Host 'Recorder app and voice-command wiring OK'
