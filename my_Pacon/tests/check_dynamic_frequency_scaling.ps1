$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -LiteralPath (Join-Path $root 'main/fluid_pendant.c') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $root 'main/CMakeLists.txt') -Raw
$defaults = Get-Content -LiteralPath (Join-Path $root 'sdkconfig.defaults') -Raw
$sdkconfig = Get-Content -LiteralPath (Join-Path $root 'sdkconfig') -Raw
$failures = [System.Collections.Generic.List[string]]::new()

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        $failures.Add($Message)
    }
}

Require ($defaults.Contains('CONFIG_PM_ENABLE=y')) 'sdkconfig.defaults must enable ESP-IDF power management.'
Require ($sdkconfig.Contains('CONFIG_PM_ENABLE=y')) 'The canonical build sdkconfig must enable ESP-IDF power management.'
Require ($defaults.Contains('# CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set')) 'Tickless Idle must remain disabled for the DFS-only step.'
Require ($main.Contains('#include "esp_pm.h"')) 'The firmware must include the public ESP-IDF PM API.'
Require ($cmake -match '(?s)REQUIRES.*esp_pm') 'The main component must declare its esp_pm dependency.'
Require ($main -match '(?s)esp_pm_config_t config.*?[.]max_freq_mhz[ ]*=[ ]*CPU_FREQ_MAX_MHZ.*?[.]min_freq_mhz[ ]*=[ ]*CPU_FREQ_IDLE_MHZ.*?[.]light_sleep_enable[ ]*=[ ]*false') 'DFS must be configured for 80-240 MHz with Light Sleep disabled.'
Require ($main -match 'esp_pm_lock_create[(]ESP_PM_CPU_FREQ_MAX,[ ]*0,[ ]*"pacon_ui"') 'UI work must use an ESP_PM_CPU_FREQ_MAX lock.'
Require ($main -match 'esp_pm_lock_create[(]ESP_PM_CPU_FREQ_MAX,[ ]*0,[ ]*"pacon_net"') 'Network work must use a separate ESP_PM_CPU_FREQ_MAX lock.'
Require ($main -match '(?s)render_at_max_cpu[(]render_home_frame[)].*?render_at_max_cpu[(]render_apps_frame[)].*?render_at_max_cpu[(]render_usb_disk_frame[)]') 'Home, launcher, and USB rendering must hold the UI CPU lock.'
Require ($main -match '(?s)UI_SCREEN_OUO.*?ui_cpu_lock_acquire[(][)].*?step_ouo[(][)].*?render_ouo_frame[(][)].*?ui_cpu_lock_release') '0u0 update and rendering must share one max-frequency window.'
Require ($main -match '(?s)const bool cpu_lock_acquired = ui_cpu_lock_acquire[(][)];.*?int64_t physics_start_us.*?step_fluid[(][)].*?render_frame[(][)].*?ui_cpu_lock_release') 'Fluid physics and rendering must share one max-frequency window.'
Require ($main -match '(?s)network_cpu_lock_acquire[(][)].*?skyorb_locate_from_network_ip[(][)].*?network_cpu_lock_release') 'IP-location TLS must hold the network max-frequency lock.'
Require ($main -match '(?s)network_cpu_lock_acquire[(][)].*?skyorb_fetch_aircraft[(][)].*?network_cpu_lock_release') 'ADS-B HTTPS must hold the network max-frequency lock.'
Require ($main -match '(?s)bool association_lock_acquired = false;.*?s_wifi_connect_requested.*?association_lock_acquired = network_cpu_lock_acquire[(][)];.*?skyorb_connect_saved_station[(][)].*?s_skyorb_wifi_connected \|\| !s_wifi_should_connect.*?network_cpu_lock_release[(]association_lock_acquired[)]') 'Scan/WPA/DHCP must hold the network max-frequency lock until GOT_IP or a terminal failure.'
Require ($main -notmatch '[.]light_sleep_enable[ ]*=[ ]*true') 'Light Sleep must not be enabled in the DFS-only step.'

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host 'PASS: 80-240 MHz DFS is enabled; UI/animation work holds max CPU frequency; Tickless Idle and Light Sleep remain disabled.'
