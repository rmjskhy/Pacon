# PACON 开发交接记录

最后更新：2026-08-05

## 工程与环境

- 正式应用：`D:\my_project\Pacon\my_Pacon`
- 首板诊断：`D:\my_project\Pacon\test_Pacon`
- ESP-IDF：`v5.4.3-277-gaac5b37fa3`；VS Code ESP-IDF 插件可直接打开正式应用目录。
- USB Serial/JTAG：`COM11`，监视器波特率 `115200`。
- `my_Pacon` 通过相对路径复用 `../Pacon/components/esp_lcd_sh8601`，不可随意移动这两个工程的相对位置。

## 已验证正常

- ESP32-S3、16 MiB Flash、8 MiB OPI PSRAM；烧录哈希校验正常。
- SH8601 QSPI AMOLED、FT3168 触摸（I2C `0x38`）、QMI8658 IMU（I2C `0x6A`，ID `0x05`）。
- 首板诊断还确认了 SD NAND、Wi-Fi 与 BLE 可工作；详见 `../test_Pacon/TEST_REPORT.md`。

## 当前流体吊坠

- 实现文件：`main/fluid_pendant.c`。
- 96 个带分离碰撞和圆形边界的粒子；IMU 倾斜决定重力，触摸产生冲击并切换蓝、靛紫、青绿三组低饱和配色。
- 渲染采用 110x108 密度场、2x2 像素采样和 64 行内部 RAM DMA 条带。密度场仍在 PSRAM 中计算；每条 QSPI 传输完成后才复用条带，避免显示源缓冲竞争。
- 2026-07-23 的双完整 PSRAM 帧缓冲版本曾测得约 **27.8 FPS**；它现已被更可靠的内部条带路径替代，待后续重新测量最终帧率。每次显示帧仍执行两次物理子步以提高流动速度。
- 2026-07-23 后续照片出现青色、洋红色和黄色马赛克；这不可能由单一金色调色板生成，故先将显示改为完整帧 DMA。待实物确认马赛克消失后，再调整粒子数量、重力和配色，避免混淆显示传输与美术效果。
- 2026-07-24 美术迭代：改为较紧实的椭圆粒子初始形状；密度核扩大并进行两次 3x3 二项平滑；用软覆盖率将液面边缘混入黑色背景，并以低强度边缘高光取代硬色环。保持 2x2 采样和每帧两次物理子步，待烧录后以实物画面继续校色。
- 2026-07-24 显示故障闭环：流体画面出现黄绿/洋红碎块，但 `test_Pacon` 的静态六色图正常。先后排除双缓冲复用、整帧 PSRAM DMA 和条带 DMA；在本工程中重放静态色图后确认 QSPI 链路正常。像素诊断显示流体源数据始终满足 `red <= blue`，故问题位于面板数据解释。SH8601 QSPI 路径需要 RGB565 高字节先发送；新增 `rgb565_for_sh8601()` 交换 16 位字节顺序后，实物画面恢复为正确的冷蓝液体。诊断代码已清除。
- 2026-08-01 当前实现改为 Opal simple 风格的 220 个高分辨率液滴、圆形边界与两轮碰撞分离。工作画布位于 PSRAM，但每次传屏仍先复制到内部 DMA 条带，避免 PSRAM 直接 DMA 的历史花屏问题。
- 2026-08-01 局部刷新故障闭环：任意窄 X 窗口的 SH8601 QSPI 刷新会产生明显残影/地址错位；首帧完整刷新正常，随后窄窗口刷新可稳定复现。改为“完整 475 像素行宽 + 仅变动的 Y 范围”后，用户实测残影消失。不要恢复窄 X 窗口优化，除非先在实物上重新验证。
- 该可靠局部刷新路径的实测性能为：两次物理子步约 9.9 ms；传屏约 80 ms；液滴活动区域约 145k–148k 像素（66–67% 整屏），约 11 FPS。当前优先级为显示正确性；若继续优化，应先减少液滴簇的 Y 覆盖范围或实现经实物验证的多区域整行刷新，而非使用窄 X 窗口。

## AXP2101 与电池安全

