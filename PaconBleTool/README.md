# PACON BLE Tool

这是 PACON 的第一版 Android BLE 控制工具，当前目标是先验证手机端通信链路：

1. 扫描并连接 `PACON-BLE-TEST`；
2. 自动发现 PACON 自定义服务；
3. 开启响应特征通知；
4. 发送 `PING`、`GET STATUS` 或其他短文本命令。

## 打开和运行

在 Android Studio 中打开本目录，等待 Gradle 同步完成，然后把手机通过 USB
连接并开启 USB 调试，点击 Run。第一次运行需要授予附近设备权限。手机必须支持
Bluetooth LE；不要使用模拟器验证 BLE。

如果 Android Studio 没有自动找到 SDK，可在 `local.properties` 中加入：

```text
sdk.dir=D\\:\\Android\\Sdk
```

当前工程使用 Android SDK Platform 37.0，要求 Android Gradle Plugin 9.1.1
和 Gradle 9.3.1。若 Android Studio 提示需要下载 Gradle 或插件，请允许同步完成。

## PACON 特征

当前固件使用以下自定义 UUID（nRF Connect 中显示为 Unknown Service）：

```text
Service  014e4f43-4150-5091-2a4d-8c5b2143709a
Command  034e4f43-4150-5091-2a4d-8c5b2143709a  WRITE
Response 044e4f43-4150-5091-2a4d-8c5b2143709a  NOTIFY/READ
```

媒体上传暂未加入这个最小验证工程。当前固件媒体协议使用 ASCII Hex 分块，完整
实现时需要严格按响应通知逐块发送，不能一次性连续写入。

## Android media upload

The Android tool now has an `UPLOAD / CONVERT IMAGE` button. It accepts a .rgb565 file
produced for PACON's native 475x466 RGB565 format. A single frame is 442700
bytes; an animation is an integer number of frames limited by the phone's
available memory and the device storage. The filename may include 8fps (or another 1-12 fps value), otherwise
8 fps is used.

The uploader requests MTU 247, sends MEDIA_BEGIN, waits for each
OK MEDIA_DATA offset/size notification before sending the next chunk, and
finishes with MEDIA_END. Do not disconnect or close the app during a transfer.
When the firmware exposes the media data characteristic ending in `...05`,
the tool uses raw binary packets with an eight-packet cumulative-ACK window.
Older firmware falls back to the sequential hexadecimal command path.

The picker also accepts ordinary PNG/JPEG/WebP images.  They are center-cropped
and scaled to PACON's 475x466 panel, converted to little-endian RGB565, and
uploaded as a single `.rgb565` frame.  A raw `.rgb565` file can still contain
multiple complete frames; include a name such as `8fps` to select its playback
rate.

The media management row provides upload progress, cancel, retry, and a
MEDIA LIST button. Tap an item to request immediate playback on PACON;
long-press an item to issue MEDIA_DELETE. The firmware management commands
are MEDIA_LIST, MEDIA_INFO <index>, MEDIA_PLAY <name>, and MEDIA_DELETE
<name>.

Control commands such as `GET STATUS`, `MEDIA_LIST`, `MEDIA_INFO`, and
`MEDIA_DELETE` use normal acknowledged GATT writes in the Android client. This
keeps command and notification ordering deterministic on Android; only bulk
binary media packets use write-without-response for throughput.

Media catalog queries allow up to 15 seconds because the firmware may need to
wait for the NAND display reader while enumerating and stat'ing files. Bulk
binary upload acknowledgements retain their separate shorter window timeout.

## Device settings

Tap `设备设置` after connecting to open the device panel. It uses the existing
BLE command protocol, so no firmware media changes are required:

- display brightness: `SET BRIGHTNESS 0..100`
- SkyOrb range: `SET RANGE 0..3`
- manual coordinates: `SET LOCATION latitude longitude`
- automatic location mode: `SET AUTO_LOCATION`
- saved Wi-Fi: `SET WIFI ssid|password`

`读取` sends `GET SETTINGS`; `保存` writes the coordinate and Wi-Fi fields.
The brightness and range sliders apply when released, and the panel reports
the device response below the controls.
