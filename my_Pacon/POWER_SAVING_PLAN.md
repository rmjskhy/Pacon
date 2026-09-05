# PACON AMOLED 安全与电池节能计划

目标硬件：DO0143FMST12 AMOLED、ESP32-S3、FT3168、QMI8658、AXP2101，
以及 402525 3.7 V / 4.2 V、250 mAh 锂电池。各阶段必须独立测试和板上验证。

## 风险基线

- 时钟固定表盘的局部老化风险最高；OuO 的亮白眼嘴集中在中央；Fluid
  固定残影风险最低，但彩色发光面积和持续刷新功耗最高。
- `SCREEN TIMEOUT = 0` 和 USB Disk 静态页面是主要长期常亮风险。
- 当前 `DISPOFF (0x28)` 只关闭画面，没有进入停止 DC/DC、振荡器和扫描的
  `SLPIN (0x10)` 状态。

## 阶段 1：AMOLED Sleep In/Out

1. 区分 Awake、Dimmed、Sleeping、Waking。
2. 休眠：亮度归零、`DISPOFF`、`SLPIN`，随后停止显示传输。
3. 首次触摸只发送 `SLPOUT`，不触发页面控件。
4. 非阻塞等待 120 ms，恢复运行寄存器，以零亮度打开显示，强制重绘当前页，
   最后恢复用户亮度。
5. Sleeping/Waking 期间 BLE 亮度命令只保存设置，不直接访问面板。
6. 本阶段不切断面板电源、不用 Deep Standby、不启用 ESP32 Light Sleep。

验收：连续睡眠/唤醒 20 次；所有页面正确重绘；无绿闪、旧帧、黑屏和误触；
BLE 在稳定等待期间不断开；休眠电流明显低于旧版。

## 阶段 2：静态页面保护

- USB Disk 与其他页面统一遵循 App 设置：到时先降亮度，再过 15 秒熄屏。
- 电池供电最长常亮 5 分钟。
- 时钟每 1～2 分钟整体偏移 1～3 像素；OuO 做幅度很小的全局漂移。

## 阶段 3：Wi-Fi 节能

- 将 `WIFI_PS_NONE` 改为实机验证过的 Modem Power Save。
- SkyOrb 获取数据后延时暂停 Wi-Fi，离开页面后不长期维持射频活动。
- 验证扫描、WPA、DHCP、HTTPS、重连以及 BLE/Wi-Fi 共存。

## 阶段 4：外设待机

- AMOLED Sleeping 时让 FT3168 进入 Monitor，验证共享 I2C 后再启用。
- 非倾斜页面降低 QMI8658 速率，熄屏时暂停加速度计。

## 阶段 5：ESP32 动态功耗

- 先启用 80～240 MHz 动态调频；动画和 QSPI 传输用电源管理锁请求高频。
- USB、BLE、Wi-Fi、I2C、QSPI 全部验证后，才评估 Tickless Idle/Light Sleep。

## 阶段 6：帧率和低电量策略

- OuO 无交互时 60 降至 30 FPS；Fluid 无触摸时目标约 20 FPS。
- 低于 20% 限制亮度、缩短休眠并减少 Wi-Fi；低于 10% 显示明确提示。

## 电池安全约束

- 标称能量约 0.925 Wh；当前 50 mA 充电约 0.2 C。
- 没有电芯厂商规格、保护板信息和电池 NTC 时，不提高充电电压或电流。
- 每阶段实测 Home、Clock、OuO、Fluid、Wi-Fi、Sleep 的整机电流，按可用容量
  除以平均电流估算续航，不用寄存器值代替物理测量。

## 当前进度（2026-09-06）

- [x] 阶段 1 代码实现：完整 Sleep In/Out 状态机、120 ms 非阻塞稳定期、
  首次唤醒触摸抑制、当前页面强制重绘、首帧后恢复亮度。
- [x] 阶段 1 源码回归检查和 ESP-IDF 5.4.3 完整编译。
- [x] 阶段 1 真机视觉验收：反复睡眠/唤醒无绿闪、旧帧、黑屏和误触；物理休眠
  电流仍待电流表测量，不能用 PMIC 电压或软件估算代替。
- [x] 阶段 2 代码实现：USB Disk 与其他页面统一按 App 设置的时间降亮度，再过
  15 秒熄屏；电池供电时即使用户关闭自动熄屏也强制最长常亮 5 分钟。时钟每
  90 秒整体漂移 2 像素，OuO 每 90 秒整体漂移 1 像素。USB 休眠后的首次触摸
  只唤醒，释放后再次点击才执行 OFF。
- [x] 阶段 2 专项与全量源码回归、ESP-IDF 5.4.3 完整编译。
- [x] 阶段 2 真机验收：USB 息屏/唤醒/OFF、时钟与 OuO 长时漂移、电池供电
  5 分钟上限均已通过；后续又在专用 MSC 模式验证 App 超时→降亮度→15 秒后
  熄屏，以及单次触摸可靠唤醒。
