# PACON 电子吧唧固件

这是 PACON 的正式应用工程；`test_Pacon` 仅用于外设和硬件专项测试。

当前正式固件包含图片/动画主页、下拉应用启动器、Fluid、0u0、SkyOrb、明亮金色机械表盘、RTC/蓝牙/Wi-Fi 校时、每日蜂鸣器闹钟、设备设置、BLE HID 手机拍照遥控、AXP2101 电池状态与充电控制，以及带页面开关的 MKDV4GCL-AB USB MSC 媒体管理。SkyOrb 最大挡位是半径 50 km。主要实现目前集中在 `main/fluid_pendant.c`。

## 设置断电记忆

设置在修改时写入 NVS，不需要先退出功能。新增的持久化逻辑需要烧录新版固件后生效。

| 功能 | 重启后保留的设置 |
| --- | --- |
| Fluid | 简约/方块/矩阵形状、自定义颜色（色相与饱和度） |
| OuO | 自动待机、倾斜反应、手动选择的心情值 |
| 蓝牙 | 蓝牙开关；配对记录仍由 NimBLE 保存 |
| Wi-Fi/雷达 | Wi-Fi 开关、保存的网络和所选网络、连接意图、范围、定位配置 |
| 显示/时钟 | 亮度、息屏时间、表盘样式、时间来源、闹钟开关和时间（原有保存逻辑） |

Wi-Fi 上次关闭则开机不启动；上次开启且选择过网络时，启动后尝试连接保存的网络，
不保证网络实际可用。蓝牙关闭后也保持关闭，需从 PACON 本机设置重新打开。
OuO 交互临时改变的心情不覆盖手动设置；临时表情、动画帧、触摸状态、USB U 盘占用状态
和当前功能页面不保存。主页仍从原入口启动，不自动恢复上次功能或素材播放进度。
遥控拍照使能是遗留调试参数，只在当前运行期间生效，不写入或读取持久化记录，
重启后默认启用；实际拍照仍需要蓝牙开启、手机已连接并允许 HID 快门。

新增记录带版本与范围校验，相同值不重复写入，保存失败会输出 `PACON_PREFS` 错误日志。
不会因为蓝牙初始化遇到 NVS 错误而自动清空所有设置。以前没有写入存储的选择无法追溯，
首次升级后请重新选择一次。普通固件烧录保留 NVS；整片擦除或工厂重置不保留。

## Fluid 隐藏导航

流体画面不再绘制左上返回和右上设置按钮，但保留原点击区域：

- 左上返回主页：`55 ≤ x < 215`，`68 ≤ y < 125`。
- 右上进入流体设置：`326 ≤ x < 448`，`24 ≤ y < 132`。OuO 的设置长按入口也使用同一区域。

坐标为屏幕原生 475×466 像素。亮屏时首次点击即触发，不需要先唤出按钮；
息屏后的首次触摸仍只负责唤醒屏幕。其它区域保留流体触摸扰动。

## 文档入口

- [DESIGN.md](DESIGN.md)：当前架构、硬件映射、界面、存储、电源和验证状态；
- [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)：按时间记录的开发与排障过程；
- [USB_MSC_NOTES.md](USB_MSC_NOTES.md)：U 盘模式、存储所有权和媒体格式；
- [POWER_KEY_AND_PMIC_DIAGNOSIS.md](POWER_KEY_AND_PMIC_DIAGNOSIS.md)：早期电源键和 PMIC 排查记录。

遇到结论冲突时，以当前源码、配置、原理图和目标板实测为准，其次参考 `DESIGN.md`。历史日志中的阶段性结论可能已经过时。

## 在 VS Code 中构建

1. 使用 VS Code ESP-IDF 插件打开 `D:\my_project\Pacon\my_Pacon`；
2. 选择 ESP-IDF v5.4.3 和 ESP32-S3；
3. 执行 Build，产物统一生成在本工程的 `build_wifi_fix` 文件夹；

为避免 ESP-IDF Python、Ninja、工具链版本和构建目录混用，推荐从 VS Code
的“终端 -> 运行任务”选择 `PACON: Build` / `PACON: Flash COM11`。这些任务
统一调用 `tools/pacon.ps1`，固定使用 ESP-IDF 5.4、`build_wifi_fix`、24 个并行
任务和 COM11。也可在 PowerShell 中运行：

```powershell
.\tools\pacon.ps1 check
.\tools\pacon.ps1 test
.\tools\pacon.ps1 build
.\tools\pacon.ps1 flash -Port COM11
```

旧的根目录构建产物和 `build_noccache` 仅作为历史文件保留，不再用于编译或
烧录；不要从多个构建目录混合选择 BIN 文件。
4. 板子进入下载模式后从 COM11 执行 Flash，再打开 115200 baud Monitor。

工程通过 `../Pacon/components` 复用项目内提供的 SH8601、LVGL 和 CMake utility 组件，请保持两个工程的相对目录关系。

## 手机拍照遥控

正式固件在现有 `PACON-BLE-TEST` 自定义服务旁注册标准 HID-over-GATT
Consumer Control 服务。第一次使用时，在手机系统蓝牙设置中配对
`PACON-BLE-TEST`，打开手机相机，然后从 PACON 的 Settings 页面点击
`Camera shutter`。固件发送一次 Volume Increment 按下/释放报告；Android/iOS
相机通常将音量键作为快门。也可通过 BLE 命令发送 `CAMERA SHUTTER`，用
`SET CAMERA REMOTE ON|OFF` 禁用或启用该功能。手机必须把 PACON 作为 HID
输入设备配对，单纯用自定义 GATT 工具连接只能验证命令返回，不能代替系统
相机接收 HID 报告。

若要从 Android 工具点击拍照按钮，手机需要同时保留两条连接：系统蓝牙设置
把 `PACON-BLE-TEST` 配对为 HID 输入设备，`PaconBleTool` 再连接 PACON 自定义
服务。正式固件已将 NimBLE 最大连接数设为 2，并按连接分别发送响应和 HID
通知。若只使用手表 Settings 页，则只保留系统 HID 连接即可。
