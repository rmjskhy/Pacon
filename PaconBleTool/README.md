# PACON Companion 0.2

Android 配套应用使用三个底部入口：

- **设备**：扫描/连接 PACON、读取电量、切换主界面/流体/OuO。
- **素材**：图片裁剪与上传、动画上传、设备素材列表、播放和删除。
- **设置**：显示、Wi-Fi、雷达、时钟与闹钟；高级选项中可开启调试模式。

调试模式默认关闭，开关会保存在手机上。PING、原始状态查询、自定义命令、
遥控快门测试、测试响铃、日志和日志复制/清空只在开启时显示。普通操作结果仍显示在页面底部。
遥控快门测试入口位于「设置 → 高级选项 → 调试模式」，不影响 PACON 本机的拍照功能。
日志有长度上限，Wi-Fi 写入命令中的凭据会隐藏；分享日志前仍应检查设备信息。

应用名称改为 PACON，包名和签名流程保持不变，原有安装可覆盖升级。

## 远程切换界面

连接后在设备页点击「主界面」「流体」或「OuO」。主界面指 PACON 的媒体首页，
不是功能图标列表。此功能需要同时更新配套 PACON 固件：

```text
SET UI HOME|FLUID|OUO
→ OK UI QUEUED HOME|FLUID|OUO
GET UI
→ {"ok":true,"screen":"HOME","state":"done"}
```

SET UI 仅入队，显示线程负责结束当前手势、取消页面过渡、唤醒屏幕并执行切换。
GET UI 的 state 为 idle/pending/done/busy；其他页面的 screen 为 OTHER。
手机只在读到目标页面且 state=done 时显示成功。媒体传输或 USB 存储占用时拒绝
切换，避免绕过存储访问保护。旧固件仍可使用其他功能，但切屏会提示更新固件。
连接后不会自动查询素材，点击「刷新设备状态」可手动确认当前页面和电量。

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

## 手机拍照

先在手机系统蓝牙设置中配对 `PACON-BLE-TEST`，并接受它作为 HID 输入设备；
相机应用需要支持“音量键拍照”。然后在本工具中扫描并连接同一个 PACON，点
`拍照 / 音量键快门` 即可发送快门命令。系统 HID 连接负责接收 Volume Increment
报告，工具连接负责发送命令，固件允许这两条连接同时存在。也可以断开本工具，
直接在 PACON 拍照功能页点击快门按钮。

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

The picker also accepts ordinary PNG/JPEG/WebP images. Before conversion it
opens a native-aspect preview: drag with one finger and pinch with two fingers
to choose the subject position and zoom inside the round-screen guide. The
confirmed 475x466 crop is converted to little-endian RGB565 and uploaded as a
single `.rgb565` frame. A raw `.rgb565` file bypasses the crop screen and can still contain
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
- automatic screen-off time: `SET SCREEN TIMEOUT 0|15|30|60|120|300`; `0` disables panel-off but keeps idle dimming
- SkyOrb radius: `SET RANGE 0..5` maps to `5/10/15/25/35/50 km`; 50 km is the center-to-edge radius, not the diameter
- manual coordinates: `SET LOCATION latitude longitude`
- automatic location mode: `SET AUTO_LOCATION`
- saved Wi-Fi: `SET WIFI ssid|password`

`读取` sends `GET SETTINGS`; `保存` writes the coordinate and Wi-Fi fields.
The brightness and range sliders apply when released, and the panel reports
the device response below the controls.

## Clock and alarm

The `时钟与闹钟` panel uses the same acknowledged BLE command path:

- `GET CLOCK` reads RTC validity, current time, source, sync state, alarm and watch style;
- custom date/time sends `SET TIME yyyy-mm-dd hh:mm:ss CUSTOM`;
- Bluetooth calibration sends the phone's local time with source `BLE`;
- Wi-Fi calibration sends `SYNC WIFI TIME`; PACON waits for its saved Wi-Fi connection, obtains SNTP time and writes PCF85063;
- alarm controls send `SET ALARM hh:mm`, `SET ALARM OFF`, or `STOP ALARM`;
- watch style sends `SET WATCH STYLE 0|1`.

Without a calibration request the firmware continues to use PCF85063. The
alarm is stored on PACON and GPIO48 sounds at the configured RTC time, so the
phone does not need to remain connected.
