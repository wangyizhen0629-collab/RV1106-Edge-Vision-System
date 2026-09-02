# SC3336 / ISP 阶段 0：主机侧现状冻结与证据缺口

日期：2026-08-26
范围：只读核对仓库、构建产物、运行 FDT、板端日志和用户提供的原理图截图；未修改应用、Kernel、DTS、IQ 或模型。
状态：主机侧与板端动态证据已完成；摄像头模组供电及 RESET/PWDN 物理连接页待补充。

## 1. Git 与构建上下文

- 当前分支：`feat/led-dts`
- 当前 HEAD：`03ca9f895cbb2e54724a3ff1433b4ab078cad94b`
- 生效 BoardConfig：`project/cfg/BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1106_Echo_Mate-DeskMate.mk`
- 当前工作区已有未提交改动，并与本阶段的 DTS、BoardConfig、Kernel 配置和文档范围重叠；后续不得清理或覆盖。
- 2026-08-26 的 `git diff --check` 无输出。

当前已跟踪改动：

```text
M  .gitignore
M  SDK/rv1106-sdk/project/build.sh
M  SDK/rv1106-sdk/project/cfg/BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1106_Echo_Mate-DeskMate.mk
M  SDK/rv1106-sdk/sysdrv/Makefile
M  SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi
M  SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig
M  SDK/rv1106-sdk/sysdrv/tools/board/buildroot/echo_mate_defconfig
D  SDK/rv1106-sdk/sysdrv/tools/board/echo_defconfig/S99start_echo_defconfig
M  docs/PROGRESS.md
```

另有未跟踪的项目说明、简历文档、DeskBot 固件集成目录和启动脚本。本记录不判定这些改动的归属与完成状态。

## 2. 可由源码确认的 SC3336 板级配置

当前 Echo-Mate DTSI 描述：

- I2C controller：I2C4，`clock-frequency = <400000>`
- sensor 地址：`0x30`
- `compatible = "smartsens,sc3336"`
- 外部时钟：`MCLK_REF_MIPI0`，consumer name 为 `xvclk`
- 时钟 pinctrl：`mipi_refclk_out0`
- PWDN：`GPIO3_C5`，`GPIO_ACTIVE_HIGH`
- MIPI CSI-2：2 lane，`data-lanes = <1 2>`
- media graph：SC3336 → CSI2 DPHY0 → MIPI CSI2 → RKCIF → SDITF → RKISP
- 未配置 `reset-gpios`
- 未配置 `avdd-supply`、`dovdd-supply`、`dvdd-supply`

### 2.1 用户提供的原理图截图

本轮用户提供了 Echo-Mate V1.0 原理图的 Main（第 3/10 页）、Power（第 4/10 页）、GPIO-assignment（第 2/10 页）和 WiFi（第 9/10 页）截图。截图不是仓库文件，以下结论以本次会话附件为证据来源。

| 摄像头功能 | 原理图网络 | RV1106 管脚 | 当前 DTS/pinctrl | 核对结果 |
| --- | --- | --- | --- | --- |
| I2C SCL | `MIPI_IIC_SCL` | GPIO3_C7 | `i2c4m2_xfer`：GPIO3_C7 mux 3 | 管脚一致 |
| I2C SDA | `MIPI_IIC_SDA` | GPIO3_D0 | `i2c4m2_xfer`：GPIO3_D0 mux 3 | 管脚一致 |
| MCLK | `MIPI_CLK0` | GPIO3_C4 | `mipi_refclk_out0`：GPIO3_C4 mux 2 | 管脚一致 |
| 控制线 | `MIPI_RST` | GPIO3_C5 | SC3336 节点写成 `pwdn-gpios`：GPIO3_C5 active high | 物理管脚一致，信号语义待确认 |
| CSI-2 clock | `MIPI_CLKN/P` | GPIO3_C0/C1 | MIPI CSI-2 endpoint | 已见差分时钟对 |
| CSI-2 data lane 0 | `MIPI_D0N/P` | GPIO3_C2/C3 | `data-lanes = <1 2>` | 已见第一组数据差分对 |
| CSI-2 data lane 1 | `MIPI_D1N/P` | GPIO3_B6/B7 | `data-lanes = <1 2>` | 已见第二组数据差分对 |

上述证据已能支持“板级 I2C、MCLK 和双 lane MIPI CSI-2 管脚连接与当前 pinctrl 选择相符”。但 `MIPI_RST` 存在需要闭环的语义差异：

