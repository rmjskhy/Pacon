# PACON 电子吧唧固件

这是 PACON 的正式应用工程；`test_Pacon` 仅用于外设和硬件专项测试。

当前正式固件包含图片/动画主页、下拉应用启动器、Fluid、0u0、SkyOrb、明亮金色机械表盘、RTC/蓝牙/Wi-Fi 校时、每日蜂鸣器闹钟、设备设置、BLE HID 手机拍照遥控、AXP2101 电池状态与充电控制，以及带页面开关的 MKDV4GCL-AB USB MSC 媒体管理。SkyOrb 最大挡位是半径 50 km。主要实现目前集中在 `main/fluid_pendant.c`。

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