- AXP2101 地址为 `0x34`，芯片 ID 寄存器 `0x03` 应为 `0x4A`。
- 重新焊接后于 2026-07-23 复测：`0x34` 仍无应答（ID 读失败；而 `0x38`、`0x6A` 仍正常），故 PMIC I2C 问题尚未解决。正式流体固件仅只读探测 PMIC，**不写电源或充电寄存器**。
- 先前测量：USB 输入约 `4.9 V`，空载电池焊盘约 `4.4 V`。在确认 PMIC 配置和实际充电截止电压前，**不要连接普通 4.20 V 锂电池**。
- 后续排查顺序：检查 U1 的焊盘桥连/虚焊、U1 SDA/SCL 至 GPIO1/GPIO2 的通断和上拉，再检查 U1 供电/地；修复后只读 ID，确认 `0x4A` 后才进行电池与充电 LED 测试。
- 2026-08-01 实测更新（覆盖上述旧的“无应答”结论）：更换并正确焊接后，PMIC 可稳定通信，当前芯片 ID 为 `0x47`。运行中可读取 VBUS、BAT、VSYS、充电状态和电量；当前程序将充电目标设为 4.0 V、恒流 50 mA、终止 25 mA。每次连接不同电池前仍须先核对实际截止电压，不能仅凭软件配置判断安全性。

## 蜂鸣器与麦克风：引脚映射更正（2026-07-24）

- `SPKOUT` 接到了 ESP32-S3 的封装脚 36 `SPICLK_N`，其软件 GPIO 编号是 `GPIO48`；`I2S_DAT` 接到了封装脚 37 `SPICLK_P`，其软件 GPIO 编号是 `GPIO47`。
- 先前把封装脚号误映射为 GPIO32/GPIO33。GPIO32 (`SPID`) 和 GPIO33（Octal PSRAM `DQ4`）确实不能复用，因此先前复位不能证明 GPIO48/GPIO47 有 PCB 冲突。
- 对 ESP32-S3R8 的这块板，Flash/PSRAM 完成启动后可以测试 GPIO48 的 LEDC 和 GPIO47 的 I2S 输入；两脚位于 VDD_SPI 电源域，烧录器识别该芯片为 `AP_3v3`，故本板为 3.3 V 逻辑。
- 未焊接的 `1N5819WT` 位于蜂鸣器支路，补焊后再评估发声；麦克风未焊接，当前只能测试 I2S 引脚初始化，不能验证采样。
- 已新增 `test_Pacon/tools/audit_pin_mapping.py`：它直接解析 EasyEDA `.epro2` 原理图，2026-07-24 已校验工厂示例、`Pacon`、`test_Pacon` 与本工程共 57 个 `GPIO_NUM_x` 定义，全部匹配。`GPIO39`/`GPIO40` 的网名仍为 `JTAG_MTCK`/`JTAG_MTDO`，编号和连线正确；待麦克风补焊后再验证其 I2S 时序与采样协议。
- 2026-07-24 已烧录 `test_Pacon` 的蜂鸣器短音测试：GPIO48 的 LEDC 定时器、通道均返回 `ESP_OK`，执行 3 次 2 kHz、80 ms 脉冲后程序继续运行且未复位。用户已确认实际听到短音；GPIO48 映射、软件 PWM 与当前蜂鸣器支路可用。

## 烧录与测试约定

1. 在 VS Code 中以 `my_Pacon` 为根目录，使用 ESP-IDF 5.4.3 执行 Build / Flash。
2. 连接 `COM11`，在 Monitor 中确认 `AXP2101 rework`、`FT3168 touch`、`QMI8658 tilt control` 日志。
3. 当前电池、麦克风、1N5819WT 均不作为本版流体功能的前置条件；勿尝试启用蜂鸣器或 I2S 麦克风测试。

## 2026-08-05：系统 UI 统一

