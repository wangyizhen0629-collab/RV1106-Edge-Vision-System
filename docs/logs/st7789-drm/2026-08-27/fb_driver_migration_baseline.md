# ST7789V FB→DRM 驱动迁移基准

- 记录日期：2026-08-27
- 当前链路：LVGL RGB565 → `/dev/fb0` → `fb_st7789v` → SPI0 CS0 → ST7789V
- 用途：TinyDRM 首次点屏必须先复现本文件中的电气时序、控制器状态和最终视觉效果，再逐项优化。
- 证据口径：`源码` 表示当前 BSP 的实际实现；`运行时` 表示板端 DT、sysfs 或 dmesg；`人工` 表示用户实物观察。

## 迁移基准总表

| 项目 | 当前 FB 基准 | 证据与迁移要求 |
| --- | --- | --- |
| 控制器初始化 | 硬复位后依次写 `11`、`3A 05`、电源/porch 参数、`29`；随后写 `36 60`、清屏和两组 gamma | 源码确认；完整字节序列见下表。首次 DRM 点屏保持一致，不自行替换为其他模组的初始化表 |
| reset | `GPIO1_C4`，物理低有效；低电平保持 20～40 µs，拉高后等待 120 ms | DTS＋源码确认。新 descriptor API 会解释 active-low，迁移时使用逻辑“assert=1、deassert=0”，不能照抄旧代码的数值 `0→1` |
| DC | `GPIO1_D0`，active-high；命令为物理低，参数及像素为物理高 | DTS＋源码确认；DRM/mipi-dbi 必须保持 `command=0`、`data=1` |
| SPI | SPI0、CS0、8-bit，`spi-max-frequency=60 MHz`；mode 0、CS 低有效、MSB first | 60 MHz/CS0/8-bit 由运行时 DT 确认；其余由未设置 `spi-cpol`、`spi-cpha`、`spi-cs-high`、`spi-lsb-first` 推定。60 MHz 是配置上限，尚未用波形测得实际 SCLK |
| rotation | DTS `rotate=270`；控制器写 `MADCTL=0x60`（MV=1、MX=1、MY=0） | 源码＋运行时确认；人工确认最终方向正常 |
| RGB/BGR | framebuffer 为 RGB565：R[15:11]、G[10:5]、B[4:0]；`bgr` 未启用，MADCTL BGR bit 清零；SPI 上每像素高字节先发 | 源码＋DTS确认；人工确认红蓝未交换、颜色正常。DRM 转换后需用纯红/绿/蓝测试图复核 |
| 分辨率 | 控制器原生 240×320；rotation 270 后逻辑 framebuffer 为 320×240、16 bpp、stride 640、全帧 153600 bytes | 源码＋运行时确认；人工确认无偏移 |
| 地址窗口 | 全屏刷新写 `2A 00 00 01 3F`、`2B 00 00 00 EF`、`2C`，即 X=0..319、Y=0..239，无额外 offset | 板端 dmesg 直接确认；人工确认无偏移 |
| 反色 | 当前 `HSD20_IPS=0`，不发送 `21`（Display Inversion On） | 源码确认；首次 DRM 点屏不启用 inversion |
| 背光 | 独立 `pwm-backlight`，PWM9 channel 0，period 300000 ns（约 3.33 kHz），默认亮度索引 50；fbtft 的 `led-gpios` 未启用 | DTS 确认；迁移时保留 PWM 背光，不把注释中的 GPIO 背光当作现状 |

## 初始化命令与寄存器

当前 `fb_st7789v.c` 中 `HSD20_IPS` 为 0，所以下表只列实际生效分支。命令与参数均为十六进制字节；`3A` 的实际参数是源码宏展开后的 `05`，不是常见示例中的 `55`。

| 顺序 | 写入 | 含义/当前值 |
| ---: | --- | --- |
| 1 | hardware reset | 物理低 20～40 µs，物理高后等待 120 ms |
| 2 | `11` | Sleep Out |
| 3 | delay 120 ms | Sleep Out 稳定时间 |
| 4 | `3A 05` | COLMOD，驱动定义为 RGB565/16-bit |
| 5 | `B2 08 08 00 22 22` | PORCTRL |
| 6 | `B7 35` | GCTRL |
| 7 | `C2 01 FF` | VDV/VRH command enable |
| 8 | `C3 0B` | VRHS |
| 9 | `C4 20` | VDVS |
| 10 | `BB 20` | VCOMS |
| 11 | `C5 20` | VCOM offset |
| 12 | `D0 A4 A1` | PWCTRL1 |
| 13 | `29` | Display On |
| 14 | `36 60` | MADCTL：rotation 270，RGB order |
| 15 | `2A 00 00 01 3F` | 全屏 column window，X=0..319 |
| 16 | `2B 00 00 00 EF` | 全屏 page window，Y=0..239 |
| 17 | `2C`＋153600-byte zero frame | 首次全屏清屏 |
| 18 | `E0 D0 05 0A 09 08 05 2E 44 45 0F 17 16 2B 33` | Positive gamma |
| 19 | `E1 D0 05 0A 09 08 05 2E 43 45 0F 16 16 2B 33` | Negative gamma |