- Echo-Mate 原理图把 GPIO3_C5 命名为 `MIPI_RST`。
- 当前 Echo-Mate SC3336 DTS 把同一根线声明为 `pwdn-gpios`，且没有 `reset-gpios`。
- SDK 的 `rv1106-evb-cam.dtsi` 参考设计对 SC3336 使用 GPIO3_C5 作为 active-high `reset-gpios`，另用 GPIO3_D2 作为 active-high `pwdn-gpios`。

因此，现阶段不能仅凭网络名或 DTS 属性断言 GPIO3_C5 的真实控制功能。需要继续查看摄像头连接器/模组页，确认该网络最终接到 SC3336 的 RESETB 还是 PWDN 管脚及其有效电平；随后再用板端 probe 和反复启停流验证时序。

Power 页确认了系统侧存在由 `+5V_SYS` 生成的 VCC_3V3、VCC_1V8、VDD_0V9 和 VCC_DDR 等电源，但截图没有显示摄像头连接器、模组内部 LDO 或 SC3336 的 AVDD/DOVDD/DVDD 连接。仓库内也没有找到原理图、PCB 或 BOM 文件。因此仍不能确认 sensor 三路电压、是否常供电、是否由 regulator 控制，也不能把“供电适配”写成已验证成果。

## 3. Sensor driver 的模式、controls 与时序

SDK 既有 `sc3336.c` 支持两个线性 RAW10 mode：

| 分辨率 | 帧率 | xvclk | link frequency | VTS default | HTS default |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2304×1296 | 25 FPS | 27 MHz | 253.125 MHz | `0x0654` | `0x05dc` |
| 2304×1296 | 30 FPS | 24 MHz | 255 MHz | `0x0550` | `0x0578 * 2` |

其他静态事实：

- chip ID：寄存器 `0x3107` 读取 `0xcc41`
- `V4L2_CID_VBLANK` 写入寄存器 `0x320e/0x320f`
- `VTS = active height + VBLANK`
- exposure 上限随 VBLANK 更新为 `VTS - 8`
- exposure 寄存器：`0x3e00/0x3e01/0x3e02`
- gain 寄存器：`0x3e06/0x3e07/0x3e09`
- driver 申请 `avdd`、`dovdd`、`dvdd` 三路 regulator
- power-on 顺序为 pinctrl → xvclk → reset low → regulator enable → reset high → PWDN high → 等待首个 SCCB 事务

待板端重点验证：`__sc3336_power_off()` 的非 thunderboot 路径中存在两次 `clk_disable_unprepare(sc3336->xvclk)`。目前只将其记录为静态审查疑点，需通过 runtime PM/反复 stream 和 dmesg 观察确认，不能直接写成已复现故障。

## 4. 编译产物与不可变输入哈希

### 4.1 DTB 与镜像

三个构建阶段的 `rv1106g-echo-mate.dtb` 内容一致：

```text
af779eae0cb77f58120ec56fff91e2719412a2e1fc2e0db90f9c920a94150b1b
```

反编译/字符串检查只发现 `sc3336@30`，未发现 SC4336 或 SC530AI；存在 `data-lanes = <1 2>`、RKCIF 和 RKISP 节点。该结论只证明主机构建产物，不证明板上当前运行的是这份 DTB。

2026-08-26 镜像冻结值：

| 产物 | SHA-256 |
| --- | --- |
| `boot.img` | `7733db2288e4cdd1ca243a3362b4bf35e6d18666d6ad341244c8e1580eb8dcbd` |
| `rootfs.img` | `c4388eed7ef58128c10755e2f39026170fd7689989e6aa9734534d09775a622d` |
| `oem.img` | `7721ed18b7a92a88a81e56cc25e2a5bb7bd3403307bb108710c9bec1372988ae` |
| `update.img` | `d6a274d0df4d584524a37d6eb90044e34590a940836ec9010cd621c84f4b4c5e` |

### 4.2 IQ、模型与应用

| 对象 | SHA-256 | 结论 |
| --- | --- | --- |
| SC3336 源 IQ JSON | `ab116bdb3aa283749b0ea0245213f1f5d63c39783ebb05d0ff5cfbcb62d85ab2` | 与 OEM 内副本一致 |
| OEM SC3336 IQ JSON | `ab116bdb3aa283749b0ea0245213f1f5d63c39783ebb05d0ff5cfbcb62d85ab2` | 424970 bytes |
| YOLOv5 源 RKNN | `7e72c8caa060ea15d69109e4af6c1f80005cbd1be14bbe1f6f2af9175861c44a` | 与 OEM 内副本一致 |
| OEM YOLOv5 RKNN | `7e72c8caa060ea15d69109e4af6c1f80005cbd1be14bbe1f6f2af9175861c44a` | 7589751 bytes |
| OEM DeskBot `main` | `703d03660001643879ca092e2cff191c65f0dae0f84bba69838b3d67e551affa` | ARM EABI5/uClibc，5092280 bytes |