- 审核正式工程全部界面，确认媒体主页、Fluid、0u0 和 SkyOrb 应保留各自的沉浸式画面，但启动器、导航控件、设置页和状态页此前缺少统一视觉语言。
- 应用启动器改为纯黑背景、圆形图标和蜂窝式排列；Fluid、0u0、SkyOrb 的返回/设置控件统一为圆形图标按钮。
- Fluid 设置、0u0 菜单和设备设置改为深灰分组卡片、白色主信息、灰色次信息和系统蓝强调色。
- Fluid 调色盘改为黑色沉浸界面和圆形返回控件；USB Disk 页面改为圆形状态图标和胶囊完成按钮。
- 未改变原有触摸命中区域和界面状态机，避免纯视觉调整破坏已验证的交互逻辑。
- 使用 ESP-IDF 5.4.3 完整编译通过，生成 `build/pacon_fluid_pendant.bin`，大小 `0x12f150`；尚需在实物圆屏上确认边缘留白、图标辨识度和整体比例。
- 后续代码改动继续同步更新 `DESIGN.md` 和本日志；影响使用方式时同时更新 `README.md`。

## 2026-08-06：应用启动器反向返回动画

- 问题：主页下拉进入应用启动器有过渡感，但启动器上滑返回主页时直接切换状态，缺少对称的上滑动画。
- 修复：增加 `APPS_DISMISS_STEPS` 过渡状态。应用启动器上滑或点击底部返回区后，启动器向上移动，主页媒体从底部进入；动画完成后才提交 `UI_SCREEN_HOME` 状态。
- 动画期间暂时锁定启动器点击，防止快速连续触摸打断页面状态；触摸热区和主页下拉逻辑保持不变。
- 仍遵守 SH8601 全宽行传输约束，过渡帧在同一组全宽 DMA 条带中合成，不使用窄 X 窗口。
- 本次修改已用 ESP-IDF 5.4.3 编译通过，固件大小为 `0x12f2c0`；尚未烧录，需在实物上确认动画时长和触摸释放时机。
# 2026-08-09 BLE and vertical Settings update

- Added `main/ble_pacon.c` and `main/ble_pacon.h`; the formal firmware now starts the verified NimBLE peripheral as `PACON-BLE-TEST`.
- Replaced automatic AP startup on entering Settings with a watch-style vertically scrollable Settings page.
- Settings currently exposes Wi-Fi (transitional AP path), Bluetooth enable/disable, brightness persistence, SkyOrb status, and power status.
- Bluetooth disable now terminates an active connection before stopping advertising.
- First flash exposed an internal-RAM ordering issue: NimBLE initialized before SH8601 starved the two internal DMA stripes and returned `ESP_ERR_NO_MEM`. BLE is now initialized after `init_lcd()`; COM11 post-flash logs confirmed the panel, touch, IMU, SD NAND, AXP2101, and BLE all start normally.
- Added `SETTINGS_BLE_NOTES.md`; BLE configuration commands are the next step before removing the temporary AP/HTTP path.
- Settings scroll optimization: retain the fixed header and flush only the 367x356 card viewport during scrolling; a full-screen flush is used only when entering the page.
- App-launcher upward return animation now uses the two-stripe DMA pipeline; the old one-buffer path waited after every stripe and caused visible pauses.
- Build verified with ESP-IDF v5.4.3; no flash was performed in this update.
- 2026-08-09 follow-up: the launcher now caches its static icon page in the PSRAM canvas.  The upward return animation reads cached app pixels and only composes the small portion of the home page entering from below, avoiding repeated full-page icon geometry.
- 2026-08-09 follow-up: page-changing gestures are blocked until FT3168 reports a genuine zero-contact release.  This prevents a stale `s_touch_down` sample after the return DMA animation from turning the next downward pull into an unintended upward swipe.
- 2026-08-09 follow-up: restored the downward home-to-launcher entrance animation with four steps.  Both directions now compose complete transition scanlines from cached home/launcher frames, reducing repeated per-pixel geometry work while keeping touch blocked until release.
- 2026-08-09 follow-up: removed a leftover nested pixel loop in the launcher renderer that multiplied each transition line's work and could starve the main task.  The corrected image was flashed to COM11 and boot logs showed normal home rendering without a watchdog reset during capture.
- 2026-08-09 follow-up: optimized the vertical Settings page by composing only its scroll viewport after entry and pipelining dirty-rectangle transfers through the two LCD stripe buffers.  Full-canvas composition/flush remains for the first Settings frame.
- 2026-08-09 follow-up: Settings text now draws LVGL glyph bitmaps directly instead of rescanning each candidate pixel across the whole string.  The Connectivity icon is also positioned with the scroll offset, removing the fixed blue dot at the upper-left.
- 2026-08-09 follow-up: made the OuO hidden settings/menu long press reliable: expanded the bottom-left candidate area, tolerated up to 30 px of touch jitter, reduced the hold to 560 ms, and also checks the duration on release so a blocked UI frame cannot lose the gesture.