- [x] 阶段 3 代码实现：STA 改为 `WIFI_PS_MIN_MODEM`；SkyOrb 成功完成 HTTPS
  刷新后保留 5 秒收尾窗口再暂停射频，离开页面请求立即暂停；Wi-Fi 开关仅表示
  用户意图，自动暂停不改写 NVS。再次进入 SkyOrb、进入 Wi-Fi 设置、到达下一次
  雷达刷新或请求 SNTP 校时时按需恢复，并重新完成 WPA/DHCP。
- [x] 阶段 3 专项与全量源码回归、ESP-IDF 5.4.3 完整编译。
- [x] 阶段 3 核心链路真机验收：扫描、WPA2、DHCP、HTTPS ADS-B 刷新、
  `WIFI_PS_MIN_MODEM` 和刷新后 5 秒自动暂停均已在 COM11 通过；自动暂停后 Wi-Fi
  用户开关保持 ON。
- [x] 阶段 3 雷达页定时恢复真机验收：STA 暂停后保持 SkyOrb 页面，180 秒到期会
  重启 STA，重新完成 WPA2/DHCP/HTTPS，并在成功刷新后的 5 秒窗口结束时再次暂停。
- [x] 阶段 3 离页暂停真机验收：若没有在途工作则直接暂停；若 HTTPS 已开始，
  先安全完成 TLS/解析，完成后立即暂停而不再等待 5 秒收尾窗口。
- [x] 阶段 3 SNTP/BLE 共存真机验收：BLE 连接期间发起 Wi-Fi 校时，STA 从暂停
  状态恢复并在共存模式下完成 WPA2/DHCP、SNTP 写入 RTC 和 HTTPS，随后再次暂停；
  BLE 命令链路保持连接。
- [x] 阶段 3 功能性真机验收完成。
- [ ] 阶段 3 物理功耗验收：用电流表实测联网、MIN_MODEM、STA 暂停及重连阶段
  电流；不能用 PMIC 电压或 Wi-Fi 睡眠时间日志代替。
- [x] 阶段 4 代码实现：FT3168 仅在共享 I2C 身份与往返访问验证通过后，随 AMOLED
  Sleeping 写入 Monitor；进入前关闭 Watch 硬件手势，首次有效触摸恢复 Active 并只
  唤醒屏幕。QMI8658 在 Fluid、启用倾斜反应的 0u0 页面使用 250 Hz，其他亮屏页面
  使用 3 Hz low-power，熄屏时关闭加速度计，恢复后丢弃前三个样本。
- [x] 阶段 4 专项源码回归、ESP-IDF 5.4.3 完整编译和 COM11 烧录；真机日志已确认
  Home 为 3 Hz、熄屏后 QMI8658 paused、共享 I2C 验证通过且 FT3168 进入 Monitor。
- [x] 阶段 4 功能性真机验收：Home、Fluid、0u0（TILT 开/关）、Watch 和 USB Disk
  均验证 3 Hz/250 Hz/paused 状态切换；休眠后的首次触摸只唤醒且不误触。Watch 在
  Monitor 前关闭硬件手势，唤醒后重新启用且表盘不误切换。专用 TinyUSB MSC 模式
  因始终有 VBUS，休眠时保留 FT3168 Active 扫描以保证单次触摸可靠唤醒；第二次
  点击 OFF 后正常重启并恢复主界面与串口。
- [ ] 阶段 4 物理功耗验收：用电流表比较外设策略启用前后的 Home、倾斜页面和 Sleep
  整机电流；不能用 I2C 寄存器或串口状态代替。
- [x] 阶段 5 第一部分代码实现：启用 80～240 MHz DFS，保持 Tickless Idle 和
  Light Sleep 关闭；渲染、动画/QSPI、TLS 使用独立最大频率锁。WPA/DHCP 的异步
  关联窗口同样持锁，取得 IP 或失败后释放，避免 80 MHz 下认证超时。
- [x] 阶段 5 功能性真机验收：Home、Fluid、0u0、Watch、USB Disk、BLE、Wi-Fi
  扫描/WPA2/DHCP/HTTPS 与 BLE/Wi-Fi 共存均通过。SkyOrb 射频暂停后显示
  `RADIO PAUSED / CACHED DATA`，BLE `GET STATUS` 响应不会把缓存页误显示为断网。
- [x] 阶段 5 USB 显示策略修正：移除专用模式固定 25 秒覆盖；App 设置时间到后
  降亮度，再过 15 秒进入 AMOLED Sleep，专用模式保持 FT3168 Active。COM11 用
  15 秒设置验证 15 秒变暗、30 秒熄屏、单次完整触摸立即唤醒。
- [ ] 阶段 5 物理功耗验收与 Tickless Idle/Light Sleep 评估：先用电流表比较
  80/240 MHz、网络活动/暂停和各页面整机电流，再决定是否继续启用系统睡眠。
- [ ] 阶段 6 尚未实施。
