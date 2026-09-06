# PACON 开发交接记录

## 2026-09-06：电池节能阶段 5 Tickless Idle（功能真机完成）

- 在已通过硬件测试的 80～240 MHz DFS 基线上单独启用 Tickless Idle，Automatic Light
  Sleep 保持关闭。首次固件在启动 Wi-Fi 驱动时稳定复现 `ESP_ERR_NO_MEM`：首轮只能分配
  1/4 个 1600 B 静态 RX 缓冲，重试降为 0/4；关闭 Tickless 后同一固件、同一启动顺序
  恢复完整 4/4 缓冲和 STA 扫描，排除了热点与凭据原因。
- 链接图单变量对照确认 Tickless 使静态 D/IRAM 从 188547 B 增至 195587 B（+7040 B）。
  启用 ESP-IDF 的 `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH`，只把允许的非 ISR
  FreeRTOS 函数放回 Flash，静态 D/IRAM 降至 185951 B；相对失败版释放 9636 B，未削减
  Wi-Fi 缓冲、BLE 或外设功能。
- COM11 冷启动检查通过：Tickless 配置标记正确，Wi-Fi 启动前内部堆为 51835 B，BLE、
  FT3168、QMI8658、NAND 媒体加载后 Wi-Fi 驱动仍取得 4 个静态 RX 缓冲并进入 STA 扫描。
  新增 `tools/check_tickless_wifi_boot.py`，可分别验证 Tickless enabled/disabled 固件，并在
  `ESP_ERR_NO_MEM` 或驱动初始化失败时立即报错。
- Tickless 功能回归期间发现 Fluid 平放时长期向屏幕底部聚集、向上翘约 30 度才反向。
  根因是 IMU 平面重力落入死区后人为写入 `gravity_y = 92`，使真实反向重力必须先抵消
  已积累的向下速度。现将死区改为 X/Y 同时归零，保留原有速度阻尼；平放自然停稳，
  轻微倾斜即跟随真实方向。新增中性死区回归，COM11 四向倾斜真机验证通过。
- Watch 的 FT3168 硬件手势 ID 会保持上次的左/右方向；旧实现只在 ID 改变时切换，导致
  连续同方向滑动被吞。现让硬件手势与 100 Hz 坐标位移回退共用“一次接触只切一次”锁，
  既避免同一次滑动重复切换，也允许锁存 ID 时由坐标路径完成操作。左右各连续两次切换
  均在 COM11 真机通过。
- 最终真机回归覆盖 Home、Fluid、0u0、Watch、BLE `GET STATUS`、SkyOrb 首次连接、
  `RADIO PAUSED / CACHED DATA`、3 分钟周期重连，以及 USB Disk 15 秒变暗、30 秒熄屏、
  单次触摸唤醒和 OFF 返回主界面，均正常。完整源代码回归与 ESP-IDF 构建通过；阶段 5
  尚缺电流表物理功耗数据，不能以功能日志替代。

## 2026-09-06：电池节能阶段 5 动态调频（功能真机验收完成）

- ESP32-S3 启用 80～240 MHz 动态调频；Tickless Idle 与 Light Sleep 继续关闭，避免在
  USB、BLE、Wi-Fi、共享 I2C 和 QSPI 功能闭环前一次引入多个变量。UI 渲染、动画/QSPI
  传输和 TLS 分别使用最大频率锁，完成后立即释放。
- 首版只覆盖同步 TLS 工作，真机出现可见热点但 WPA 四次握手超时。最小 LCY 复现确认
  STA 从认证进入关联后约 10 秒以 reason 204 退出；将网络锁扩展到异步 WPA/DHCP 窗口，
  在连接请求前持 240 MHz，取得 IP 或连接取消/失败后释放。修复后 LCY 首次完成 WPA2，
  DHCP 取得 `192.168.137.114`，随后自动定位及 HTTPS 均成功。
- SkyOrb 在 HTTPS 成功后的射频暂停期改为继续展示缓存飞机，并明确标注
  `RADIO PAUSED / CACHED DATA`，不再把省电暂停渲染成 `WIFI ON / NOT CONNECTED`。
  BLE 保持连接时发送 `GET STATUS` 已收到响应，页面与缓存状态保持不变。
- 完成 Home、Fluid、0u0、Watch、USB Disk、BLE、Wi-Fi 扫描/WPA2/DHCP/HTTPS、
  SkyOrb 暂停缓存和 BLE/Wi-Fi 共存真机复测，未发现新的功能回归。Wi-Fi 闭源驱动在
  TLS 压力下仍偶发打印非致命 `wifi:mem fail`，但请求与后续暂停成功，继续作为内存
  压力观测点保留。
- USB Disk 专用模式原先硬编码 25 秒睡眠，覆盖了已持久化的 `SET SCREEN TIMEOUT 120`；
  同时 FT3168 Monitor 的低频扫描会漏掉短触。现统一为 App 设置时间到后降亮度、15 秒
  后完全息屏，并在始终有 VBUS 的专用 MSC 模式保持 FT3168 Active。COM11 用 15 秒设置
  验证：15 秒变暗、30 秒息屏、一次完整点按立即唤醒，USB 磁盘与 OFF 退出路径保持正常。
- 新增/更新 DFS、SkyOrb 暂停页、USB 休眠和静态显示保护回归；完整 `pacon.ps1 test`、
  ESP-IDF 5.4.3 构建与 COM11 烧录通过。最终应用大小 `0x52ee30`，最小应用分区余量
  65%。阶段 5 目前只完成代码与功能真机验收；物理电流测量及 Tickless Idle/Light Sleep
  是否值得启用仍待决定，不能用频率日志或 PMIC 电压代替电流表结果。

## 2026-09-05：电池节能阶段 4 外设待机（功能真机验收完成）

- FT3168 在 AMOLED Sleeping 时进入 Monitor。为规避 FT3168 与 AXP2101、QMI8658、
  PCF85063 共用 I2C0 的已知限制，固件每次上电只做一次 Active 状态下的触摸→PMIC→
  IMU→RTC→触摸往返验证；任一身份或事务失败，本次启动保持触摸 Active 作为安全回退。
- Watch 页使用的 FT3168 硬件手势在进入 Monitor 前关闭，唤醒后再按页面恢复。Monitor
  写事务 ACK 作为进入成功的边界，不立即读取 `0xA5`：实机表明紧随写入的读取可能仍
  返回旧值，而其他 I2C 从设备通信后 FT3168 会按 Monitor 行为暂不应答，直到触摸自动
  回到 Active。失败重试均限速，避免在共享总线上刷警告。
- QMI8658 采用页面驱动状态机：Fluid 和启用倾斜反应的 0u0 使用 ±8g/250 Hz，其他
  亮屏页面使用 ±8g/3 Hz low-power，AMOLED Sleeping 时通过 CTRL7 暂停加速度计；
  从暂停或低速恢复后丢弃前三个样本，避免陈旧 FIFO/稳定期数据驱动界面。
- 新增 `tests/check_peripheral_power_saving.ps1`，覆盖共享 I2C 验证、安全回退、Watch
  手势互锁、首次触摸唤醒、QMI8658 状态策略和主循环调用顺序。专项检查和 ESP-IDF
  5.4.3 完整构建通过，应用大小 `0x52ce10`，最小应用分区余量 65%；最终候选已烧录
  COM11，三个镜像均通过摘要校验。
- 真机启动与休眠日志确认 Home 使用 3 Hz low-power，熄屏后 QMI8658 paused，共享
  I2C 验证通过且 FT3168 Monitor 写入成功；持续 PMIC/RTC 总线活动下，长时间休眠后
  首次触摸仍只唤醒并完成 Active、SLPOUT、重绘和亮度恢复，没有误触页面控件。
- Fluid 进入后 30 ms 内切到 250 Hz，返回 Home 后 40 ms 内降回 3 Hz，休眠时暂停。
  0u0 的 TILT 开启时表情页为 250 Hz；进入设置页或关闭 TILT 后为 3 Hz，返回表情页
  仍保持低速；重新开启并返回后恢复 250 Hz，原有开启设置已还原。
- Watch 实测在亮屏时启用 FT3168 硬件手势，休眠前先关闭手势再进入 Monitor，唤醒后
  重新启用；首次唤醒没有误切 KKD1/KKD2。一次触摸落在休眠边界时，状态机也能恢复
  Active 后重新进入 Monitor，下一次触摸仍正常唤醒。
- USB Disk 控制页的串口日志确认 25 秒后进入 QMI paused 与 FT Monitor，首次触摸
  只唤醒并恢复 3 Hz/重绘，不执行页面操作。随后在专用 TinyUSB MSC 模式完成可见
  行为验收：25 秒后熄屏，首次触摸只亮屏，释放后第二次点击底部 OFF 才退出；设备
  正常重启回主界面并恢复 Serial/JTAG。MSC 占用 USB PHY 期间没有串口，因此不把
  该段可见行为误记为串口寄存器证据。阶段 4 功能性真机验收至此完成，仅剩用电流表
  比较 Home、倾斜页面和 Sleep 的物理功耗；日志和 PMIC 电压不能替代该测量。

## 2026-09-05：电池节能阶段 3 Wi-Fi 省电（功能真机验收完成）

- WPA 认证和 DHCP 期间保持 `WIFI_PS_NONE`；取得 IP 后切换到
  `WIFI_PS_MIN_MODEM`，允许射频在 DTIM 间隔休眠；暂不启用延迟更高的
  `WIFI_PS_MAX_MODEM`。
- 分离用户 Wi-Fi 开关与瞬时射频状态。SkyOrb 成功完成 ADS-B HTTPS 刷新后等待
  5 秒再由网络任务安全停止 STA；离开 SkyOrb 也请求暂停，自动暂停不改写 NVS。
- 再次进入 SkyOrb、进入 Wi-Fi 设置、缓存数据到达 180 秒刷新周期或 BLE 请求
  Wi-Fi/SNTP 校时时恢复 STA；若暂停前已联网，则重新执行保存网络的 WPA 与 DHCP。
- 新增 `tests/check_wifi_power_saving.ps1`；Wi-Fi/SkyOrb/TLS/持久化专项、统一全量
  回归和 ESP-IDF 5.4.3 完整构建通过。固件大小 `0x52ae80`，最小应用分区余量 65%。
  已烧录到 COM11；仍需验证 DHCP、HTTPS、自动暂停/恢复、SNTP、BLE/Wi-Fi
  共存，并用电流表比较联网与暂停电流。
- 真机扫描能稳定看到 `test` 与 `lcy`，但两者均在 802.11 开放系统认证阶段以
  `WIFI_REASON_AUTH_EXPIRE` 失败。关闭 modem power save、关闭 BLE、解除 BSSID
  固定、扩大管理缓冲区以及热点改为 WPA2-only 均不改变结果。
- 独立 `test_Pacon/wifi_ap_tx_test` 固件成功启动非隐藏 SoftAP，但手机与电脑均
  搜不到其信标；10 dBm/20 dBm、信道 1/6、partial/full PHY 校准结果相同。