# 2026-08-09 BLE settings command channel

- Kept the nRF Connect `PING`/`PONG` regression command and added an application-owned command callback to the PACON BLE service.
- Added line-oriented commands: `GET STATUS`, `GET SETTINGS`, `GET HELP`, `SET BRIGHTNESS 0..100`, `SET RANGE 0..3`, `SET LOCATION lat lon`, `SET AUTO_LOCATION`, and `SET WIFI ssid|password`.
- BLE brightness writes are applied by the main UI task, then persisted in `pacon_ui`; this avoids touching SH8601 from the NimBLE host task.
- SkyOrb settings are validated, written to the existing `skyorb` NVS namespace, and reflected in the Settings/SkyOrb state without automatically starting the AP.
- The command channel deliberately does not expose the Wi-Fi password in status responses and does not yet implement image/file transfer.

# 2026-08-10 BLE media upload

- Added a first media upload state machine to the existing BLE command characteristic.
- `MEDIA_BEGIN name size frames fps` validates native `475x466` RGB565 dimensions and creates `/sdnand/media/.name.part`.
- `MEDIA_DATA offset hex` accepts sequential chunks up to 108 bytes and flushes each chunk; `MEDIA_END` atomically renames the completed file and schedules a main-task media rescan. `MEDIA_ABORT` removes an incomplete upload.
- The NAND media reader is paused while a transfer is active, preventing a displayed frame from being read while its file is replaced. The filename should include the intended rate, such as `misaka_8fps.rgb565`.
- Fixed the empty-directory edge case: if the device booted before any external media existed, the first successful BLE upload now lazily allocates the PSRAM cache and starts the NAND reader task during the rescan. This keeps the first uploaded image/animation renderable without rebooting.
- Flashed the lazy-initialization build to COM11. Boot logs confirmed SD NAND mounted at `/sdnand`, AXP2101 status reporting remained active, and the display task continued running. The board had no valid external media at boot, so it correctly used the fallback home screen.

# 2026-08-10 Android BLE tool scaffold

- Added `../PaconBleTool`, a dependency-light Java Android project for the phone-side BLE test.
- The first screen scans for `PACON-BLE-TEST`, connects, discovers the PACON custom service, enables response notifications, and sends `PING`/`GET STATUS` or a short custom command.
- The project uses the SDK installed at `D:\\Android\\Sdk`; full media upload is intentionally the next phase after the control link is verified on the user's phone.

# 2026-08-10 Android build-tool alignment

- The SDK Manager reports Android SDK Platform 37.0 and Sources for Android 37.0 as installed.
- Updated `PaconBleTool` from AGP 8.6.1 to 9.1.1 and its Gradle wrapper from 9.3.0 to 9.3.1, matching the official API 37.0 minimum toolchain. BLE source code was not changed.

# 2026-08-10 Android media upload

- After the phone-side BLE scan/connect/notify/PING/GET STATUS/brightness tests passed, added an UPLOAD RGB565 picker to PACON BLE Tool.
- The app requests MTU 247, validates native 475x466 RGB565 files (442700 bytes per frame), infers 1-12 fps from names such as misaka_8fps.rgb565, and sends the existing MEDIA_BEGIN/sequential MEDIA_DATA/MEDIA_END protocol.
- Each chunk waits for the corresponding OK MEDIA_DATA offset/size notification before continuing, preventing BLE write-queue overruns. Raw media transport compiles with Android SDK Platform 37.0; JPEG/PNG/GIF conversion remains a later task.

# 2026-08-10 Android media upload diagnosis

- The phone log reached MTU 256, completed `MEDIA_BEGIN` with `status=0`, and then received `ERR cannot open media temp file`. This proves BLE framing and the Android uploader reached the firmware-side NAND staging step.
- Firmware now checks `mkdir`/`stat` for `/sdnand/media` and logs `errno` when the staging file cannot be opened.
- The temporary upload name changed from a hidden long filename (`.<remote-name>.part`) to the FAT 8.3-compatible `/sdnand/media/PACON.UPL`; the final user filename and atomic rename behavior are unchanged.
- The firmware build was verified successfully; no flash was performed in this update.

