# PACON 电子吧唧固件

这是 PACON 的正式应用工程；`test_Pacon` 仅用于外设和硬件专项测试。

当前正式固件包含图片/动画主页、下拉应用启动器、Fluid、0u0、SkyOrb、设备设置、AXP2101 电池状态与充电控制，以及 MKDV4GCL-AB 的 USB MSC 媒体管理。主要实现目前集中在 `main/fluid_pendant.c`。

## 文档入口

- [DESIGN.md](DESIGN.md)：当前架构、硬件映射、界面、存储、电源和验证状态；
- [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md)：按时间记录的开发与排障过程；
- [USB_MSC_NOTES.md](USB_MSC_NOTES.md)：U 盘模式、存储所有权和媒体格式；
- [POWER_KEY_AND_PMIC_DIAGNOSIS.md](POWER_KEY_AND_PMIC_DIAGNOSIS.md)：早期电源键和 PMIC 排查记录。

遇到结论冲突时，以当前源码、配置、原理图和目标板实测为准，其次参考 `DESIGN.md`。历史日志中的阶段性结论可能已经过时。

## 在 VS Code 中构建

1. 使用 VS Code ESP-IDF 插件打开 `D:\my_project\Pacon\my_Pacon`；
2. 选择 ESP-IDF v5.4.3 和 ESP32-S3；
3. 执行 Build，产物生成在本工程的 `build` 文件夹；
4. 板子进入下载模式后从 COM11 执行 Flash，再打开 115200 baud Monitor。

工程通过 `../Pacon/components` 复用项目内提供的 SH8601、LVGL 和 CMake utility 组件，请保持两个工程的相对目录关系。