- 随后从干净提交 `a1f3b87` 隔离构建并烧录原始固件；该版本使用原始网络任务、
  原始缓冲区和全程 `WIFI_PS_NONE`，连接当前 `test` 热点仍在同一认证阶段以
  reason 2 失败。因此可排除阶段 3 代码回归，但尚不能仅凭 SoftAP 测试断言硬件
  损坏；当前故障边界是旧/新固件共同的 802.11 TX/认证现场状态。上述试探性
  生产代码改动已撤回，需用另一台热点或另一块板继续交叉验证。
- 后续交叉测试确认电脑热点 `LCY`（信道 11）以及手机热点切换到信道 11 后，历史
  固件和阶段 3 固件都能完成 WPA/DHCP；同一手机热点重新落到信道 6 时两版又都失败。
  因此此前“完全无法连接”并非阶段 3 回归，ESP32-S3 的 STA 收发链路也并未损坏。
- 阶段 3 固件连上 `LCY` 并取得 `192.168.137.114` 后，TLS 证书验证成功但曾出现
  `esp-aes: Failed to allocate memory`。当时内部堆仅余约 8 KiB，而硬件 AES 会为
  PSRAM 中的 TLS 记录申请同尺寸的内部 DMA 对齐跳板；这与双 QSPI 显示 DMA 条带争用。
  保留已经实测的显示流水线，改为 240 MHz 下无速度优势损失的软件 AES，并在
  `check_tls_memory.ps1` 固定该内存约束。
- 修复后在 COM11 真机重新扫描到 `LCY`，完成 WPA2、DHCP，启用
  `WIFI_PS_MIN_MODEM`，HTTPS 成功刷新 9 架飞机。日志于 21691 ms 安排 5 秒收尾窗口，
  于 26691 ms 停止 STA，并确认 Wi-Fi 用户开关保持 ON；不再出现 AES 内存错误。
  配置回归和 ESP-IDF 5.4.3 完整构建通过，应用大小 `0x52c560`，分区余量 65%。
  仍需继续验证 180 秒定时恢复、离页暂停、SNTP、BLE 共存和物理电流。
- 保持 SkyOrb 页面完成 180 秒周期验收：700771 ms 缓存到期后 STA 自动恢复，
  704501 ms 重新取得 DHCP 地址并启用 `WIFI_PS_MIN_MODEM`，716011 ms HTTPS
  刷新 8 架飞机，721071 ms 再次暂停且用户开关保持 ON；全程无 AES 内存错误。
  阶段 3 尚余离页暂停、SNTP、BLE 共存和物理电流验收。
- 离页暂停实测通过：821631 ms 再次进入 SkyOrb 并恢复 STA，827001 ms 返回 Home
  时定位/HTTPS 已在执行；网络任务先安全收尾，834411 ms ADS-B 刷新结束后约
  240 ms 即于 834651 ms 停止 STA，没有再等待普通 5 秒窗口，也没有自动重连。
  这验证了离页请求不会破坏在途 TLS，同时不会让射频长期留在后台。
- BLE/SNTP 共存实测通过：BLE 保持连接时于 1107121 ms 收到 `SYNC WIFI TIME`，
  STA 以 `coexist: 1` 恢复，重新完成 WPA2/DHCP；1108711 ms 启动 SNTP，
  1111651 ms 将网络时间写入 PCF85063。随后同一连接完成 HTTPS 并刷新 14 架飞机，
  1122001 ms 再次暂停 STA，BLE 命令链路未断开。
- 该轮连接期间 Wi-Fi 闭源驱动打印过一次非致命 `wifi:mem fail`，但 WPA/DHCP、
  SNTP、RTC 写入、HTTPS 与后续暂停均成功。保留此警告作为后续内存压力观测点，
  不将它描述为功能失败或假装完全无警告。
- 至此阶段 3 的扫描、WPA2、DHCP、MIN_MODEM、HTTPS、5 秒自动暂停、180 秒恢复、
  离页暂停、SNTP 唤醒和 BLE/Wi-Fi 共存均通过真机验收；只剩物理电流表测量，
  不能用日志中的 Wi-Fi sleep time 或 PMIC 电压代替。

最后更新：2026-09-05

## 2026-08-29：Fluid 设置与调色盘触摸反馈优化

- 用实际 C 渲染循环的机械转换回放建立重复工作基线：切换一个 Fluid 模式或移动一次
  选色标记，旧路径都会重新计算并传输完整的 `475×466 = 221350` 像素。
- Fluid 设置页改为复用原生 RGB565 画布和双 DMA 条带；切换模式只重绘新旧两行，
  最多计算/传输 57000 像素。调色盘缓存静态 301×301 色环，暖更新只恢复旧标记并
  绘制新标记，最多计算 169 像素、传输 12350 像素；PSRAM 缓存申请失败时仍保持
  相同画面并回退为局部实时计算。
- 调色盘支持按住连续拖动；一次拖动只在松手后合并保存一次，画面反馈不再等待 NVS
  提交。保存失败不会丢弃待保存状态，而是每秒重试；页面切换、屏幕唤醒和 DMA 失败
  均强制下一帧完整重绘，避免共享画布残影。
- 新增 `[FLUID-UI-PERF]` 实机日志，分别报告绘制、屏幕传输、输入到刷新完成、NVS 保存
  的平均/最大耗时。`check_fluid_controls_performance.ps1` 做完整画面差分、拖动/保存、
  缓存失败和 DMA/NVS 失败回归；全部源检查和 ESP-IDF 固件编译通过，本轮尚未烧录。

## 2026-08-29：功能设置入口统一到右上角

- PACON 当前有本机独立设置入口的 Fluid 与 OuO 统一使用右上角
  `x=326..447, y=24..131` 触摸区域；Fluid 保持先前要求的隐藏图标和单击进入，OuO
  保留 560 ms 静止长按防误触，但不再从左下角唤出菜单。
- OuO 预览器同步新的右上角触发区，删除固件与预览器的旧左下角判断；新增
  `check_feature_settings_positions.ps1`，固定检查两项功能边界一致。

## 2026-08-28：补齐功能设置断电记忆（已烧录，待真机设置恢复复测）

- 新增 `main/pacon_preferences.c/.h`，以 `pacon_prefs` 命名空间保存版本化小记录；Fluid 形状/颜色和 OuO 手动选项各为一条记录，校验长度、版本与范围，相同值跳过写入，写入/提交错误记录到日志。
- 在实际触摸修改点保存，启动时于首次绘制前恢复；手势产生的临时心情不覆盖 OuO 手动心情值。
- 保存蓝牙和 Wi-Fi 开关；Wi-Fi 恢复放在显示、BLE、媒体内存初始化后，并复用已有扫描后连接的序列。删除选中网络时清除连接意图。按用户确认，遥控拍照使能只作为当前运行期间的调试开关，重启默认启用，不持久化；OuO 隐藏菜单选项仍保留记忆。
- 保留原有显示、时钟、闹钟和雷达配置存储；移除 BLE 初始化异常时整片清空 NVS 的行为。USB 存储所有权、当前页面、动画帧和临时表情不持久化。
- `check_feature_persistence.ps1` 检查真实调用点和启动顺序，并防止遥控拍照调试开关重新进入持久化路径；对实际 C 存储/编码函数做机械语法转换并模拟 NVS，覆盖恢复、坏数据、存储失败。该检查不是物理断电测试。
- 范围调整后 92 个存储测试场景及完整回归通过，固件编译成功（`0x520260` 字节）。已按用户要求烧录到 COM11，全部写入哈希校验通过并重启；仍需真机修改设置、重启、读回验证，不能把烧录校验当作设置恢复实测。App 无需重装。

## 2026-08-22：KKD2 独立手臂图层顺序修复

- 对照原 APK 的 `kkd2_watchface.xml`，确认完整人物/小时层先绘制，独立手臂/分钟层最后绘制在前景。
- 之前反转图层顺序会遮住手臂与人物的连接部分，只剩一段看似偏移的手臂；现恢复为背景、时间盘、完整人物小时层、独立分钟手臂的原始顺序。
- `tests/check_kkd2_reference_pose.ps1` 同时检查 APK XML 与固件绘制顺序，防止后续再次反转。

## 2026-08-22：KKD2 参考姿态与确定性构建入口

- 对照 `com.KKD2.apk` 解包后的 `kkd2_watchface.xml` 确认：完整人物是整数小时层（`HOUR_0_23 * 30`），独立胳膊是分钟层（`MINUTE * 6`）。因此 16:02 时人物应为 120°、胳膊应为 12°，照片中接近 12 点并略向右偏的位置符合原 APK。
- 删除固件中 KKD2 人物角度额外加入的分钟插值；新增 `tests/check_kkd2_reference_pose.ps1`，固定校验 XML 公式、资源原点、图层顺序和 16:02 参考姿态。
- 新增 `tools/pacon.ps1` 和 VS Code PACON 任务，统一 ESP-IDF/Python/Ninja、`build_wifi_fix`、COM11 与并行度；旧 `build`、根目录构建和 `build_noccache` 不再作为烧录来源，避免误烧旧 BIN 和重复排查环境。

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
- 2026-08-22 因正式固件闹钟仍无声，新增 `test_Pacon/buzzer_test` 独立诊断固件：只初始化 GPIO48 的 LEDC，以 2 kHz、50% 占空比循环输出 1 秒并静音 1 秒，不启动屏幕、BLE、RTC 或其他外设。另移除表盘普通触摸对 `s_watch_style` 的异或切换，避免 Android 选择 KKD2 后又被触摸切回 KKD1。

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
- Added line-oriented commands: `GET STATUS`, `GET SETTINGS`, `GET HELP`, `SET BRIGHTNESS 0..100`, `SET RANGE 0..5`, `SET LOCATION lat lon`, `SET AUTO_LOCATION`, and `SET WIFI ssid|password`. The six range indices map to 5/10/15/25/35/50 km.
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
- Follow-up fixed the inverse path: entering Sky Radar no longer forces a
  user-disabled Wi-Fi switch back ON, and duplicate FT3168 touch edges within
  700 ms are ignored. Hardware verification confirmed Wi-Fi remained OFF
  after entering and leaving the radar app.
- Sky Radar continues its local sweep and demo-aircraft animation while Wi-Fi
  is disabled. The radar status now explicitly reads `OFFLINE / DEMO DATA`;
  `CONNECTING` is reserved for an enabled Wi-Fi connection that has not yet
  obtained network connectivity.
- Wi-Fi re-enable testing showed the switch and driver were ON, but the STA
  repeatedly transitioned `init -> auth -> init` without obtaining an IP.
  SkyOrb now records the ESP-IDF disconnect reason and distinguishes
  `AUTH FAILED / CHECK PASSWORD`, `AP NOT FOUND`, and generic `CONNECTING`
  rather than presenting every non-connected state as offline.

# 2026-08-13 Phone-style Wi-Fi settings and cold-start radar fix

- A cold boot with Wi-Fi left OFF could reset immediately after entering Sky
  Radar. `skyorb_snapshot_aircraft()` unconditionally took
  `s_skyorb_mutex`, but that mutex was only created by the network startup
  path. The snapshot now has a safe no-mutex path before networking has ever
  been initialized.