# 2026-08-10 Android media upload rename diagnosis

- After the short staging-file fix, the final `MEDIA_DATA` and `MEDIA_END` reached the firmware, but the Android tool then sent `MEDIA_ABORT`. The missing `BLE media: completed` log indicates the commit/rename stage failed, not the BLE transfer.
- `sdkconfig` had `CONFIG_FATFS_LFN_NONE=y`; this allowed the 8.3 staging file but could not rename it to user names such as `misaka_lightning_landscape.rgb565`. Enabled heap-backed FAT long-file-name support (`CONFIG_FATFS_LFN_HEAP=y`, `CONFIG_FATFS_MAX_LFN=255`) and retained 8.3 alias scanning for media copied by older firmware.
- Added close/remove/rename `errno` diagnostics and explicit error responses around `MEDIA_END`.
- Build verified successfully and the LFN-enabled image was flashed to COM11; the user then confirmed that uploading the `.rgb565` file completed successfully.

# 2026-08-10 Small animation upload test asset

- The media directory did not contain a small pre-converted animation; the available multi-frame asset is about 35 MB (79 frames).
- Created `../图片/PACON_Media/misaka_2frame_2fps.rgb565` by concatenating the two existing 475x466 RGB565 stills. It is 885400 bytes (2 frames at 2 FPS) and is intended for a quick BLE upload/playback smoke test.

# 2026-08-11 BLE media throughput and Android image conversion

- Added a dedicated binary media characteristic (`...05`) beside the existing command/response characteristics.
- Binary packets contain a four-byte little-endian offset and raw RGB565LE payload. The device writes packets sequentially and sends one cumulative `OK MEDIA_BIN offset/size` acknowledgement per eight packets; the old hexadecimal path remains available for compatibility.
- Updated the Android tool to use the binary window when the new characteristic is present, falling back to the legacy uploader otherwise. This removes hexadecimal expansion and the per-packet response round trip.
- The Android media picker now accepts ordinary Android-decoded PNG/JPEG/WebP images, center-crops/scales them to 475x466, converts to RGB565LE, and uploads the generated `.rgb565` frame automatically.
- During an upload the Android client requests high BLE connection priority and restores balanced priority afterwards; this is only an optional throughput hint and does not affect compatibility.
- ESP-IDF firmware build succeeded. Direct Android `javac` compilation against SDK Platform 37.0 succeeded; full Gradle verification still requires the locally unavailable Android Gradle plugin marker/network resolution.
- Hardware verification: the new firmware was flashed to COM11, and the user confirmed that the Android tool can upload media successfully over the binary BLE channel.

# 2026-08-11 Media management phase

- Added firmware commands MEDIA_LIST, MEDIA_INFO <index>, and MEDIA_DELETE
  <name>. Listing snapshots /sdnand/media without changing the renderer's
  active carousel array; deletion pauses the NAND reader, validates the RGB565
  filename, removes the file, and schedules the normal main-task rescan.
- Added Android controls for upload progress, cancel, retry, media listing,
  and long-press deletion. The existing binary upload path and legacy
  hexadecimal fallback are unchanged.
- Firmware ninja -C build app and direct Android SDK 37 javac checks both
  passed. The new firmware image is built but has not been flashed yet.

- Follow-up hardware validation: flashed the media-management image to COM11
  using esptool; SHA verification passed. Post-reset serial output showed
  SD NAND mounted with 6 media items, AXP2101 telemetry, OLED dimming, and
  normal home performance logs with no reset or panic.

# 2026-08-11 BLE command disconnect diagnosis

- Captured the user's `GET STATUS` failure on COM11. The firmware logged
  `***ERROR*** A stack overflow in task nimble_host has been detected` immediately
  after the response notification was initiated; this was a firmware task-stack
  failure, not an Android-side disconnect.
- Moved command/media buffers out of the NimBLE GATT callback and the BLE
  command parser into static storage. This removes the 244/280/512-byte local
  arrays from `nimble_host` while retaining the existing protocol and response
  behavior.
