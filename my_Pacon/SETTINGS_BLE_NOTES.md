# PACON Settings and BLE update

Date: 2026-08-09

## Current firmware

- `main/ble_pacon.c` and `main/ble_pacon.h` add the verified NimBLE control channel.
- The device advertises as `PACON-BLE-TEST` using the same UUIDs tested with nRF Connect.
- The GATT command characteristic still accepts the validation command `PING` and returns `PONG`.
- The Settings screen is now a watch-style page: fixed header, vertically scrollable cards, Wi-Fi, Bluetooth, brightness, SkyOrb, and system status.
- Settings scrolling now transfers only the card viewport after the first frame; the fixed header is refreshed only when its BLE state changes.
- The retired `PACON-Sky` access point and embedded HTTP configuration server have been removed from the formal firmware.
- Wi-Fi is STA-only. Credentials are managed through BLE or the board's saved-profile page; the HTTP client remains available for radar data and IP geolocation.
- Bluetooth is enabled at boot by default and can be switched off from Settings. Disabling it also terminates an active BLE connection.
- BLE is initialized after SH8601 setup. The panel needs two large internal-DMA stripes; initializing NimBLE first caused `SH8601 initialization failed: ESP_ERR_NO_MEM`.
- Display brightness is controlled by the slider and persisted in NVS namespace `pacon_ui` under key `brightness`.

## Hardware test procedure

1. Build with ESP-IDF 5.4.3 from VS Code or with `idf.py build`.
2. Flash only after the board is in download mode.
3. Open nRF Connect and scan for `PACON-BLE-TEST`.
4. Connect, read the status characteristic, then write ASCII `PING` to the command characteristic and confirm a `PONG` notification.
5. Open Settings on the board, verify the Bluetooth switch and brightness slider, then confirm that disabling Bluetooth disconnects the nRF Connect link.

## Command protocol now available

The command characteristic accepts ASCII lines.  Use nRF Connect's write action
with no response disabled for the quickest test:

```text
PING
GET STATUS
GET SETTINGS
GET HELP
SET BRIGHTNESS 0..100
SET RANGE 0..5
SET LOCATION <latitude> <longitude>
SET AUTO_LOCATION
SET WIFI <ssid>|<password>
MEDIA_BEGIN <name.rgb565> <bytes> <frames> <fps>
MEDIA_DATA <offset> <hex-bytes>
MEDIA_END
MEDIA_ABORT
```

`PING` returns `PONG`.  Settings writes are validated and persisted; brightness
is applied by the main UI task so the NimBLE host never touches the SH8601 DMA
path directly.  Status responses intentionally omit the Wi-Fi password.
Media transfer accepts native `475 x 466` RGB565 frames.  `MEDIA_BEGIN` creates
an on-NAND `.part` file, `MEDIA_DATA` writes sequential hexadecimal chunks (up
to 108 bytes per command), and `MEDIA_END` atomically renames the completed
file and asks the main UI task to rescan `/sdnand/media`.  The reader task is
paused during transfer so a frame cannot be read while it is being replaced.
The filename should include the desired animation rate, for example
`misaka_8fps.rgb565`.  The binary/Android transport can later use the same
state machine without changing the media format.
If the directory was empty when the device booted, the first committed upload
also creates the NAND reader/cache runtime lazily; no reboot is required.

## Build record

The current `my_Pacon` application builds successfully with ESP-IDF v5.4.3. The generated binary is `build/pacon_fluid_pendant.bin`; the smallest app partition still has substantial free space. The post-flash COM11 boot check confirmed SH8601, BLE advertising, AXP2101, FT3168, QMI8658, and SD NAND initialization.

The app-launcher upward return animation now uses the same two-stripe DMA pipeline as the home screen, so CPU composition overlaps QSPI transfer instead of waiting after every stripe.