- Wi-Fi enablement, scanning, selecting a saved network, connecting, and the
  obtained-IP state are now separate states. Turning the master switch ON
  starts STA mode and scans; it no longer silently connects or starts the old
  provisioning SoftAP. The saved credential row reports visibility/RSSI and
  must be tapped to connect.
- Added a dedicated scroll-free Wi-Fi detail page reached from Settings. It
  contains a master switch, the currently saved network, `SCANNING`,
  `IN RANGE`, animated `CONNECTING`, `CONNECTED`, `AUTH FAILED`, and
  `NOT IN RANGE` states, plus an explicit rescan control. The current NVS
  schema stores one saved network, so this first version displays one row.
- Sky Radar now consumes the same authoritative state: `OFFLINE / DEMO DATA`
  when Wi-Fi is disabled, `WI-FI ON / NOT CONNECTED` before a network is
  selected, specific connection errors while joining, and online/live states
  after an IP is obtained. The large central automatic-location/network-IP
  prompt was removed; the location hint is kept unobtrusively near the bottom.
- Full ESP-IDF build succeeded. Application size is `0x15ed20` bytes with 91%
  of the smallest app partition still free. Hardware verification on COM11 is
  required for touch layout, scan results, joining, and the cold-start radar
  path.
- Hardware connection trace showed the saved AP at `-61 dBm`, followed by
  repeated `init -> auth -> init` transitions and disconnect reasons 2
  (`WIFI_REASON_AUTH_EXPIRE`) and 205 (`WIFI_REASON_CONNECTION_FAIL`). The
  station never associated and DHCP was never reached. Both reasons are now
  terminal authentication failures instead of being retried until the generic
  15-second timeout. The Wi-Fi page reports `AUTH FAILED / CHECK PASSWORD`;
  the saved password or router security mode must be corrected over BLE.

# 2026-08-14 Router-specific Wi-Fi authentication diagnosis

- PACON still reported `AUTH FAILED` for the saved `lcy` network after Wi-Fi
  power saving was disabled, but connected successfully to a phone 2.4 GHz
  hotspot. This verifies the board's STA transmit/receive path, DHCP path,
  generic WPA credential handling, and antenna well enough to move the fault
  boundary to the `lcy` router or its advertised security/association setup.
- Increasing the previously minimal Wi-Fi buffers and disabling power saving
  did not change the `lcy` failure. The earlier reasons 2/205 occur before an
  IP address is obtained and the UI's `AUTH FAILED` label is therefore a broad
  association category, not proof that the stored password is wrong.
- Added `[WIFI-SCAN]` diagnostics for the selected BSSID, channel, auth mode,
  pairwise/group cipher, RSSI, and advertised PHY modes. A connection now pins
  the exact BSSID/channel from the saved-network scan row, preventing a
  dual-band/mesh router with several same-name BSSIDs from silently selecting
  a different candidate. The next hardware trace must compare `lcy` with the
  known-good phone hotspot before changing further connection parameters.
- Hardware trace for `lcy`: BSSID `E0:40:07:67:E0:94`, channel 1,
  `auth=3` (WPA2-PSK), pairwise/group cipher 4 (CCMP/AES), RSSI -57 dBm,
  and 802.11b/g/n support. The pinned BSSID still transitions
  `init -> auth -> init` after one second with reason 2, never reaching
  association or the WPA four-way handshake. This rules out WPA3/mixed mode,
  weak signal, and selection of another same-name BSSID. Since the same
  firmware connects to a phone hotspot, the next one-variable hardware test
  is to disable BLE before joining `lcy`; if it still fails, inspect router
  MAC filtering/access control for station MAC `10:20:BA:78:4F:A4`.
- Root cause confirmed on the router: its 2.4 GHz radio failed PACON
  authentication while bandwidth was set to automatic `20/40 MHz`, but PACON
  connected successfully after the router bandwidth was forced to `40 MHz`.
  The saved password, WPA2-PSK/CCMP security, BLE coexistence, RSSI, and board
  RF hardware were not the cause. For this router, retain fixed 40 MHz unless
  later firmware/router updates are re-tested. Disconnect reason 2 should be
  presented as a generic router authentication/compatibility failure rather
  than always instructing the user to check the password.

# 2026-08-14 Sky Radar `DATA RETRY` state fix

- After Wi-Fi connectivity was restored, Sky Radar displayed `DATA RETRY`.
  Inspection found that an automatic IP-location failure incorrectly set the
  aircraft-data failure flag. The task then called the aircraft API every five
  seconds even when no valid latitude/longitude existed, and the UI checked
  `fetch_failed` before `location_valid`.
- Sky Radar now skips aircraft requests until a valid location exists and
  displays `ONLINE / SET LOCATION` for the missing-location state. Only an
  actual aircraft HTTP/parsing failure can display `DATA RETRY`; non-200
  responses now log the HTTP status and a short response preview.
- The Airplanes.live periodic request interval was changed from 5 seconds to
  180 seconds. This caps uninterrupted periodic polling at 480 requests/day,
  within the documented approximate 500-request free allowance; the first
  fetch after connection remains immediate.

# 2026-08-15 Multi-network Wi-Fi profiles and Android settings split

- Replaced the single saved Wi-Fi credential with a five-profile NVS model.
  Existing `ssid/password` data migrates to profile 0 and the selected profile
  remains mirrored to those legacy keys for rollback compatibility.
- The board Wi-Fi page now lists only saved networks. A tap selects/connects;
  a 700 ms long press exposes a per-row delete control. Background scanning
  annotates saved profiles with visibility, RSSI, BSSID, and channel without
  displaying unknown access points.
- Extended the BLE settings protocol with `GET WIFI LIST`, `GET WIFI <index>`,
  `SELECT WIFI <index>`, and `DELETE WIFI <index>`. `SET WIFI` now upserts by
  SSID and also supports open networks with an empty password.
- Split the Android tool into Device/Display, Wi-Fi Management, and Radar
  Settings panels. The Wi-Fi list refreshes over BLE, taps connect, long
  presses confirm deletion, and radar save/automatic-location operations no
  longer alter Wi-Fi credentials.
- Verification: ESP-IDF build `build_wifi_fix` succeeded with 24 Ninja jobs;
  application size is `0x160170` bytes with 91% of the smallest app partition
  free. Android `assembleDebug` also succeeded. Hardware interaction and BLE
  list migration still require target-board verification.

# 2026-08-15 Wi-Fi switch sequencing, full-face radar, and BLE client state

- Fixed the first profile-switch timeout by routing both the board Wi-Fi list
  and BLE `SELECT WIFI` through one sequencer. It disables retry of the old
  profile, disconnects, waits briefly, scans the selected saved SSID, and only
  then starts association. `tests/check_wifi_switch_sequence.ps1` guards this
  ordering and passes after failing against the old direct-connect path.
- Sky Radar now uses a 230-pixel-radius full-face canvas. Visible Back/Settings
  controls were removed; the top-left 104x104 region is an invisible exit
  target. A circular drag around the face changes range one step on release:
  clockwise increases and counter-clockwise decreases, with a 0.40-radian
  threshold to reject incidental touches.
- The Android client now shows explicit discovered/connecting/service-ready/
  disconnected state. Connect is disabled while connecting or connected and
  Disconnect is disabled while idle. The whole page is scrollable, uses
  `adjustResize`, and scrolls focused Wi-Fi credential fields above the soft
  keyboard.
- Verification: Wi-Fi sequencing regression check passed; ESP-IDF
  `build_wifi_fix` produced a `0x160420`-byte application with 91% of the
  smallest app partition free. Android `assembleDebug` completed successfully.
  Physical profile switching and circular gesture direction still require the
  target-board interaction check.
# 2026-08-15 SkyOrb range visual feedback

- Reproduced the range-visibility issue with a source-level regression check: offline demo aircraft used fixed normalized radii, so every range produced the same layout.
- Unified the physical radar scale with the then-advertised 5/10/15/25 km choices; the later six-position update below extends this model to 5/10/15/25/35/50 km.
- Demo targets now have fixed distances in kilometres and are rescaled with the active range; out-of-range targets become rim markers, matching live-data behaviour.
- Added a persistent `RANGE n KM` readout and a half-range ring label so scale changes remain visible even when few aircraft are present.

## BLE radar coordinate readback

- Reproduced that Android read SkyOrb coordinates from the aggregate `GET SETTINGS` JSON, whose typical response is already about 241 bytes and can exceed a 256-byte ATT MTU payload when the SSID is longer.
- Added a compact `GET RADAR` command carrying `location_valid`, `location_auto`, latitude, longitude and range.
- Android now uses the compact response and distinguishes IP-derived, manually saved and not-yet-generated locations instead of silently hiding zero coordinates.
- Automatic location is asynchronous. `GET RADAR` now also reports
  `location_state` and `location_error`; Android polls it after
  `SET AUTO_LOCATION` and displays waiting, progress, success, or a concrete
  network/HTTP/parse failure instead of immediately falling back to "not set".
- The firmware first tries the documented key-free `https://ipapi.co/json/`
  client-IP endpoint, then falls back to the documented free
  `http://ipwho.is/` endpoint. This avoids making auto-location depend on one
  service or on the previously assumed HTTPS form of the ipwhois endpoint.
- Verification: `tests/check_radar_settings_ble.ps1`, ESP-IDF
  `build_wifi_fix`, and Android `assembleDebug` pass. Target-network provider
  reachability remains a hardware/network test.

# 2026-08-15 Offline radar and launcher polish

- Sky Radar no longer draws either demo or cached/live aircraft unless the
  station has a current Wi-Fi connection. The radar grid, sweep, range and
  explicit offline/network state remain visible.
- Radar network/status labels and the range label now use measured Montserrat
  glyph advances to center themselves on the physical display instead of
  relying on per-string hand-tuned X coordinates.
- After checking the current Google Play artwork, the launcher now follows the
  real OuO icon composition: black field, two white circular eyes on a
  diagonal, and a small white crescent mouth. A thin dark-grey rim keeps the
  black tile visible against PACON's launcher background. The interactive 0u0
  application itself is unchanged.
- Verification: `tests/check_skyorb_offline_and_launcher_ui.ps1`, the existing
  SkyOrb range and BLE radar checks, and ESP-IDF `build_wifi_fix` pass.

# 2026-08-15 Remove legacy SoftAP and HTTP setup server

- Removed the retired `PACON-Sky` SoftAP constants, AP event handling, URL/form
  parser, HTTP configuration pages and `esp_http_server` component dependency.
- Formal firmware networking is now STA-only. BLE and the board's saved-profile
  Wi-Fi page remain the only provisioning paths.
- Preserved `esp_http_client` because Sky Radar data and public-IP geolocation
  depend on outbound HTTP requests.
- Added `tests/check_sta_only_network.ps1` to prevent the AP/server path from
  being reintroduced while checking that STA mode, BLE Wi-Fi commands and the
  HTTP client remain present.