- Rebuilt and flashed the fix to COM11. Boot output is normal; `GET STATUS` and
  `MEDIA LIST` still require a phone-side regression check after this flash.

# 2026-08-11 Android MEDIA_LIST timeout diagnosis

- Used ADB UI inspection of the installed PACON BLE Tool while the firmware
  reported `BLE media: list count=6` and initiated its notification.
- The phone log showed `Media list failed: waiting for device response timeout`,
  followed by `<< OK MEDIA_LIST 6`; the response was delayed until after the
  five-second wait. This was Android BLE write-queue ordering, not a NAND or
  firmware list failure.
- Changed `sendCommandAndWait()` to use normal acknowledged GATT writes for
  control commands. The high-throughput binary media characteristic continues
  to use write-without-response. Direct Android SDK 37 `javac` verification
  passed; Gradle `assembleDebug` also passed and the updated APK was installed
  on the connected Android device through ADB.

# 2026-08-11 Android MEDIA_LIST stale-cancel fix

- A fresh ADB dump showed the decisive timing: after reconnect, `MEDIA_LIST`
  failed about one second after the service became ready, then `OK MEDIA_LIST
  6` arrived. The 15-second timeout was not actually being reached.
- Root cause was `disconnect()` setting `mediaCancelRequested=true` to stop an
  active upload, while `connectSelected()` left that flag set on the new
  connection. `sendCommandAndWait()` therefore skipped its wait immediately
  and reported a false timeout.
- The Android client now clears the flag when starting a fresh connection and
  only lets it interrupt waits while an upload is active. Gradle build and ADB
  installation passed; this is the next required `MEDIA LIST` regression test.

# 2026-08-11 Android MEDIA_LIST timeout follow-up

- The user still reproduced the failure after the acknowledged-write change.
  ADB logs continued to show `OK MEDIA_LIST 6` arriving after the five-second
  wait, while the firmware logged the matching `MEDIA_LIST` command and list
  count. This keeps the failure at the catalog-query timing boundary rather
  than a missing response or a disconnected device.
- Extended Android waits for `MEDIA_LIST` and each `MEDIA_INFO` query to 15 s;
  binary upload ACK timing is unchanged. Built with Gradle 9.3.1 / SDK 37 and
  installed the updated debug APK to the connected phone. The next regression
  is to reconnect and press `MEDIA LIST`; success should show six names and no
  timeout. If it still times out, the next probe is firmware-side timing of
  `ble_collect_media_entries()` and replacement with the cached media catalog.

# 2026-08-12 Android GATT command write serialization

- ADB logs showed that `MEDIA_LIST` and `MEDIA_INFO` indices 0/1 succeeded,
  but the next acknowledged command was issued before Android delivered the
  previous `onCharacteristicWrite` callback. `gatt.writeCharacteristic()`
  returned false, producing the misleading “BLE write failed” result.
- Added a serialized acknowledged-command write gate in the Android client;
  the next control command waits for the prior write callback. Binary media
  packets remain write-without-response and keep their throughput path.
- The gate is cleared on write failure, permission exceptions, and disconnects.
- Gradle `assembleDebug` passed and the APK was installed. The user confirmed
  that `MEDIA LIST` no longer fails after reconnecting, so the catalog query
  path is verified on the phone.

# 2026-08-12 Android media catalog metadata

- The Android media list now retains the complete `MEDIA_INFO` response and
  displays each item's filename, storage size, frame count, and playback FPS.
- Long-press deletion still uses the validated filename rather than the
  formatted display text.
- Gradle `assembleDebug` passed and the updated APK was installed for the
  next phone-side catalog/delete check.
- Phone-side verification passed: the user confirmed long-press delete and
  the subsequent media-list refresh work normally.

# 2026-08-12 Android upload catalog refresh

- After a successful upload, the Android client now waits briefly for the
  firmware's NAND/display-reader rescan and refreshes `MEDIA LIST`
  automatically, so the new file appears without a second manual query.
- Gradle `assembleDebug` passed and the APK was installed for validation.

# 2026-08-12 BLE media tap-to-play

- Added firmware command `MEDIA_PLAY <name>`. The command resolves the
  validated filename against the current NAND catalog, schedules the normal
  preloaded slide transition on the main task, and returns the selected index.