当前 IQ JSON 的 `normal/day/scene_isp32` 包含 AE、WB、BLC、CCM、Gamma、DRC、Bayer2DNR v23、BayerTNR v23、YNR v22、CNR v30、Sharp v33 等模块。其 AE 当前配置为 low-light strategy、50 Hz anti-flicker；`AecEnvLvCalib.Enable = 0`，因此不能用 RKAIQ EnvLv 代替外部 lux 计量。

## 5. 静态运行链分析

已确认：

- DeskBot Camera 代码使用 `cap.open(0)`，静态上对应 `/dev/video0`。
- 旧板端日志把 `/dev/video0` 标识为 RKCIF 的 2304×1296 `BG10` RAW 节点。
- 旧板端日志把 RKISP mainpath 标识为 `/dev/video11`。
- 旧 media graph 已出现 `m00_b_sc3336 4-0030`、2304×1296 SBGGR10、25 FPS 和 enabled link。
- 新 OEM 启动脚本只加载 RKCIF、RKISP、SC3336、RKNPU module，然后启动 DeskBot。
- 新 DeskBot ELF 的直接动态依赖中没有 `librkaiq`。
- `S21appinit` 与 `RkLunch.sh` 中未发现启动 `rkisp_demo`、`rkaiq_3A_server` 或 `simple_vi_rkaiq`。
- 旧 `top` 日志中未检索到 RKAIQ/rkisp_demo/rkipc 进程。

第一轮 SSH 采集已经发现 DeskBot 运行时映射了 `librkaiq.so`，说明它通过间接依赖或运行时加载进入了进程；这修正了单看 ELF 直接依赖得出的初步判断。但“库已加载”仍不等于“RKAIQ context 已成功启动并加载指定 SC3336 IQ”，也不等于 YOLO 输入一定来自 RKISP mainpath，仍需结合进程 fd、应用日志和运行中的 controls 确认。

## 6. 板端第一轮 SSH 证据

虚拟机没有 RV1106、USB 串口或 RNDIS 直连设备；用户通过 Windows 主机上的 Xshell 登录开发板，并完成了第一轮只读采集。

### 6.1 系统与 sensor 绑定

- 板型：`Echo Mate`
- Kernel：Linux 5.10.110，ARMv7，构建时间为 2026-07-17
- 根文件系统：SD 卡 `mmcblk1p7`，ext4
- 板端 `date` 显示 2021-01-01，与真实采集日期不一致；正式测试必须校时或以 Windows/Xshell 日志时间为准
- `/sys/bus/i2c/devices/4-0030/name` 为 `sc3336`，说明 I2C4 地址 0x30 已绑定 SC3336 driver
- 本次筛选后的 `dmesg` 无输出，尚未保存明确的 chip ID/probe 日志

### 6.2 实际 media graph 与格式

两张 media device 均已注册，链路为：

```text
SC3336 /dev/v4l-subdev2
  2304x1296 SBGGR10 @ 25 FPS
→ CSI2 DPHY0 /dev/v4l-subdev1
→ MIPI CSI2 /dev/v4l-subdev0
→ RKCIF /dev/video0
  2304x1296 BG10 RAW
→ RKISP /dev/v4l-subdev3
→ mainpath /dev/video11
  864x480 NV21
```

所有关键 media link 均为 enabled。由此可确认 sensor、MIPI CSI-2、RKCIF 和 RKISP 的拓扑已经建立，当前 sensor mode 为 2304x1296 RAW10、25 FPS。根据 driver mode table，该模式应对应 27 MHz xvclk，但在读取 runtime clock 前仍标记为“静态推断”。

### 6.3 DeskBot 与 RKAIQ

- DeskBot 进程为 PID 563，路径 `/oem/usr/share/deskbot/main`
- 未发现独立的 `rkipc`、`rkisp_demo`、`rkaiq_3A_server` 或 `simple_vi` 进程
- `/proc/563/maps` 显示进程已加载 `/oem/usr/lib/librkaiq.so` 和 `librknnmrt.so`
- 在 YOLO 页面连续 5 秒采样时，进程持续持有 `/dev/video11`、SC3336 `/dev/v4l-subdev2`、RKISP `/dev/v4l-subdev3`、statistics `/dev/video19`、input-params `/dev/video20`，以及 RKCIF/RAW read 相关节点
- 应用日志明确输出 `devpath = /dev/video11`、`driver = rkisp_v7`、`card = rkisp_mainpath`，并持续输出 YOLO 检测结果
- 主机侧二进制字符串显示内置 opencv-mobile camera backend 会动态加载 `librkaiq.so`，调用 `rk_aiq_uapi2_sysctl_init/prepare/start`，IQ 目录固定为 `/oem/usr/share/iqfiles`