- All five source regression checks pass. ESP-IDF v5.4.3 produced a
  `0x1604e0`-byte application, 976 bytes smaller than the preceding
  `0x1608b0` build. It was flashed successfully through COM11. Startup capture
  confirmed SH8601, BLE advertising, FT3168 touch, QMI8658, AXP2101 and SD NAND;
  no setup AP or embedded HTTP server was started.

# 2026-08-15 SkyOrb HTTPS allocation failure

- Reproduced `DATA RETRY` while COM11 remained attached. Wi-Fi association and
  DHCP completed, the HTTP fallback generated automatic coordinates, but both
  HTTPS providers failed before sending a request with
  `mbedtls_ssl_setup returned -0x7F00`.
- ESP-IDF defines `-0x7F00` as `MBEDTLS_ERR_SSL_ALLOC_FAILED`. The active
  configuration forced all mbedTLS allocations into internal RAM and reserved
  16 KiB RX plus 4 KiB TX buffers while the pre-Wi-Fi largest internal block
  was only about 31 KiB.
- mbedTLS now allocates from the board's PSRAM and uses dynamic TLS RX/TX
  buffers. `tests/check_tls_memory.ps1` prevents reverting to the
  internal-only configuration.
- A transient target-board run displayed live aircraft data, but the result was
  not stable across the following firmware restart; it must not be treated as a
  completed fix until the fetch path is observed repeatedly on the current build.

# 2026-08-15 Sky Radar retry diagnostics

- Reproduced the reported `DATA RETRY` state while monitoring COM11. During more
  than five minutes of capture there was no Wi-Fi startup, association, DHCP, or
  aircraft HTTP log, proving that the generic label could outlive the request
  which originally set it.
- Wi-Fi OFF and a fresh `GOT_IP` now clear both the stale failure flag and fetch
  deadline. A fresh IP therefore triggers an immediate request.
- Failed aircraft requests now retry every 15 seconds instead of waiting for the
  normal 180-second success refresh interval.
- Added `[SKYORB-FETCH]` stage diagnostics and matching on-screen states for
  client allocation, TLS/network open, HTTP response, body read, and JSON parse
  failures. Added `tests/check_skyorb_retry_diagnostics.ps1`.
- The diagnostic build proved that Wi-Fi/DNS/TLS were healthy and
  `api.airplanes.live` was returning HTTP 403 with a project-approval message.
  A desktop request carrying a descriptive PACON `User-Agent` was rejected the
  same way, so this was not an ESP32 header or TLS defect.
- Switched the compatible point endpoint to the public `api.adsb.lol` service,
  which returned HTTP 200 in a direct validation request, and added a
  project-identifying `User-Agent`. The response remains readsb-compatible and
  uses the existing `ac` parser. ADSB.lol data is ODbL 1.0 licensed.

# 2026-08-15 SkyOrb successful-empty response and OLED wake redraw

- Runtime capture confirmed the replacement ADSB.lol request completed and
  parsed successfully, but the selected location/range returned zero aircraft:
  `SkyOrb: refreshed 0 aircraft`.
- The request completed after OLED idle protection had already issued display
  off.  Touch wake previously restored panel power and brightness but assumed
  the controller retained a visible frame.  Wake now marks the active screen
  dirty so its normal renderer restores the complete frame.
- A successful zero-aircraft response now displays `NO AIRCRAFT / IN SELECTED
  RANGE` instead of the ambiguous `LIVE 0` state.  A throttled
  `[SKYORB-RENDER]` log records flush result, link state, aircraft count,
  location state, fetch state, and panel sleep state.
- Regression: `tests/check_skyorb_empty_and_wake.ps1`.
- A separate `build_noccache` build tree was generated because the installed
  ccache instance could stall or report `File exists`. A 12-way uncached build
  completed successfully in about 50 seconds; the application is `0x1610e0`
  bytes and leaves 91% of the factory application partition free.

# 2026-08-15 50 km radar, rounded switches, and mechanical Watch

- SkyOrb range indices now map to `5/10/15/25/35/50 km`. Firmware validation,
  NVS clamping, rotary wraparound, BLE help/error text, Android range control,
  and visual regression checks share the six-position model.
- The Settings and Wi-Fi switch tracks now use capsule-shaped rounded drawing;
  the existing 0u0 menu switch was already rounded.
- Added `UI_SCREEN_WATCH` and a sixth launcher icon. The original watch face is
  inspired by the circular gold mechanical composition of the two supplied KKD
  APK references without copying their character art. It reads PCF85063 once
  per second, renders analog hands plus a date/time complication, and falls back
  to uptime with an explicit `RTC SET` warning while RTC data is invalid.
- The top-left Watch corner is an invisible return target; a normal tap changes
  between two dark/gold themes. Added
  `tests/check_watchface_and_switches.ps1`.
- All nine firmware source/regression checks pass. A clean 12-way ESP-IDF build
  produced a `0x162630`-byte application with 91% of the application partition
  free, and the image was flashed successfully to COM11 with a stable startup.
- The Android range selector was expanded to the same six positions;
  `assembleDebug` completed successfully and produced
  `PaconBleTool/app/build/outputs/apk/debug/app-debug.apk`.

# 2026-08-15 RTC calibration, alarm, watch-face fidelity, and guarded USB MSC

- Clarified the SkyOrb scale: the six values are radar radii, and the highest
  setting means 50 km from the screen centre to the outer ring. The on-screen
  readout now says `RADIUS 50 KM` rather than the ambiguous `RANGE`.
- Reworked Watch toward the supplied bright gold/orange reference: cream Roman
  numeral annulus, mechanical decoration, analog hands, a dark time/date
  subdial, and a central region which can reuse the user's current NAND media.
  No bitmap or character artwork was extracted from the reference APKs.
- Integrated PCF85063 into the formal firmware as the persistent clock source.
  Custom and phone-BLE time writes update the RTC; Wi-Fi calibration uses SNTP
  and then updates the same RTC. An invalid/VL RTC stays visibly marked instead
  of silently treating uptime as calibrated time.
- Added a persisted daily alarm. At the configured RTC minute GPIO48 emits an
  intermittent 2 kHz alert for up to 20 seconds; the same date cannot retrigger
  it. Touching Watch or BLE `STOP ALARM` stops the active alert.
- Added `GET CLOCK`, `SET TIME`, `SYNC WIFI TIME`, `SET ALARM`, `STOP ALARM`,
  and `SET WATCH STYLE` to BLE, plus an Android `时钟与闹钟` section covering
  custom time, phone/BLE sync, Wi-Fi sync, alarm control and watch style.
- Opening USB Disk now leaves Serial/JTAG active. Only the explicit switch on
  that page starts MSC and disconnects serial; after PC eject, switching OFF
  requests the safe reboot back to application/serial ownership.
- Added `tests/check_clock_alarm_usb.ps1` and changed the radar visual test to
  require `RADIUS`. ESP-IDF `build_wifi_fix` and Android `assembleDebug` both
  complete successfully; target-board time/alarm/USB interaction remains to be
  verified in this iteration.

# 2026-08-15 KKD1 character-arm watch mechanism correction

- The first Watch implementation was structurally incorrect: it placed a media
  image behind conventional software-drawn hour/minute hands. Inspection of the
  supplied `com.KKD1(1).apk` watch-face XML established that the character and
  both arms remain static; the hour and minute rings rotate behind those arms,
  so the arms themselves are the time pointers.
- Imported the five supplied KKD1 layers needed by that mechanism (character,
  seconds ring, minutes ring, Roman-hours ring, and centre mechanism) into
  compact embedded RGB565+alpha assets. The renderer now reproduces the APK
  layer order and transforms: minute/second rings rotate clockwise and the
  Roman hour ring rotates in the opposite direction, while the character stays
  fixed above them.
- Removed the conventional hour/minute/second line hands. The existing RTC,
  BLE/SNTP calibration, complication, alarm and hidden return interaction are
  unchanged.
- Added `tools/make_kkd1_watch_asset.py` for deterministic asset regeneration
  and strengthened `tests/check_watchface_and_switches.ps1` to reject a return
  of conventional hands. All firmware checks pass; `build_wifi_fix` builds a
  `0x350470`-byte image with 77% of the application partition free.

# 2026-08-15 Watch seconds-ring skipped-mark correction

- Target observation: the rotating seconds ring sometimes advanced two or four
  marks in one visible update. PCF85063 polling was already once per second;
  the visible ring nevertheless consumed the latest cached RTC second directly.
- The Watch cadence timestamp was also written after the layered render. Its
  real start-to-start interval therefore became one second plus render time,
  making skipped RTC values increasingly likely when a frame was expensive.
- Added a separate seconds-of-day display clock. A normal delay of up to ten
  seconds is rendered as consecutive one-mark catch-up frames; a deliberate
  large time correction still snaps to the newly calibrated time. A transient
  one-second-old RTC sample can no longer move the visible dial backwards.
- The one-second cadence is now anchored before rendering, so render duration
  is not added to every interval. `tests/check_watchface_and_switches.ps1`
  requires the one-step helper and rejects direct raw-RTC seconds-ring wiring.

# 2026-08-22 Configurable display timeout, interactive image crop, and OLED risk audit

- Added persisted `pacon_ui/sleep_s` display timeout values of 15/30/60/120/300
  seconds plus 0 for no panel-off. `GET STATUS`/`GET SETTINGS` now expose
  `screen_timeout`, and `SET SCREEN TIMEOUT` updates it over the verified BLE
  command channel. The existing 20-second dimming remains active even when
  panel-off is disabled.
- Android `设备与显示` now provides the matching timeout selector and reads the
  stored value back from PACON. Ordinary PNG/JPEG/WebP uploads now open a
  475x466 round-screen crop preview with one-finger pan and pinch zoom before
  RGB565LE conversion. Native `.rgb565` files keep the established direct path.
- Closed an OLED startup-state gap: GPIO6 is now explicitly driven low before
  I2C/PMIC setup instead of relying on reset defaults. Panel power then settles
  for 20 ms at zero brightness, and user brightness is restored only after
  panel initialization plus a further 40 ms.
- Screen-risk review: default 50% brightness, idle dimming, configurable true
  panel-off, wake redraw, and home/status pixel shifting are present. Remaining
  risk is cumulative OLED differential aging when using 100% brightness,
  static non-home pages, or the no-timeout option; firmware cannot validate an
  out-of-spec physical panel rail.
- Added `tests/check_display_timeout_and_crop.ps1`. All firmware source checks,
  ESP-IDF 5.4.3 `build_wifi_fix`, and Android `assembleDebug` pass. The firmware
  image is `0x350880` bytes with 77% of the app partition free.

# Current development plan status (2026-08-22)

1. BLE media management is complete and retained without protocol changes.
2. Android settings/transfer UX now covers media management, Wi-Fi profiles,
   radar, clock/alarm, brightness, display timeout, and interactive image crop.
3. Next work returns to final firmware cleanup/performance: remove obsolete
   transitional configuration paths, split the monolithic UI/network/media
   responsibilities into documented modules, then repeat full board regression
   without changing the validated display, charging, BLE, or media formats.

