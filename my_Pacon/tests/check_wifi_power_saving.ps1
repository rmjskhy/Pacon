$ErrorActionPreference = 'Stop'
$source = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw

function Require-Pattern([string]$pattern, [string]$message) {
    if ($source -notmatch $pattern) { throw $message }
}

Require-Pattern 'esp_wifi_set_config\(WIFI_IF_STA,[\s\S]{0,300}esp_wifi_set_ps\(WIFI_PS_NONE\)[\s\S]{0,200}esp_wifi_connect\(\)' `
    'WPA association must remain fully awake on the BLE coexistence hardware.'
Require-Pattern 'IP_EVENT_STA_GOT_IP[\s\S]{0,600}esp_wifi_set_ps\(WIFI_PS_MIN_MODEM\)' `
    'Minimum modem power save must be enabled only after DHCP completes.'
if ($source.IndexOf('esp_wifi_set_ps(WIFI_PS_NONE)') -gt
    $source.IndexOf('IP_EVENT_STA_GOT_IP')) {
    throw 'The first full-power association setup must precede GOT_IP.'
}
Require-Pattern '#define SKYORB_WIFI_IDLE_GRACE_MS\s+5000' `
    'SkyOrb must retain a short post-HTTPS grace period before pausing STA.'
Require-Pattern 'if \(success\)[\s\S]{0,400}s_wifi_radio_pause_after' `
    'A successful aircraft refresh must schedule radio pause.'
Require-Pattern 'static void skyorb_pause_radio\(void\)[\s\S]{0,1400}esp_wifi_stop\(\)' `
    'The network task must have a dedicated radio-pause path.'
Require-Pattern 'STA paused; switch remains ON' `
    'Automatic pause must remain distinct from the saved Wi-Fi switch.'
Require-Pattern 's_ui_screen == UI_SCREEN_SKYORB[\s\S]{0,400}SKYORB_FETCH_PERIOD_MS' `
    'An open SkyOrb page must resume STA when its cached refresh becomes due.'
Require-Pattern 's_wifi_radio_reconnect = s_skyorb_wifi_connected' `
    'Automatic pause must remember whether the saved station needs reconnecting.'
Require-Pattern 'STA restarted; reconnecting saved profile' `
    'Radio resume must re-run association and DHCP for the saved profile.'
Require-Pattern 'static void wifi_settings_enter\(void\)[\s\S]{0,300}skyorb_start_network_task\(\)' `
    'Entering Wi-Fi settings must resume the radio for scanning.'
Require-Pattern 'SYNC WIFI TIME[\s\S]{0,300}s_wifi_radio_requested = s_skyorb_wifi_enabled' `
    'Wi-Fi time sync must wake a power-paused station.'
Require-Pattern 'SkyOrb: returned home[\s\S]{0,1}' `
    'SkyOrb return path is missing.'
Require-Pattern 's_ui_screen = UI_SCREEN_HOME;[\s\S]{0,160}s_wifi_radio_pause_requested = true' `
    'Leaving SkyOrb must request radio pause.'

Write-Host 'Wi-Fi power-saving regression checks passed.'
