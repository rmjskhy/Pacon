$ErrorActionPreference = 'Stop'
$firmwarePath = Join-Path $PSScriptRoot '..\main\fluid_pendant.c'
$firmware = Get-Content -Raw $firmwarePath
$projectCMakePath = Join-Path $PSScriptRoot '..\CMakeLists.txt'
$projectCMake = Get-Content -Raw $projectCMakePath
$failures = [System.Collections.Generic.List[string]]::new()

$checks = [ordered]@{
    'USB MSC boot handoff must use an RTC-retained one-shot magic.' = 'RTC_NOINIT_ATTR\s+static\s+uint32_t\s+s_usb_msc_boot_magic'
    'USB ON must request a controlled reboot into dedicated MSC mode.' = 's_usb_msc_boot_magic\s*=\s*USB_MSC_BOOT_MAGIC;[\s\S]{0,320}s_usb_msc_reboot_requested\s*=\s*true;'
    'app_main must sample the dedicated USB boot request.' = 'const\s+bool\s+usb_msc_boot\s*=\s*usb_msc_boot_requested\(\);'
    'Dedicated USB startup must exclude BLE/media and begin only after local UI initialization.' = 'if\s*\(!usb_msc_boot\)[\s\S]{0,700}ble_pacon_init\(\);[\s\S]{0,2400}\}\s*else\s*\{[\s\S]{0,500}init_touch\(\);[\s\S]{0,700}start_usb_msc_mode\(\);'
    'Normal VFS/media initialization must be skipped in dedicated USB mode.' = 'if\s*\(!usb_msc_boot\)[\s\S]{0,1800}init_sd_nand_read_only_probe\(\);[\s\S]{0,800}init_home_external_media\(\)'
    'USB boot magic may only clear after TinyUSB starts successfully.' = 's_usb_msc_started\s*=\s*true;[\s\S]{0,220}s_usb_msc_boot_magic\s*=\s*0;'
    'Dedicated MSC boot must block the retained ON contact until release.' = 'init_touch\(\);[\s\S]{0,260}block_touch_until_release\(\);'
    'USB OFF must require both a release and a 1.5 second arming delay.' = 's_usb_msc_exit_arm_after\s*=\s*xTaskGetTickCount\(\)\s*\+\s*pdMS_TO_TICKS\(1500\);[\s\S]{0,12000}!s_usb_msc_exit_armed'
    'Dedicated MSC must enter the minimal loop immediately after TinyUSB startup.' = 's_usb_msc_result\s*=\s*start_usb_msc_mode\(\);[\s\S]{0,260}run_usb_msc_screen_loop\(\);'
    'Dedicated MSC prepaint must show the requested ON state before TinyUSB starts.' = 's_usb_msc_ui_on\s*=\s*true;[\s\S]{0,180}render_usb_disk_frame\(\);[\s\S]{0,180}start_usb_msc_mode\(\);'
    'USB OFF must use a dedicated raw touch reader with init retry and a broad lower-screen target.' = 'static\s+void\s+usb_msc_poll_exit_touch[\s\S]{0,1600}!s_touch_ready[\s\S]{0,700}init_touch\(\)[\s\S]{0,1600}ADDR_TOUCH[\s\S]{0,2400}y\s*>=\s*330[\s\S]{0,900}s_usb_msc_exit_requested\s*=\s*true;'
    'Dedicated USB loop must wait for explicit raw OFF touch.' = 'run_usb_msc_screen_loop\(void\)[\s\S]{0,500}usb_msc_poll_exit_touch\(\);[\s\S]{0,1000}s_usb_msc_exit_requested'
    'USB OFF restart must tear down TinyUSB PHY and hold a one-second detach window.' = 'static\s+void\s+usb_msc_disconnect_for_restart[\s\S]{0,1000}tinyusb_driver_uninstall\(\)[\s\S]{0,1000}pdMS_TO_TICKS\(1000\)[\s\S]{0,900}esp_restart\(\);'
    'USB OFF must explicitly return the shared internal PHY to USB Serial/JTAG before restart.' = 'static\s+esp_err_t\s+usb_msc_restore_serial_jtag_phy[\s\S]{0,900}USB_PHY_CTRL_SERIAL_JTAG[\s\S]{0,500}USB_PHY_TARGET_INT[\s\S]{0,800}usb_new_phy\('
    'USB ON must paint accepted green feedback before the dedicated-mode reboot.' = 's_usb_msc_ui_on\s*=\s*true;[\s\S]{0,180}s_usb_disk_dirty\s*=\s*true;[\s\S]{0,260}s_usb_msc_reboot_requested\s*=\s*true;'
    'USB switch must be raised clear of the round panel edge with a matching normal-mode hit region.' = 'home_in_round_rect\(x,\s*y,\s*170,\s*365,\s*305,\s*417,\s*26\)[\s\S]{0,300}y\s*-\s*391[\s\S]{0,110000}home_in_round_rect\(x,\s*y,\s*155,\s*350,\s*320,\s*432,\s*24\)'
    'USB page must sample the launcher contact release before its slow full-frame render.' = 'else\s+if\s*\(s_ui_screen\s*==\s*UI_SCREEN_USB_DISK\)[\s\S]{0,260}poll_touch\(\);[\s\S]{0,260}if\s*\(s_usb_disk_dirty\)[\s\S]{0,100}render_usb_disk_frame\(\);'
    'OLED framebuffer must be cleared while dark before the panel is shown.' = 'lcd_set_brightness\(0\)[\s\S]{0,2200}draw_bitmap[\s\S]{0,900}esp_lcd_panel_disp_on_off\(s_lcd_panel,\s*true\)'
    'Minimal MSC loop must not render or service media.' = 'static\s+void\s+run_usb_msc_screen_loop\(void\)[\s\S]{0,1800}vTaskDelay\(pdMS_TO_TICKS\(50\)\);'
}

foreach ($check in $checks.GetEnumerator()) {
    if ($firmware -notmatch $check.Value) { $failures.Add($check.Key) }
}


if ($firmware -match 'usb_msc_(mount_changed|premount_changed|update_host_state)|callback_(pre)?mount_changed\s*=') {
    $failures.Add('Safe eject must not auto-restart PACON; only explicit OFF may request exit.')
}

if ($firmware -notmatch 'remove\(s_ble_media_scan_path\);[\s\S]{0,600}stat\(s_ble_media_scan_path') {
    $failures.Add('MEDIA_DELETE must verify that the exact path is absent before acknowledging success.')
}

if ($projectCMake -match 'smartknob[\\/]superdial_port[\\/].*tinyusb') {
    $failures.Add('PACON must resolve TinyUSB from its own managed dependency, not the smartknob project.')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host ('FAIL: ' + $_) -ForegroundColor Red }
    exit 1
}
Write-Host 'USB DISK BOOT HANDOFF CHECK: PASS'