# 2026-08-22 Battery replacement and 4.2 V charge target

- After replacing the protected battery with a 4.2 V full-charge cell, changed
  AXP2101 register `0x64[2:0]` from `0x01` (4.0 V) to `0x03` (4.2 V).
- Kept the previously verified 50 mA constant-current, 25 mA precharge and
  25 mA termination settings unchanged. The periodic status decoder now reports
  every supported target code (4.0/4.1/4.2/4.35/4.4 V) instead of only 4.0 V.
- The target-board regression must confirm `target=4200 mV` and observe the BAT
  voltage and charger state near termination; software register configuration
  does not replace a multimeter check at the battery pads.

# 2026-08-22 Android BLE PING disconnect correction

- Reproduced the phone-side failure sequence after a successful connection:
  low-latency scanning continued, automatic media refresh occupied the
  acknowledged GATT write, manual `PING` could not enter the write queue,
  Android reported write status 133, then disconnected with status 8 at the
  five-second supervision timeout. The firmware `PING` handler was not the
  source of the disconnect.
- `connectSelected()` now stops scanning before `connectGatt()`. Manual control
  commands and media/settings commands all run on the existing single-thread
  `mediaExecutor`; the automatic reconnect catalog refresh is retained but
  delayed 1.2 seconds after notification setup.
- Added `tests/check_android_ble_command_stability.ps1`. The structural check
  passes and Android `:app:assembleDebug` completes successfully. The repaired
  APK was installed and launched on the authorized Xiaomi device; the final
  connection/PING regression still requires one physical tap sequence.
- Follow-up showed that disconnection also occurred without pressing `PING`.
  Android connected at 11:52:32.423, automatically changed from 1M to 2M PHY at
  11:52:34.093, and disconnected with status 8 at 11:52:39.222—about one
  supervision timeout after the PHY change. The pending 10-byte `GET STATUS`
  write then surfaced status 133, but was not the initial trigger.
- The Android client now calls `setPreferredPhy()` with 1M TX/RX immediately
  after connection and logs every PHY update. The regression check requires
  the explicit 1M preference; the rebuilt APK was installed on the Xiaomi
  phone for a 15-second idle-link and command test.
- The forced-1M build still disconnected with Android status 8. Its trace
  confirmed a successful 1M PHY update, MTU 256 and service discovery, followed
  by the reconnect-time automatic `GET STATUS` write and then the five-second
  supervision timeout. This disproves the 2M-PHY hypothesis but does not yet
  distinguish an idle-link failure from a first-command failure.
- Installed an explicit A/B diagnostic APK with
  `AUTO_MEDIA_REFRESH_ON_CONNECT=false`. It performs discovery and CCCD setup
  but sends no command until the operator presses one, so a 15-second idle wait
  followed by a manual `PING` can identify which side of that boundary fails.
- Board testing completed that isolation: the idle connection remained up and
  `PING`/`PONG` also remained stable, but opening Android “设备与显示” sent
  `GET SETTINGS` and disconnected the link. The command shared the full status
  JSON, which can reach about 300 bytes while an MTU-256 notification can carry
  only 253 bytes.
- Fixed two firmware faults in that failure path. `GET SETTINGS` now has a
  compact dedicated response and `GET STATUS` omits radar/Wi-Fi detail that has
  separate query commands. `notify_response()` bounds every notification by
  the negotiated ATT MTU and no longer frees an mbuf after
  `ble_gatts_notify_custom()`, whose API consumes it even on failure. The old
  failure path therefore performed a double free after the oversized notify.
  Disconnect reason logging and `tests/check_ble_notification_safety.ps1` were
  added. Both BLE checks pass and the ESP-IDF 5.4.3 formal firmware build
  succeeds in `build_wifi_fix`. The corrected image was flashed successfully
  to the ESP32-S3 on COM11; Android “设备与显示” and reconnect catalog
  regression remain to be confirmed on the board.
- Fixed the Android client's clipped bottom diagnostics before continuing the
  BLE regression. The log now has its own 300 dp scrolling pane, follows the
  newest line automatically, and yields outer-page gesture interception only
  while the operator drags inside the log. Root padding now includes the
  runtime status/navigation-bar insets on both Android 30+ and the legacy API.
  `tests/check_android_log_visibility.ps1` and the BLE command-stability check
  pass; the debug APK built successfully and was installed/launched on the
  authorized Xiaomi handset. A captured device screenshot confirms that the
  log card ends above the system navigation bar.

# 2026-08-22 Alarm trigger and watch-style differentiation

- Replaced the scheduled alarm's fragile `second < 2` gate with a whole-minute
  match plus the existing once-per-date key. A delayed RTC poll can therefore
  no longer miss the alarm merely because it first observes second 2 or later.
- Added BLE `TEST ALARM` and an Android `测试响铃` action for direct GPIO48
  verification. LEDC timer/channel/duty failures are now checked and logged;
  the audible test uses a 2 kHz, 50% duty tone with the existing 20-second
  alarm cadence and can still be stopped through `STOP ALARM`.
- Added a genuinely separate KKD2 face from the extracted `com.KKD2.apk`
  background, character/hour, arm/minute and complication assets. The Android
  selector now names `KKD1 · 狂三双臂指针` and `KKD2 · 旋转人物表盘`, rather
  than presenting two nearly identical color variants.
- Added `tests/check_alarm_watchface_runtime.ps1`; it failed against the prior
  first-two-seconds trigger and color-only style, then passed after the runtime,
  asset and Android control changes.
- Corrected a separate Android usability regression in the diagnostics pane.
  `ScrollView.fullScroll(FOCUS_DOWN)` moved keyboard focus to the log whenever
  BLE output arrived, which could also pull the outer settings page away from
  the button being pressed. New lines now follow only when the log was already
  at its bottom, using focus-free child `scrollTo`; otherwise both the log and
  the outer page retain the operator's position.

# 2026-08-22 Watch-face layer isolation

- A board photo showed KKD1 looking like two watch faces were stacked. The
  style selector itself was already mutually exclusive; the actual cause was
  the KKD1 path painting a second procedural gold face and three generated
  gears before compositing the extracted APK dial, mechanism and character
  layers.
- Split KKD1 into `watch_compose_kkd1()`. Both KKD compositors now clear the
  full canvas and draw only their own APK-derived layers. Removed the obsolete
  procedural gear renderer while retaining the KKD1 digital complication.
- Added `tests/check_watchface_layer_isolation.ps1`. It first failed on the
  mixed renderer and now enforces dedicated routing, full-frame clearing and
  the absence of procedural gear overlays.

# 2026-08-22 KKD1 reference-frame differential check

- A second board photo still appeared visually crowded, so the dial transform
  was checked against the APK's own 450x450 preview rather than adjusted by
  eye. `tools/compare_kkd1_rotation.py` recovered the preview's 10:08 minute
  layer at -48 degrees in PIL coordinates and its hour layer at -150 degrees;
  these exactly match the firmware's clockwise transform convention. The
  rotating rings and static character are therefore intentional APK behavior,
  not remaining KKD1/KKD2 overlap.
- The differential check exposed the remaining real mismatch: firmware had
  omitted APK resource `wfs_6` and drew a larger synthetic complication at
  `(74,158)`. The converter now scales `wfs_6` into the XML-prescribed
  110x110 box at `(106,121)`, embeds it as `kkd1_complication.rgb565a`, and
  centres the time/date inside the original slot before drawing the character.
- Added `tests/check_kkd1_complication_layout.ps1`. It failed before the asset
  existed, then passed together with the layer-isolation, alarm/watch runtime,
  and watchface/switch checks after the correction.
- Recorded an unambiguous `00:02` visual reference: the rightward character arm
  points to Roman `XII` and the raised arm points to minute tick `2`. The
  2026-08-22 board photograph matches that geometry. Its displayed complication
  also reads `00:02`; when that differs from wall-clock time, the remaining
  fault is RTC/time synchronisation rather than watch-face composition.

# 2026-08-22 Verified watch-style hand-off

- A later report described KKD1 and KKD2 as still mixed. The supplied board
  photo is a complete KKD1 frame and matches the APK-derived `00:02` reference;
  it contains neither KKD2's inverted character nor a retained partial frame.
  The unresolved ambiguity was that Android did not show whether the device
  had actually accepted the requested style.
- `GET CLOCK` now returns both the numeric `style` and `style_name`. Android's
  Apply action performs a write followed by a device read-back and reports a
  mismatch instead of always claiming success. The settings status continuously
  identifies the device-reported KKD1/KKD2 style.
- On a real style transition, the UI task now flushes one explicit black full
  frame before composing the selected APK face and logs the rendered style.
  This gives a deterministic visual boundary without affecting normal
  one-second watch animation. `tests/check_watch_style_roundtrip.ps1` locks the
  command/read-back/clear contract.
# 2026-08-22 KKD2 reference pose and deterministic tool environment

- Compared the reported KKD2 16:02 board pose with the original watch-face
  XML. The APK rotates the complete character by `HOUR_0_23 * 30` and the
  separate arm by `MINUTE * 6`; therefore the arm's near-12-o'clock position
  at 16:02 is intentional. Firmware removed its extra minute interpolation
  from the character layer so the transform now matches the APK exactly.
- Added `tests/check_kkd2_reference_pose.ps1`, which first failed on the
  interpolated character angle and now locks the XML formulas, asset origins,
  layer order and the 16:02 reference pose (120 degrees / 12 degrees).
- Added `tools/pacon.ps1` plus VS Code tasks. Firmware operations now use one
  verified contract: ESP-IDF 5.4, `build_wifi_fix`, pinned Ninja 1.12.1,
  24 build jobs and COM11. The wrapper validates all paths before doing work,
  reads the generated flash manifest rather than hard-coding binary names,
  and also exposes deterministic Android build/install commands. Historical
  root and `build_noccache` trees remain untouched but are no longer selected.
### 2026-08-22：固定串口监视器 Python 环境

- `tools/pacon.ps1 -Action monitor` 现在显式设置 `IDF_TOOLS_PATH` 与 `IDF_PYTHON_ENV_PATH`，与编译、烧录共同使用 `D:\espidf\mytools` 和其中的 `python_env\idf5.4_py3.11_env`。
- 同时把已验证的 `xtensa-esp-elf\esp-14.2.0_20250730` 工具链加入监视器子进程的 `PATH`，避免 `addr2line` 又落到残留环境或无法解析异常地址。
- 原因：`idf.py monitor` 会启动子进程；若只用固定 Python 调用 `idf.py`，子进程仍可能读取旧的全局 `.espressif` 环境路径并报 Python 不存在。
- 后续统一使用 `tools/pacon.ps1` 的 `check/test/build/flash/monitor` 入口，避免 VS Code 或用户环境变量漂移造成重复排查。

### 2026-08-22：KKD2 左臂肩部遮挡修复