- The Android media list now sends `MEDIA_PLAY` on a normal tap while keeping
  long-press deletion unchanged.
- Firmware `ninja -C build app` and Android Gradle `assembleDebug` both passed;
  the APK was installed. The firmware image was flashed to COM11 and all
  bootloader, partition-table, and application hashes were verified.
- Hardware verification passed: tapping a media-list item on the Android tool
  switches PACON to the selected image/animation normally.

# 2026-08-12 Android current-media indicator

- The Android media catalog now queries `GET STATUS` before `MEDIA LIST` and
  marks the entry whose index matches the firmware's `media_index`.
- After a successful `MEDIA_PLAY`, the list updates the marker immediately so
  the selected item is visible without waiting for another query.
- The APK compiles successfully and was installed after USB debugging was
  authorized; phone-side verification passed: the current item shows
  `[PLAYING]`, and the marker moves immediately after selecting another item.

# 2026-08-12 Android reconnect catalog refresh

- After the response CCCD is enabled, the Android tool now automatically
  refreshes `GET STATUS`/`MEDIA LIST` after a short settle delay. Reconnecting
  therefore restores the media catalog and `[PLAYING]` marker without a manual
  button press.
- Gradle `assembleDebug` passed and the updated APK was installed over the
  authorized USB-debug connection. Phone-side reconnect verification passed:
  the media list and `[PLAYING]` marker appeared automatically after reconnect.

# 2026-08-12 Current development plan

The work is now organized into three phases:

1. **BLE media management closure** — verify reconnect auto-refresh, current
   playback marker, tap-to-play, long-press delete, upload cancellation/retry,
   and automatic catalog refresh after upload. Keep the existing binary media
   protocol unchanged.
2. **Android settings and transfer UX** — replace the temporary command-entry
   workflow with dedicated controls for brightness, Wi-Fi/BLE state, SkyOrb
   range/location, media list, playback, deletion, and upload/conversion
   progress. Validate each control against `GET STATUS`/`GET SETTINGS`.
3. **Final firmware cleanup and performance** — remove the transitional AP
   path only after BLE settings are verified, then optimize list queries and
   animation/render feedback without changing the already validated display,
   charging, or media file formats.

The first phase is complete. No new firmware protocol change is planned for
the media catalog; the next work starts with the Android settings UX.

# 2026-08-12 Android BLE tool visual refresh

- Reworked the Android control screen with a dark, rounded-card layout for
  connection, quick diagnostics, media library, progress, and activity log.
- Kept the existing BLE command serialization, upload protocol, media list,
  playback, delete, and current-marker behavior unchanged.
- Added clearer action hierarchy, larger touch targets, colored status/action
  buttons, and a scrollable page so the media catalog and log remain usable on
  smaller phones.
- Gradle `assembleDebug` passed and the refreshed APK was installed over USB
  debugging. Phone-side visual and BLE regression checks are next.

# 2026-08-12 Android media list contrast fix

- Replaced the platform default media-list row with an explicit dark row and
  high-contrast text, avoiding theme-dependent black text.
- The current `[PLAYING]` item is green; other media entries use light text.
- APK rebuild and phone-side visual check are pending.

# 2026-08-12 Android settings panel

- Added a collapsible device-settings panel to `PaconBleTool`.
- The panel reads and writes the existing BLE settings protocol for display
  brightness, SkyOrb range, manual latitude/longitude, automatic location, and
  saved Wi-Fi credentials; firmware media commands were not changed.
- `assembleDebug` passed and the APK was installed over the authorized USB
  debugging connection. Phone-side control verification remains pending.

# 2026-08-12 Wi-Fi settings reboot fix

- Root cause found in the device Settings touch handler: enabling Wi-Fi both
  created `skyorb_network_task` and synchronously called `skyorb_start_network`
  from the UI task. The network task also calls `skyorb_start_network`, so the
  Wi-Fi driver/netif/event setup could run concurrently and trigger a reset.
- Removed the synchronous second startup; Wi-Fi setup now runs once on the
  dedicated `skyorb_net` task. Added `[WIFI-DBG]` startup checkpoints and boot
  reset-reason logging for hardware verification.