以上证据共同确认：**DeskBot 的 YOLO 输入使用 RKISP mainpath `/dev/video11` 的 864x480 NV21 帧；RKAIQ 在 DeskBot 进程内运行，并通过 RKISP statistics/input-params 节点执行 3A/IQ 控制。** `/dev/video0` 的 2304x1296 BG10 RAW 节点虽然也被 camera backend 打开，但不是日志声明的 YOLO 帧输出节点。

### 6.4 Sensor controls 与固定输入

YOLO 页面运行时的 SC3336 control 快照：

| Control | 当前值 | 约束/含义 |
| --- | ---: | --- |
| exposure | 405 | min 1，max 1612，default 128 |
| analogue gain | 428 | min 128，max 99614，default 128 |
| vertical blanking | 324 | min/default 324 |
| VTS | 1620 | `1296 + 324`，exposure max 为 `VTS - 8` |
| link frequency | 253.125 MHz | menu index 0，read-only |
| pixel rate | 101.25 MHz | read-only |
| test pattern | disabled | 正常成像输入 |

link frequency、pixel rate、VBlank/VTS 与 driver 的 2304x1296@25 FPS mode 一致。当前 exposure/gain 只是单帧时刻的 AE control 快照，不是低照度基线；正式基线需在固定 lux、固定场景下连续采样。

另有一个静态/动态一致的 metadata 疑点：`horizontal_blanking` 在板端显示为 `-804`（控制范围的无符号值为 4294966492）。25 FPS mode 的 `hts_def = 0x05dc = 1500` 小于 width 2304，而 driver 直接计算 `hts_def - width`，因此发生无符号下溢。现阶段不修改 driver，但后续计算行周期或曝光时间时不能直接使用这个 HBLANK control，需先结合 SC3336 datasheet/寄存器单位确认 HTS 定义。

板端三个冻结对象与主机产物 SHA-256 完全一致：

| 对象 | 板端 SHA-256 |
| --- | --- |
| SC3336 IQ JSON | `ab116bdb3aa283749b0ea0245213f1f5d63c39783ebb05d0ff5cfbcb62d85ab2` |
| YOLOv5 RKNN | `7e72c8caa060ea15d69109e4af6c1f80005cbd1be14bbe1f6f2af9175861c44a` |
| DeskBot `main` | `703d03660001643879ca092e2cff191c65f0dae0f84bba69838b3d67e551affa` |

### 6.5 Runtime MCLK 与 regulator

临时挂载 debugfs 并保持 YOLO 页面运行时确认：

- `clk_ref_mipi0` 与 `mclk_ref_mipi0` 的 prepare/enable count 均为 1，实际频率均为 27 MHz
- MIPI1 reference clock 为 24 MHz，但 prepare/enable count 为 0，与本摄像头无关
- `pclk_mipicsiphy` 已启用，频率为 148.5 MHz
- SC3336 的 `4-0030-avdd`、`4-0030-dovdd`、`4-0030-dvdd` 全部挂在 `regulator-dummy` 下，电压显示为 0 mV

这确认了当前 2304x1296@25 FPS mode 的实际 xvclk 为 27 MHz，也确认 Kernel/DTS 没有建模或控制 sensor 的三路真实供电。物理硬件显然存在可工作的供电来源，但它是板级常供电、模组内部 LDO 还是其他连接，仍需摄像头连接器/模组原理图确认。因此简历中的“供电适配”暂时只能描述为供电拓扑与时序核对，不能写成三路 regulator 控制已完成。

debugfs 采集后已正常卸载，没有持久化系统改动。

### 6.6 尚未取得的信息

运行 DT 的 SC3336 节点位于 `/sys/firmware/devicetree/base/i2c@ff470000/sc3336@30`，关键属性为：

- 存在 `pwdn-gpios`，12 bytes 三个 cell 为 phandle `0x2c`、GPIO offset `0x15`、flags `0`
- GPIO offset `0x15 = 21`，对应 GPIO3_C5；flags 0 对应 active high
- 不存在 `reset-gpios`
- endpoint 的 `data-lanes` 原始值为 `00 00 00 01 00 00 00 02`，即 `<1 2>`
- GPIO debug 显示全局 gpio-117（GPIO3 base 96 + offset 21）consumer 为 `pwdn`，当前方向/电平为 `in hi`