- 多个时刻的实机照片证明问题不是单一角度或坐标偏移，而是独立分钟臂位图始终绘制在完整人物上方，暴露了肩部根节点，视觉上形成悬浮、脱体。
- 嵌入式素材为不透明栅格，合成顺序调整为：背景、信息盘、独立分钟臂、完整人物/小时层。完整人物最后覆盖肩部根部，臂端仍可随分钟旋转显示。
- `tests/check_kkd2_reference_pose.ps1` 新增固件栅格层顺序断言，防止后续又恢复为脱体显示。

### 2026-08-22：KKD2 左臂图层关系纠正

- 最新完整表盘照片确认上一版对遮挡关系的判断相反：完整人物层后绘制时，会遮住独立左臂靠近肩部的一段，因此即使坐标正确，视觉上仍像断臂。
- 保留已经校正的旋转中心和坐标，仅恢复原 APK 的图层顺序：背景、信息盘、完整人物/小时层、独立左臂/分钟层。左臂作为前景覆盖在人物之上，肩部连接段不再被身体层截断。
- 同步修改 `tests/check_kkd2_reference_pose.ps1` 的层序断言，防止以后再次把人物层绘制到独立左臂之后。
### 2026-08-23：KKD1 / KKD2 信息盘文字光学居中

- 根据两种表盘的实机照片分别校正黑色圆形信息盘内的时间与日期，而不是共用旧坐标：KKD1 时间/日期顶部调整为 160/187，KKD2 调整为 162/188，水平中心均保持 XML 槽位中心 x=161。
- KKD2 恢复两行信息（18 px 时间、14 px 日期），避免只显示单行时间且整体贴近圆盘上沿；KKD1 同样显式使用两行独立坐标。此次未改动已经验证正确的 KKD2 左臂坐标和图层顺序。
- 最新实机照片确认：仅按字体 `adv_w`（逻辑前进宽度）居中时，Montserrat 字形的左右边距会让可见笔画仍显得偏左。现新增 `watch_text_visible_bounds()`，利用 LVGL 字形描述中的 `box_w`、`box_h` 和 `ofs_x` 计算整行文字的实际可见像素包围盒，再由 `watch_text_optically_centered()` 将包围盒中心对准黑色圆盘中心；KKD1、KKD2 的时间和日期均使用该方法，纵向坐标、手臂坐标和图层顺序保持不变。
- `tests/check_watch_complication_text_alignment.ps1` 与 `tests/check_kkd1_complication_layout.ps1` 已先验证旧实现会失败，再验证新实现通过；表盘图层隔离、样式切换和闹钟/表盘运行时回归检查也通过。

### 2026-08-23：BLE HID 手机拍照遥控

- 在现有 NimBLE host 的 PACON GATT 数据库中增加标准 HID-over-GATT 服务
  `0x1812`，报告地图使用 Consumer Control 的 Volume Increment 输入位；
  不启动第二套 HID host，避免破坏已经验证的自定义命令和媒体通道。
- 广播响应同时携带 PACON 自定义服务 UUID 与 HID 服务 UUID。`CAMERA SHUTTER`
  和 Settings 页的 `Camera shutter` 行通过一个独立任务发送 35 ms 的按下/释放
  报告，`SET CAMERA REMOTE ON|OFF` 提供显式开关，默认开启。
- Android BLE Tool 增加 `拍照` 命令按钮；但只有在手机系统蓝牙设置中把
  `PACON-BLE-TEST` 配对为 HID 输入设备后，系统相机才能接收 HID 快门，单纯
  的自定义 GATT 连接只适合验证命令返回。
- 新增 `tests/check_camera_remote.ps1`，固定检查 HID UUID、Consumer Control
  报告、快门 worker、PACON 命令、Settings 入口和 Android 按钮，拒绝再次引入
  第二套 NimBLE HID host。
- 尚未在目标手机/相机和实物板上完成配对、音量键快门以及 HID 通知的最终回归；
  本次先完成固件/Android 构建级验证。

### 2026-08-23：拍照遥控双连接补全

- 上一轮日志记录了 Android `拍照` 按钮，但源码实际缺少该控件；现已在
  `PaconBleTool` 添加 `拍照 / 音量键快门` 按钮，连接就绪后发送现有
  `CAMERA SHUTTER` 命令。
- 发现手机系统 HID 主机与自定义 BLE 工具不能共用原先的单连接配置；将
  NimBLE 最大连接数从 1 调整为 2。固件现在保存每条连接的句柄，响应通知回到
  发起命令的连接，HID 快门报告只发送到已订阅 HID Report 的连接。
- 广播在首条连接建立后继续开启，允许系统 HID 与 Android 工具分别建立连接；
  断开任一连接不会清除另一条连接的 HID 状态。
- `tests/check_camera_remote.ps1` 与全部 `check_*.ps1` 源检查通过；固定
  ESP-IDF 5.4.3 工具链下固件完整编译通过，生成 `build_wifi_fix/pacon_fluid_pendant.bin`。
- 尚未在目标手机和相机上完成真实配对/快门回归；实测时需确认系统相机已启用
  音量键快门，且串口日志出现 `camera HID notify ... enabled`。

### 2026-08-23：OuO 采集几何全状态复核