这里记录的是 fbtft 注册时的实际调用顺序：`init_display()` → `set_var()` → 首次全屏 update → `set_gamma()`。当前采集发生在系统运行数分钟后，高频 fbtft debug 已覆盖启动期 ring buffer，因此初始化表属于源码级确认；运行时日志直接确认了持续刷新使用的 `2A/2B/2C` 和 153600-byte 帧长度。

## reset 极性陷阱

当前 BSP 的 fbtft GPIO 获取代码只根据 `GPIO_ACTIVE_LOW` 选择初始输出低电平，却没有把 `GPIOF_ACTIVE_LOW` 标志传给 GPIO descriptor。因此旧驱动中的：

```text
gpiod_set_value(reset, 0)  → 物理 LOW，复位有效
等待 20～40 µs
gpiod_set_value(reset, 1)  → 物理 HIGH，退出复位
等待 120 ms
```

是按物理电平工作的。改用正常的 `devm_gpiod_get(..., "reset", ...)` 后，DTS 的 active-low 会被 descriptor 正确解释，此时应写成逻辑语义：

```text
gpiod_set_value(reset, 1)  → logical assert → 物理 LOW
等待 20～40 µs
gpiod_set_value(reset, 0)  → logical deassert → 物理 HIGH
等待 120 ms
```

迁移验收应优先检查物理波形或至少检查复位后的稳定点屏现象，避免只比较 C 代码中的 0/1。

## DC、像素格式与线上字节序

- `fbtft_write_reg8_bus8()` 把每条写入的第一个字节作为 command：DC=0；后续参数：DC=1。
- `fbtft_write_vmem16_bus8()` 在发送像素前令 DC=1。
- framebuffer 内存采用 Linux RGB565 bitfield：red offset 11/length 5，green offset 5/length 6，blue offset 0/length 5。
- RV1106 为 little-endian，fbtft 在 8-bit SPI 路径中对每个 16-bit pixel 执行 `cpu_to_be16()`，所以总线上先发 RGB565 高字节，再发低字节。
- 当前 DTS 没有 `bgr` 布尔属性，`MADCTL_BGR` 未置位，最终 `MADCTL=0x60`。

因此 DRM 迁移不能只写“RGB565”：必须同时保持 framebuffer/DRM format、MADCTL RGB/BGR bit 和 SPI wire byte order 三层一致。推荐固定显示纯红、纯绿、纯蓝、白、黑及四角标记图进行首次验收。

## 分辨率、旋转与刷新窗口

```text
ST7789V/fbtft native mode       240 × 320
DTS rotation                   270°
MADCTL                         0x60 (MV | MX)
DRM 目标逻辑 mode              320 × 240
RGB565 stride                  640 bytes
全帧                           153600 bytes
全屏地址窗口                   X=0..319, Y=0..239
panel offset                   0, 0
```

板端 fbtft 日志持续出现：

```text
fbtft_update_display(start_line=0, end_line=239)
fbtft_write_reg8_bus8: 2a 00 00 01 3f
fbtft_write_reg8_bus8: 2b 00 00 00 ef
fbtft_write_reg8_bus8: 2c
fbtft_write_vmem16_bus8(offset=0, len=153600)
```

这证明当前链路没有通过隐藏的 X/Y offset 修正画面。用户在实物上确认方向正常、无偏移。

## 人工视觉验收

用户于 2026-08-27 对当前 FB 基线确认：

- 颜色正常；
- 方向正常；
- 无偏移；
- 无明显撕裂、闪烁或花屏。

该结论是当前正常工作 FB 链路的验收基准。由于采集目录内 `operator_notes.txt` 是已完成采集的原始产物，未回写修改；本文件记录采集后的用户确认。

## TinyDRM 首次点屏等价检查表

- [ ] SPI0 CS0、8-bit、mode 0、CS active-low、MSB first，最大频率先保持 60 MHz。
- [ ] reset 物理低脉冲 20～40 µs，退出复位后等待 120 ms。
- [ ] DC 物理低为命令、物理高为参数和像素。
- [ ] 复用上面的初始化字节表，不引入其他 ST7789V 模组参数。
- [ ] 逻辑 mode 为 320×240，MADCTL 初始保持 `0x60`，offset 为 0/0。
- [ ] panel 接收 RGB565 高字节优先，MADCTL BGR bit 保持清零。
- [ ] 保留 PWM9 `pwm-backlight`；不要启用注释中的 `led-gpios`。
- [ ] 用纯色和四角标记图确认颜色、方向与边界，再运行 Home/YOLO。
- [ ] 重复确认无明显撕裂、闪烁、花屏，并记录 DRM commit/flush 与 SPI 完成口径。
- [ ] 关闭 per-transfer debug 后，再与 FB 基线比较 CPU、RSS、FPS 和吞吐。

## 尚未直接测得但不阻塞阶段一的项目

- 当前固件没有暴露 kernel config、GPIO debugfs 和 clock summary。
- 60 MHz 是 DT 的 SPI 频率上限，控制器实际 SCLK 及 reset/DC 波形尚未用逻辑分析仪测量。
- 启动期初始化命令没有保留在当前 dmesg ring buffer；字节表来自与运行固件匹配的当前 BSP 驱动源码。
- 没有 TE 信号证据；“无明显撕裂”是当前场景下的人工观察，不代表面板具备硬件同步。

这些内容应在 TinyDRM 驱动 probe/首轮实机验证时补采，但不影响将现有 FB 行为作为迁移基准。