这确认运行 DT 与当前源码在 SC3336 的 PWDN、reset 缺失和双 lane 配置上相符，同时确认原理图 `MIPI_RST` 在软件中确实被解释为 PWDN。需要注意：driver 用 `GPIOD_ASIS` 申请该线，power-on/off 只调用 `gpiod_set_value_cansleep()`，没有显式设置输出方向；板端也显示为 input high。因此当前只能确认 consumer 占用和高电平状态，不能声称 Linux 已主动完成可靠的 RESET/PWDN 时序控制。

虽然筛选后的 dmesg 没有打印 chip ID，但 driver probe 在 `sc3336_check_sensor_id()` 成功后才注册 media sensor subdev。当前 I2C driver 已绑定且 sensor subdev 已注册并稳定出流，因此可确认 probe 中的 chip ID 检查已通过。

运行 `/sys/firmware/fdt` 大小为 36864 bytes、SHA-256 为 `7906e67f88a4cae48e0f723ef590a49628a088ba55c83a8b6c7d2ccd966aadad`；主机构建 DTB 大小为 36081 bytes、SHA-256 为 `af779eae0cb77f58120ec56fff91e2719412a2e1fc2e0db90f9c920a94150b1b`。

导出并反编译两份 DTB 后，结构化 diff 只有以下 bootloader 运行时修正：

- 新增 memory reserve 区域
- 注入芯片 serial number
- 注入实际 memory 节点
- 注入 Ethernet MAC 地址
- 扩充 `/chosen/bootargs` 的存储介质与分区参数

SC3336、I2C4、PWDN、MCLK、MIPI CSI-2、RKCIF 和 RKISP 相关节点没有差异。因此可确认板上运行的摄像头相关 DT 配置与本次主机构建产物对应；整文件大小和哈希差异来自 bootloader fixup，而不是摄像头 DTB 版本不一致。应用日志多次报告 NTP/DNS 失败，板端时间仍不可信。

## 7. 阶段 0 当前结论

软件链已经完成闭环取证：I2C probe、27 MHz MCLK、双 lane CSI-2、2304x1296 RAW10@25 FPS、RKCIF→RKISP、RKAIQ 3A/IQ、864x480 NV21 检测输入，以及 IQ/模型/应用版本均已确认。

剩余问题属于硬件连接证据而不是软件链路未知：

1. AVDD/DOVDD/DVDD 在 Kernel 中均为 dummy regulator，需原理图确认真实电压与供电来源。
2. 原理图将 GPIO3_C5 命名为 `MIPI_RST`，运行 DT/driver 将其当作 PWDN。
3. GPIO3_C5 运行状态为 input high，当前 driver 没有显式切换输出方向，不能把 RESET/PWDN 时序描述为已可靠控制。
4. 正式低照度测试前需解决板端时间基准，并固定 lux 测量、场景、帧集、模型哈希、置信度/NMS 阈值和漏检定义。

## 8. 阶段 0 完成条件

- [x] 通过原理图截图确认 I2C、MCLK 与双 lane MIPI CSI-2 的 SoC 管脚连接
- [ ] 获得摄像头连接器/模组页，确认 AVDD/DOVDD/DVDD、GPIO3_C5 对应 RESETB/PWDN 及有效电平
- [x] 结构化比较运行 FDT 与主机构建 DTB，确认摄像头节点一致，差异仅为 bootloader fixup
- [x] 保存 SC3336 media graph，确认 2304x1296 RAW10、25 FPS 和完整 enabled link
- [x] 保存 SC3336 subdev controls，并确认 VTS、link frequency 和 pixel rate
- [x] 由 driver probe 控制流、绑定状态和稳定出流确认 SC3336 chip ID 检查通过
- [x] 确认实际 xvclk 为 27 MHz，对应 2304x1296@25 FPS mode
- [x] 确认 Kernel 侧 AVDD/DOVDD/DVDD 全部使用 dummy regulator；物理供电仍待原理图确认
- [x] 确认板端 IQ、模型和 DeskBot 程序与主机冻结哈希一致
- [x] 通过 `/proc/<pid>/fd` 和应用日志确认 DeskBot 使用 `/dev/video11`
- [x] 通过 maps、statistics/input-params fd 和调用入口确认 RKAIQ 控制 ISP
- [x] 确定后续基线使用 RKISP mainpath 的 864x480 NV21 检测输入

用户已决定进入阶段 1。物理供电与 RESET/PWDN 缺口作为已知风险保留：阶段 1 可开展低照度基线协议和评测采集，但在硬件连接闭环前不修改 sensor power/reset 逻辑，也不把对应能力写成已完成成果。