- 对 `capture_1_611` 的待机、点击、长按、拖动和多点录屏抽帧重新做了连通域测量，确认默认是圆眼+深 U，眨眼是上缘裁切的圆眼，稳定闭眼帧可作为开心的浅上弧眼，难过是斜上眼睑+小皱眉，亲吻是弧形闭眼+侧开 `3` 嘴，猫嘴是横条眼+双弧嘴，惊讶是平顶圆嘴。
- 旧版预览/固件的 44px 眼睛、214px 间距、默认斜眼和浅 W 嘴与这些证据不符；统一改为 `eyeRadius=30`、`eyeGap=226`、`eyeY=216`、`mouthY=264`，并保持预览器和固件同一组几何。
- 螺旋眼、巨型实心嘴、独立裁切眼以及 H/I 形连续拖动帧仍只保留在研究边界，不作为 PACON 正式情绪，避免圆屏上出现不适或损坏感。
- 新增 `tests/check_ouo_geometry.ps1`，`pacon.ps1 test` 全部通过；固件已重新构建并烧录至 COM11。
- 复核预览器后修正“开心”：采集没有独立的命名开心帧，因此采用 `idle_08/idle_19` 中稳定的浅上弧闭眼和深 U 嘴；默认状态继续使用圆眼，避免把中性圆眼误当成开心眼睛。
- 闭眼弧线按采集连通域收窄到约 56×26（466 坐标），并同步 `OUO_EXPRESSION_HAPPY`、亲吻和闭眼平静的固件像素判断；预览器/固件测试通过并再次烧录 COM11，三段镜像均报告 `Hash of data verified`。
- 亲吻嘴按 `holds/mouth_300.png` 重画为更厚的双弧 `3`；根据新录制视频 `视频/Screenrecorder-2026-08-23-17-51-15-417.mp4` 的 `new_pull_1751/frame_008.png`，嘴侧拉改为左侧填充 `3`、白色中心横梁和独立右弧，并保留两者之间的小黑缝。新增 `check_ouo_mouth_shapes.ps1`，本轮源检查通过；固件待本轮构建后重新烧录。
- 根据新增的四张拉嘴参考图，把嘴拖动改成按完整向量实时形变：横向填充条、纵向胶囊、斜向圆角方形、向上尖角；极限横拉仍保留录屏中的 `3`+独立右弧。修复预览器原先只改参数但 `kissMouth()` 不读取参数的问题，并修复固件嘴触摸分支误设 `s_ouo_mouth_touch_active=false` 导致拖动永远不进入的问题。
- 补齐表情触发映射：左/右眼短按分别触发 wink，眼睛长按或双指眼部触摸触发惊讶，头部横向拖动触发生气，头部持续按压触发困倦，摇晃触发 dizzy，情绪滑块继续触发难过/开心。新增 `check_ouo_gesture_triggers.ps1`；全量测试、编译通过，固件已重新烧录 COM11 且三段镜像校验成功。
- 预览器同步固件触摸映射：左/右眼短按、眼睛长按、头部点击/横拖/持续按压、嘴拖向量、脸颊挤压和快速往返摇晃均有对应触发；新增眩晕时间相位，让固件和预览器的两只螺旋眼持续同向旋转。新增 `check_ouo_expression_animation.ps1`，并通过全量源检查与 JavaScript 语法检查。
- 用户实测嘴侧拉只能落到惊讶/亲吻，定位为横向阈值过高且预览器拖动未调用侧拉几何；固件和预览器统一改为水平约 32px 即显示填充 `3`+独立弧线，并重新构建烧录 COM11，三段镜像校验成功。
- 预览器拖动状态改为首个明显向量后锁定：水平拖动进入嘴侧拉后不再经过惊讶，非水平拖动进入惊讶后也不回切；嘴侧拉忽略 Y 位移，惊讶对角方嘴改为屏幕对齐。新增 `check_ouo_gesture_transitions.ps1`，全量源检查和 JavaScript 语法检查通过。
- 继续对照 Android 任意角度拉嘴表现，移除惊讶嘴在 45° 附近的 `square` 分类；预览器改用独立前后/上下边缘的连续不规则四边形，向上拖动仍为三角形，新增 `check_ouo_mouth_continuity.ps1`。
- 依据接触表 `capture_1_611/new_pull_1751` 做了预览器固定方向实验，并生成 `tools/ouo-preview/experiments/pull_experiment_comparison.png`；对照发现 OuO 的侧拉/纵拉/尖角是连续路径中的多种轮廓，而不是离散方形。修正预览器尖角实验的方向，使上拉尖端朝上；全量测试通过。
- 根据用户重新提供的四张 Android 基准图，修正预览器四个研究按钮：横向不再调用极限 `3`+右弧，改为宽扁填充嘴；纵向、圆角方嘴、三角嘴按参考宽高和曲率重画，并将拉嘴专用锚点下移 20px。新增 `tests/check_ouo_pull_reference.py`，对预览器截图的嘴/眼比例和垂直偏移做回归测量。
- 同一组基准图的眼睛连通域高度约为宽度的 0.87；四个拉嘴研究帧改用压扁圆眼，普通状态继续保留圆眼。已生成 `tools/ouo-preview/experiments/pull_experiment_comparison.png` 做一一对应复核。
- 修复实际鼠标/触摸拖嘴仍显示近似方形的问题：真实拖动现在统一进入 `livePullMouth()`，按拖动向量连续计算独立左右/上下边缘和曲线，不再调用旧的水平侧拉阈值或角度方形分支；向上拖动保留尖角，向下拖动保持胶囊轮廓。拖动中的眼睛同步采用基准图的压扁比例；右侧“嘴侧拉”按钮仍保留独立的录屏参考形。新增 `tests/check_ouo_live_pull.ps1`，预览器脚本和全量源检查通过。
- 实测预览器右下 45° 拖动后发现轮廓虽不旋转仍过于接近圆角方块；收窄斜向交叉放大项，增加独立侧边曲率，并固定上下边缘水平，仅让拖动方向的侧边产生轻微外鼓，避免出现倾斜方嘴。
- 复现四个方向后发现侧边外鼓使用了无符号的正值，导致左/上拖动也残留右下轮廓；改为按 `Math.sign(dx/dy)` 施加水平/垂直曲率，并加入方向对应的上下边缘弧度。新增实时拖动回归断言，防止外鼓方向再次固定。
- 用户复测发现真实水平拖动虽能进入模式但视觉上不像“嘴侧拉”，且横向阈值过窄；预览器将横向主导阈值放宽到 `abs(dx) >= abs(dy) * 1.25`，并让该状态直接绘制录屏采集的填充 `3`+独立右弧，惊讶/亲吻继续使用连续向量轮廓。
- 真实指针复测左下/右下后，将 45° 斜向的宽高交叉项恢复到 Android 圆角方嘴比例，同时把方向偏置按 `1 - 0.9 * cross` 平滑衰减；斜向不再被拉成菱形，水平/垂直方向仍保持各自的宽条和胶囊/尖角特征。
- 再次复测发现通用不规则路径仍会让纯下拉和斜下拉误读成菱形；新增 `liveVerticalPullMouth()` 与 `liveDiagonalPullMouth()`，将真实向下拖动直接对齐竖胶囊参考帧、斜向拖动对齐屏幕坐标的圆角方嘴，避免轮廓旋转或固定偏向右下。水平主导仍走录屏的填充 `3`+独立右弧。
- 复现“先向下再沿半圆向左/右划”后发现，首次方向只锁定了惊讶/嘴侧拉状态，未锁定轮廓族；新增 `pullFamilyForVector()`，在第一次明显移动时锁定竖向胶囊、斜向圆角方嘴、上拉尖角或嘴侧拉，半圆后续只改变尺寸，竖向手势不再切换成菱形/方嘴。新增运行时半圆轨迹回归测试。
- 上述轮廓族锁定过度，导致连续半圆手势只剩竖胶囊或上尖角；保留惊讶/嘴侧拉两种交互模式锁定，改为在惊讶模式内按当前向量切换横条、竖胶囊、斜向圆角方嘴和上尖角。横条也改为独立屏幕对齐的实时参考轮廓，新增回归断言确保一次半圆轨迹能经过竖向、斜向和横向族且不调用旋转。
- 根据 Android 实测“每次拉嘴类型随机”的行为，实时嘴拖动在首次明显移动时从圆、方、横向宽扁、竖胶囊、尖角和录屏侧拉中随机选一类，并在本次拖动内保持；新增圆嘴/实时侧拉原语和运行时回放断言，固定横向参考帧仍保持 `126×70`。
- 根据用户补充，横向宽扁与竖胶囊属于同一个“拉伸”随机族，而不是两个独立随机类型；合并为 `stretch` 变体，按当前拖动方向输出横条、竖胶囊或斜方嘴，圆/尖角/侧拉仍作为其它随机族。
- 为解决用户无法稳定触发横向宽扁嘴的问题，首次水平主导的左右拖动现在确定选择 `stretch` 变体：嘴形保持横向宽扁并随左右方向移动；非水平首方向继续随机，固定“嘴侧拉”研究按钮仍保留录屏中的填充 `3`+独立右弧。新增左右拖动回归断言并通过全量测试。
- 根据用户重新区分轮廓族：斜向圆角方嘴从 `stretch` 中拆成独立 `square` 变体，横向宽扁与竖胶囊继续共用 `stretch`。为恢复嘴侧拉触发，改为从白色嘴左/右边缘开始水平向外拖动时锁定 `side`，左右使用镜像的填充 `3`+独立右弧；嘴中央水平拖动仍稳定显示 `stretch` 横条。新增左右边缘触发回归断言。
- 修复横条拖动初段高度增长过快导致“宽扁嘴看起来像方嘴”的问题，实时横条改为从首个明显样本就保持约 1.8:1 宽高比；扩大嘴部命中范围，并让嘴命中优先于左侧隐藏菜单，避免左嘴边点击被菜单吞掉。回放测试改用实际嘴视觉中心和左右边缘，专项及全量测试通过。
- 根据用户补充的动态参考，重写 `stretch` 轮廓为“竖胶囊→宽扁 blob”的连续幅度插值：宽度由约 42 增至 126，高度由约 84 收到 70，并复用参考图的平顶、圆侧和浅 U 底边。嘴侧拉起始区扩大到嘴左右半区，不再强制从最外边缘向外拖，降低触发难度。
- 再次复核活动轨迹后修正轴向响应：纯向下拖动只沿 Y 增加竖胶囊高度，横向宽度保持约 42；只有 X 分量和幅度共同增加时才展开为宽扁 blob。独立 `square` 变体改用 `irregularPullMouth(dx,dy,mag)`，不再无论方向都绘制同一个正方形；新增纯下拉、水平展开和不同斜向方嘴回放断言。
- 用户复测发现斜向方嘴重新出现右下菱形、竖向拖动夹带横向变宽；将 `square` 改成屏幕对齐的独立边框，只允许单侧不对称，不旋转；`stretch` 按主轴锁定，竖向及轻微横向抖动保持胶囊宽度，水平主导才展开宽扁嘴。新增带横向/纵向抖动的回放断言并通过全量测试。
- 修正拉伸轮廓的锚点：以前宽度以嘴中心对称增长，导致拖向一侧时反侧也被拉宽；现在固定与拖动方向相反的边缘，只向手指方向扩展，竖向拖动在 Y 轴采用同样的反侧锚定。回放测试新增左右、上下边界方向断言。
- 将预览器的嘴拖动族同步到 PACON 固件：扩大嘴命中区并让嘴优先于隐藏菜单；首次明显移动锁定随机圆/拉伸/方/尖角或左右嘴侧拉族，水平中央拖动固定进入拉伸族，左右半区水平拖动进入镜像侧拉；拉伸和方嘴使用屏幕对齐、反侧锚定的实时轮廓。统一固件嘴中心为 466px 参考坐标，并完成全量源检查与固件构建。
- 修复眩晕眼动画相位系数过小的问题：原固件每 16ms 仅前进 0.000144 弧度，改为约 0.18 弧度/帧与预览器同步；新增数值回归检查，重新构建并烧录 COM11，三段镜像校验通过。
- 用户复测发现眩晕动画仍有卡顿感；定位为主循环统一使用 30ms（约 33 FPS），为 OuO 增加 16ms 专用帧周期，保持 Fluid 等页面原节奏不变；新增帧周期回归检查，重新构建并烧录 COM11，三段镜像校验通过。
- 进一步定位眩晕卡顿：`ouo_dizzy_eye_pixel()` 在动态区域的每个像素上都先执行 `sqrtf/atan2f`，即使像素远离眼睛；增加 33px 半径的整数距离预裁剪，减少无效浮点计算。新增回归检查，重新构建并烧录 COM11，三段镜像校验通过。
- 修正嘴侧拉与预览器不一致：固件此前将完整 `travel_y` 用作嘴位置偏移，斜向侧拉会把固定高度的 `3`+独立弧线推到眼睛附近；侧拉模式现在仅允许 ±6px 的基线漂移，轮廓高度保持不变。新增固定高度回归检查，重新构建并烧录 COM11，三段镜像校验通过。
- 根据 `视频/Screenrecorder-2026-08-24-23-15-48-633.mp4` 复核双侧向中间挤压：录像不是固定 `SQUISH` 嘴，而是“默认笑脸 → 斜底/半圆眼 → 横向夹嘴 → 填充 `3`+独立弧 → 细横条 → 回弹”的连续阶段。预览器新增双指位置跟踪、`squishPhase` 和回弹动画；固件新增 `s_ouo_squish_phase`、双指活动标志及阶段化眼/嘴像素原语，释放后保留相位回放恢复过程。新增回归断言，`pacon.ps1 test` 全部通过，固件已完整编译；本轮尚未烧录，待实物双指触控验证后再刷写。
- 因电脑鼠标只有一个 Pointer ID，预览器工具区新增“播放双指挤压”演示按钮，自动播放同一阶段时间轴，便于不接触摸屏时先核对视觉效果。
- 复核演示后发现中段错误复用了单侧嘴侧拉；依据录像 5–8 秒帧拆出“双侧外弧 + 中间独立白色 3 核心”的专用轮廓，预览器和固件均不再调用 `pullSideMouth`。专项/全量测试通过，固件重新完整编译。
- 对照录像中段参考帧继续修正双侧挤压嘴：外侧弧线改为向中心明显内弯的平滑曲线，中间 `3` 改为连续双贝塞尔弧，预览器和固件使用同一组采样几何；测试通过并重新编译固件，尚未烧录。
- 修正双侧挤压阶段中央 `3` 与 `一` 字线越过两条外弧内侧尖点的问题：扩大外弧中线间距，并将两种中央形状收进安全边界；新增几何回归断言，测试通过并重新编译固件，尚未烧录。
- 根据 `视频/Screenrecorder-2026-08-25-00-10-34-859.mp4` 新录制复核嘴侧拉：侧拉的 `3` 核心、中心桥和独立弧线会随拖动幅度连续变高/变宽，固件新增幅度参数并与预览器同步；双指挤压改为先经过夹嘴/一字阶段，达到较高压缩相位后才进入双弧+3。新增时序与幅度回归断言，测试通过并重新编译固件，尚未烧录。
- 将双指挤压夹嘴阶段的外侧弧线也改为与预览器一致的内弯采样曲线，避免固件仍显示直括号；专项及全量测试通过，固件重新编译，尚未烧录。
- 针对实物双指挤压无法触发的问题，确认 FT3168 支持两点触控；触摸轮询改为一次性读取 0x02~0x0C 的完整帧，校验第二触点事件位，并把双指候选从两侧开始后锁存到整个内收过程，放宽纵向和侧边触发范围。嘴拖动的纵向偏移统一限制为 `travel_y / 8` 的 ±6px，避免固件嘴形上移覆盖眼睛。Fluid 的形状、调色板模式、色相和饱和度新增 NVS 保存/恢复，重新进入页面不再回到默认值。新增 `check_touch_and_fluid_persistence.ps1`；专项、全量测试和固件构建通过，尚未烧录。
- 实机复测仍失败后重新对齐预览器：固件此前遗漏了拉嘴专用 `mouthY+20` 下移锚点，而且把限幅后的纵向位移传给轮廓尺寸计算，造成嘴上移覆盖眼睛并明显变小。现已分离原始拖动向量与显示平移（X 限 ±8、Y 限 ±6），拉嘴轮廓重新按原始向量增长；双点读取接受 FT3168 的多个有效事件编码，并新增一次性触点数量/坐标串口诊断。全量测试和固件构建通过，待烧录验证。
- 上述修正版已烧录 COM11，Bootloader、应用和分区表均报告 `Hash of data verified` 并完成硬复位；待实机复测双点日志和拉嘴位置。
- 解析 COM11 实机日志后确认触摸控制器在本次操作中每帧都只上报 `contacts=1`，没有任何 `contacts=2`，因此固件双指处理函数未被调用；这不是双指间距阈值问题，需继续区分触摸膜/控制器配置与实际手指接触。另修复拉嘴动态轮廓条件误用限幅偏移（±8/±6）导致分支永远不进入的问题，现改用原始拖动向量触发动态嘴型；专项测试和固件构建通过，本轮未烧录。
- 上述动态嘴型修正版已烧录 COM11；Bootloader、应用和分区表均报告 `Hash of data verified`，并完成 RTS 硬复位，待实机重新验证嘴型族和 `contacts=2` 日志。
- 根据用户确认，最新串口记录就是双指挤压实测：记录中仍只有 `contacts=1`，没有 `contacts=2`，因此双指失败可确认发生在触摸控制器/触摸膜上报层，而非 OuO 手势分支。另修正圆嘴和三角嘴的动态尺寸使用单轴最大值/`|travel_y|`造成斜向或横向拖动增长过小的问题，改为使用完整二维拖动长度；新增 `check_ouo_mouth_activity.ps1`，全量测试和固件构建通过，待烧录验证。
- 按硬件确认计划加入只读触摸身份探测：启动时读取标准 FocalTech `0xA3/0xA6/0xA8` 寄存器并打印原始值，不参与 `s_touch_ready` 判定，也不改变触摸帧解析。烧录后可据此判断实际控制器变体；专项/全量测试通过，固件构建完成（`build_wifi_fix/pacon_fluid_pendant.bin`，`0x44CF40`，分区余量 71%），待烧录读取实机 ID。
- 已烧录上述版本至 COM11：Bootloader、应用和分区表均报告 `Hash of data verified`，并完成硬复位。启动日志显示 `Touch ID probe @0x38: A3=0x64(ok) A6=0x01(ok) A8=0x11(ok)`，触摸控制器可正常读取这些身份寄存器；进入 0u0 后仍需再次实测双指是否产生 `contacts=2`。
- 分析用户最新 COM11 记录：共 59 次 `contacts=1`、59 次释放，`contacts=2` 和更高触点均为 0；同时没有复位、Panic 或异常。单点眼睛、脸颊、嘴部触发均正常，因此 I2C 读帧和单点坐标解析工作正常，双点失败仍发生在触摸控制器/触摸膜没有上报第二触点这一层。
- 核对用户提供的 DWO 页面对应公开规格：`SPEC-DO0143FMST10`（1.43 英寸 466×466）写明 Touch Driver IC 为 `FT3168 or Compatible`、触摸 I2C 地址为 `0x38`，但未声明最大触点数；FocalTech FT3168 芯片数据手册本身明确支持两点触控。因此可确认芯片方案具备双点能力，但不能据此保证具体模组的触摸膜/兼容芯片配置启用了双点；这与 COM11 实测始终 `contacts=1` 相符。
- 记录 `POWER_SAVING_PLAN.md`：以 402525 250mAh 电池和 DO0143FMST12 AMOLED 为基线，
  分六阶段推进屏幕深度休眠、静态画面保护、Wi-Fi/外设/ESP 动态功耗和低电量策略。
