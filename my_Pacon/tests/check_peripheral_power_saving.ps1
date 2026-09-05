$ErrorActionPreference = 'Stop'
$source = Get-Content (Join-Path $PSScriptRoot '..\main\fluid_pendant.c') -Raw

function Require-Pattern([string]$pattern, [string]$message) {
    if ($source -notmatch $pattern) { throw $message }
}

Require-Pattern '#define FT3168_REG_POWER_MODE\s+0xA5[\s\S]{0,120}FT3168_POWER_MONITOR\s+0x01' `
    'FT3168 Monitor must use the established 0xA5 power-mode register.'
Require-Pattern 'touch_validate_monitor_on_shared_i2c[\s\S]{0,2200}ADDR_TOUCH, FT3168_REG_POWER_MODE[\s\S]{0,500}ADDR_AXP2101[\s\S]{0,300}ADDR_QMI8658[\s\S]{0,300}ADDR_PCF85063[\s\S]{0,400}ADDR_TOUCH, FT3168_REG_POWER_MODE' `
    'Monitor must be gated by a touch/AXP/IMU/RTC/touch shared-I2C round trip.'
Require-Pattern 'touch_enter_monitor[\s\S]{0,1600}FT3168_REG_GESTURE_ENABLE, 0x00[\s\S]{0,900}FT3168_REG_POWER_MODE, FT3168_POWER_MONITOR' `
    'Watch gestures must be disabled before FT3168 enters Monitor.'
Require-Pattern 'write_err == ESP_OK[\s\S]{0,700}s_touch_monitor_active = true[\s\S]{0,500}Monitor write failed; keeping Active' `
    'FT3168 Monitor must accept an I2C write ACK and retain an Active fallback on write failure.'
Require-Pattern 'if \(display_power_inactive\(\)\) return;[\s\S]{0,1600}hardware watch gestures enabled' `
    'Watch gesture setup must stay disabled throughout OLED sleep/wake.'
Require-Pattern 'const uint8_t touch_count = count & 0x0F;[\s\S]{0,1400}s_touch_monitor_active = false' `
    'A Monitor touch must record automatic Active restore before requesting OLED wake.'
Require-Pattern 's_touch_monitor_active = false;[\s\S]{0,1000}if \(display_power_inactive\(\)\) \{\s+display_note_activity\(\)' `
    'A Monitor touch must remain wake-only on a sleeping display.'

Require-Pattern '#define QMI8658_CTRL2_8G_250HZ\s+0x25[\s\S]{0,100}QMI8658_CTRL2_8G_3HZ_LP\s+0x2F' `
    'QMI8658 must retain 250 Hz tilt and use the documented 3 Hz low-power ODR.'
Require-Pattern 'imu_set_power_state[\s\S]{0,500}QMI8658_REG_CTRL7, 0x00[\s\S]{0,400}QMI8658_CTRL2_8G_250HZ[\s\S]{0,120}QMI8658_CTRL2_8G_3HZ_LP[\s\S]{0,400}QMI8658_REG_CTRL7, 0x01' `
    'QMI8658 transitions must pause the sensor before selecting and enabling an ODR.'
Require-Pattern 's_display_power_state == DISPLAY_POWER_SLEEPING[\s\S]{0,180}requested = IMU_POWER_PAUSED[\s\S]{0,250}s_ui_screen == UI_SCREEN_FLUID[\s\S]{0,180}s_ui_screen == UI_SCREEN_OUO && s_ouo_tilt_reactions' `
    'IMU policy must pause on sleep and reserve 250 Hz for tilt-driven pages.'
Require-Pattern 's_imu_power_state != IMU_POWER_TILT_RATE\) return;' `
    'Tilt reads must not access a paused or low-rate IMU.'
Require-Pattern 'display_service_power\(now\);\s+peripheral_service_power\(now\);' `
    'Peripheral policy must follow the AMOLED state machine in both UI loops.'

Write-Host 'Peripheral power-saving regression checks passed.'