- `my_Pacon/build` app target compiled successfully with ESP-IDF 5.4 toolchain;
  the corrected bootloader, partition table, and application were flashed to
  COM11. Runtime verification is pending the next Settings Wi-Fi toggle.

# 2026-08-13 Wi-Fi SoftAP memory guard

- Runtime log showed `RTC_SW_CPU_RST`/`LoadProhibited` inside the Wi-Fi
  SoftAP beacon path immediately after `wifi:alloc eb len=752 type=4 fail`.
  This is an internal-RAM allocation failure in the Wi-Fi driver, not a PMIC
  or brownout reset.
- Reduced Wi-Fi RX/TX/BA/management buffer counts and enabled PSRAM preference
  for Wi-Fi/LWIP allocations in `sdkconfig` and `sdkconfig.defaults`.
- Added `[WIFI-DBG]` internal-heap logging and a preflight guard before starting
  SoftAP. The duplicate UI-task startup fix remains in place.
- Rebuilt the complete app successfully with ESP-IDF 5.4.3. Runtime testing
  on COM11 is still required; do not treat the fix as confirmed until Wi-Fi
  can be enabled without a reset.

# 2026-08-13 Wi-Fi startup allocation failure (follow-up)

- The COM11 log from the flashed image shows that the Settings switch does
  reach the dedicated network task. `esp_wifi_init()` then fails with
  `ESP_ERR_NO_MEM`: the Wi-Fi driver reports `malloc buffer fail` and cannot
  allocate its expected RX buffers. This is why the switch appears to do
  nothing; the task exits after reporting the failure.
- The flashed image did not contain the latest runtime buffer override (there
  was no `[WIFI-DBG] init buffers` line), so the board was still running the
  previous 16/32-buffer profile. A separate `build_wifi_fix` directory was
  configured with ccache disabled and built successfully with ESP-IDF 5.4.3.
  The new application image is
  `build_wifi_fix/pacon_fluid_pendant.bin`; it has not been flashed yet.
- The 1 h 13 min wall time was dominated by repeated environment/configuration
  and full-build retries. A normal cached build/flash is much shorter; the
  clean 1,385-target build in `build_wifi_fix` completed in about 46 seconds
  after configuration, confirming that the two-minute flash step was not the
  whole session.

# 2026-08-13 Build-time cleanup and repository publish

- The host exposes 32 logical processors. The successful clean firmware build
  used `ninja -j16`; VS Code is now configured for `-j24`, leaving some CPU
  capacity for the IDE and operating system.
- The ESP-IDF extension now consistently uses `build_wifi_fix`, the clean and
  verified build tree with ccache disabled. A no-change build check takes about
  0.15 seconds; normal source-only changes should recompile and relink instead
  of rebuilding all 1,385 targets.
- Firmware and Android sources were pushed to
  `git@github.com:rmjskhy/Pacon.git` on branch `main` after the SSH key was
  installed. Generated build output remains ignored.

# 2026-08-13 Wi-Fi switch state synchronization

- Reproduction: Wi-Fi could be enabled once and SkyOrb continued animating,
  but reopening Settings showed Wi-Fi OFF. Further taps appeared to do
  nothing because the persistent `skyorb_net` task was already allocated.
- Root cause: the Settings switch was rendered from transient AP/STA event
  flags (`ap_ready || wifi_connected`) while task lifetime and driver state
  were tracked separately. A disconnect therefore changed the UI to OFF even
  though the user had not disabled Wi-Fi.
- Added an authoritative user-requested Wi-Fi state. Settings now remains ON
  during reconnects, OFF explicitly stops the driver, and ON reuses the
  existing network task. Saved credentials select STA mode; the provisioning
  AP remains a fallback only when credentials are absent.
- Incremental build with 24 jobs completed in about 11 seconds and the app-only
  COM11 flash in about 15.5 seconds. Runtime verification showed repeated STA
  reconnect attempts with no `ESP_ERR_NO_MEM` and no reset. The saved home
  network had not obtained an IP during the 45-second capture, so live ADS-B
  data was not yet confirmed; SkyOrb animation alone may be demo data.
- Hardware UI verification passed: after leaving Settings and entering it
  again, the Wi-Fi switch still displayed ON while the STA reconnect loop was
  active.