- 完成阶段 1 代码：休眠按亮度归零→`DISPOFF`→`SLPIN`，唤醒按 `SLPOUT`→非阻塞等待
  120ms→恢复运行寄存器→零亮度 `DISPON`→重绘→恢复用户亮度；首次触摸只唤醒不触发控件。
  新增专项回归检查，完整固件以 ESP-IDF 5.4.3 编译通过，按用户要求做 20 次循环及电流实测。
- 上述阶段 1 固件已烧录 COM11；Bootloader、应用和分区表均通过 `verify_flash` 摘要校验并硬复位，待实机完成 20 次睡眠/唤醒观察。
- 钟表页面新增本地左右滑动切换 KKD1/KKD2：水平移动至少 64px 且明显大于纵向偏移才触发，
  单次接触只切换一次并立即保存 NVS；普通点击不切换，左上角返回和闹钟停止行为保持不变。
  新增专项测试，全量源码回归与固件构建通过；已烧录 COM11，三个镜像摘要校验一致并硬复位。
- 实机发现表盘切换会黑屏且滑动偶发不响应。根因一是样式变化分支主动发送纯黑全帧，
  根因二是 64px/20px 的滑动限制偏严；同时一秒整帧传输会拉长触摸采样间隔。
  现改为只发送合成完成的新表盘帧、阈值放宽为 42px/6px，并在表盘全帧传输后补采一次触摸。
  新增质量回归测试，全量检查和构建通过；已烧录 COM11，三个镜像摘要匹配并硬复位。
- 表盘切换消除黑闪后实机仍有漏触；继续定位为连续快速滑动时主循环可能漏掉短暂的
  `contacts=0`，导致下一次 DOWN 被旧锁挡住，同时全帧 DMA 传输存在触摸采样盲区。
  固件现解析 FT3168 主触点事件位，用钟表页的新 DOWN 安全重置手势，并在约 8 条 DMA 条带间采样。
  新增采样回归测试，全量检查和构建通过；已烧录 COM11，三个镜像摘要匹配并硬复位。
- 提高 FT3168 采样后实机仍感觉表盘滑动漏触，定位到新的渲染竞态：DMA 条带期间采到滑动会
  设置 `s_watch_dirty`，但旧帧完成时又用 `catch_up_pending` 覆盖，导致新样式延迟绘制甚至被下一滑切回。
  现对每帧锁定 `frame_style`，合成显式使用该快照，帧末保留传输期间的样式变化并立即重绘。
  新增竞态回归测试，全量检查和构建通过；已烧录 COM11，三个镜像摘要匹配并硬复位。
- 表盘漏触经三轮 COM11 手势轨迹实测收敛：旧版被完整记录的 18 次手势全部成功，说明 42px
  阈值和样式状态机不是主要原因；未记录的短触摸来自钟表页仍沿用 30ms 主循环轮询。钟表页现使用
  独立 10ms（100Hz）触控周期，表盘仍只按需及每秒绘制，不额外刷新 AMOLED。
- 100Hz 复测发现右圆屏边缘的一次有效手势仅由 FT3168 上报 12px 横移，因此增加左右 64px
  边缘区的“向屏幕内移动 10px”轻扫规则；中部仍保持 42px 且横向明显主导，避免轻点误切。
  最终实测中右边缘 21–23px 短滑及左右普通滑动均成功；剩余未切换样本仅有 0–3px 位移，
  没有可判断方向的信息，按轻点处理。专项采样、手势、切换画质、渲染竞态、NVS 往返、图层隔离
  和屏幕安全共 7 项回归通过；临时轨迹日志已移除。
- 固定 20 次左右交替滑动复测推翻了“剩余样本只是轻点”的结论：固件只收到 9 个触摸段并切换
  5 次；成功切换后同步执行的 `nvs_commit()` 造成约 0.59–0.66 秒主循环停顿，会吞掉紧随其后的
  手势。表盘现在先立即切换和重绘，停止触摸 2 秒后再把一轮连续变化合并成一次 NVS 写入；
  7 项表盘回归和固件构建通过，等待 COM11 恢复后烧录实机复测。
- 延迟保存后继续实测发现 FT3168 坐标流本身仍会漏掉整段快速滑动：20 次操作只上报 12–15 个
  接触段；把活动扫描周期从 `0x08` 调到合法最小值 `0x04`、把本机触摸阈值从 `0x0A` 试降到
  `0x08` 均无实质改善，已恢复控制器原参数。转而启用 FT3168 内置特殊手势引擎（`D1=0x03`、
  `D0=0x01`），首轮 20 次左右交替滑动得到完整交替的 `0x20/0x21`，达到 20/20。
- 并行兼容探针确认特殊手势模式下 `TD_STATUS` 和 P1 坐标仍完整可读：左上点击为 `(92,47)`，
  左滑为 `(407,260)→(75,260)`，右滑为 `(130,224)→(360,253)`。正式实现因此在钟表页使用
  硬件 `0x20/0x21` 切换表盘，坐标仅保留左上返回、闹钟停止和息屏首次触摸唤醒；离开钟表页
  立即写 `D0=0` 恢复普通触摸，硬件配置失败时仍保留原坐标滑动作为回退。最终正式固件实测
  连续 10 次全部成功，串口逐次记录切换，左上返回也成功；7 项回归、完整构建与烧录均通过，
  临时手势/坐标日志和寄存器解压目录已清理。
- 继续节能计划阶段 2：`display_update_idle()` 对 USB Disk 强制使用 25 秒休眠，电池供电时将
  用户关闭/延长的自动熄屏限制为最多 5 分钟；专用 TinyUSB MSC 循环也运行显示休眠状态机，
  唤醒后仅重绘一次 USB 页面，第一次触摸不会误执行 OFF。时钟每 90 秒整体平移 2 像素，
  OuO 的待机快速路径与动态表情共用每 90 秒 1 像素漂移。新增
  `check_static_display_protection.ps1`，更新两个抽取式测试夹具；全量回归与 ESP-IDF 构建通过，
  待烧录完成 USB/时钟/OuO 真机验收及物理电流测量。

### 2026-09-06：阶段 6 第一部分与 Recorder 离线中文语音控制

- 阶段 6 第一部分加入按活动状态变化的帧率：OuO 动态交互保持高帧率、空闲时降低刷新；
  Fluid 在触摸或明显运动时提升帧率，静止时目标约 20 FPS。Fluid 粒子和仿真工作数组迁移到
  PSRAM，避免与 Wi-Fi、BLE 和语音模型争用内部 RAM。`check_adaptive_frame_rate.ps1`、全量回归
  和固件构建通过；0u0 与 Fluid 均已通过真机测试，Fluid 倾斜、触摸和静置降帧期间无拖影、
  明显卡顿或其它异常。
- 新增 Recorder 页面：I2S 麦克风实时波形、点击开始/停止、最长 60 秒录音，以及按编号写入
  NAND 的 `REC_NNN.WAV`；临时文件完成后原子改名，避免中断留下伪装成完整 WAV 的文件。
- 初版 `Hi ESP` WakeNet 在命中后调用 `clean()` 导致 `dl_convq_queue_bzero` 中
  `LoadProhibited` 并自动重启；删除该不安全清理调用后，真机命中可进入录音且不再复位。
- 根据实测改用 ESP-SR MultiNet6 中文模型，模型分区扩大到 4500 KiB，命令 `wo cao` 对应
  “卧槽”。同一句命令按 Recorder 当前状态切换：未录音时开始，录音中再次命中则停止保存；
  不再注册识别不稳定的 `ei you`。COM11 串口确认两次命中并在 2128 ms 后原子保存
  `REC_006.WAV`（68096 字节），用户真机确认功能正常且无重启。
- 为兼顾 ESP-SR 与 Wi-Fi，允许部分 BSS 放入 PSRAM，并将可安全执行的 heap/ringbuffer 函数
  放回 Flash。最终应用约 5.38 MiB、中文模型约 3.53 MiB；启动检查确认 Wi-Fi 的 4 个静态 RX
  缓冲成功分配，Tickless Idle 保持启用，未出现 `ESP_ERR_NO_MEM`。Recorder 专项检查和完整
  `pacon.ps1 test` 均通过。
