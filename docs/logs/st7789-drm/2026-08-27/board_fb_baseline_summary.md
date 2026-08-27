# ST7789V FB 板端基线汇总

- 归档日期：2026-08-27（主机时间；板端 RTC 为 2021-01-01，不可信）
- 场景：`home` 60 个样本、`yolo` 120 个样本
- 运行内核：Linux 5.10.110，armv7l，单核
- DeskBot PID：两组均为 540，采样期间未重启
- 数据状态：量化数据、运行时链路和驱动迁移参数已分析；用户已在采集后确认人工视觉结果

## 同源性

两组采集的关键文件 SHA-256 一致：

| 文件 | SHA-256 |
| --- | --- |
| `/oem/usr/share/deskbot/main` | `fc9d16fd1b33e22cb535b5fa5ac24defe379619fd06c722c2a1516e65495c80a` |
| `model/yolov5.rknn` | `7e72c8caa060ea15d69109e4af6c1f80005cbd1be14bbe1f6f2af9175861c44a` |
| `libdrm.so` / `libdrm.so.2` | `71c7035fc061c43e6e51e10bd1d495424bfbbad3873b66f72b98a1816224886b` |

因此 Home 与 YOLO 可以作为同一固件、同一应用二进制下的场景基线。该数据不是 OpenCV 与 VI/VPSS 的 A/B；两组都来自当前 RKMPI 应用。

## 显示硬件与驱动链

```text
LVGL RGB565
  → mmap /dev/fb0
  → fb_st7789v
  → SPI0 CS0 @ 60 MHz
  → ST7789V, rotation 270
  → 320×240 LCD
```

- `/dev/fb0`：`fb_st7789v`、320×240、16 bpp、stride 640、mode `U:320x240p-0`。
- `/dev/dri` 不存在，说明当前板端没有可供 LVGL 使用的 DRM/KMS 节点。
- `spi0.0` 的 driver link 指向 `fb_st7789v`，OF full name 为 `/spi@ff500000/fbtft@0`，compatible 为 `sitronix,st7789v`。
- 运行时 DT 属性：buswidth=8、fps=60、debug=7、rotation=270、60 MHz、DC=`GPIO1_D0`、reset=`GPIO1_C4` active-low。
- 运行时 SPI 枚举没有 SPI0 spidev；`spi2.0` 是 SPI NAND，与屏幕无关。

## 量化结果

汇总命令：

```bash
python3 docs/tools/summarize_fb_display_baseline.py \
  docs/logs/st7789-drm/2026-08-27/home \
  docs/logs/st7789-drm/2026-08-27/yolo
```

| 指标 | Home | YOLO |
| --- | ---: | ---: |
| `elapsed_s` 覆盖 | 59 s | 119 s |
| 首末 uptime 跨度 | 62.01 s | 129.78 s |
| DeskBot CPU 平均 / p95 / 最大 | 3.00% / 4.00% / 4.04% | 35.71% / 37.76% / 39.22% |
| 系统 busy 平均 / p95 / 最大 | 7.64% / 10.78% / 11.76% | 51.19% / 53.40% / 55.88% |
| RSS 平均 / 最大 | 7300 / 7300 KiB | 13691 / 13828 KiB |
| RSS 首 / 尾 | 7300 / 7300 KiB | 13828 / 13684 KiB |
| VmHWM | 10952 KiB | 14360 KiB |
| fbtft 吞吐样本数 | 3 | 3 |
| fbtft 吞吐均值 | 4319 KiB/s | 3821 KiB/s |
| 等效全屏传输能力 | 28.80 次/s | 25.47 次/s |
| fbtft 日志中的实际 FPS | 0 | 8、8、11 |
| SPI0 IRQ 增量 / 约每秒 | 65 / 1.05 | 5825 / 44.88 |

CPU 百分比按相邻样本间的进程 jiffies 除以全系统 jiffies 计算。在单核 RV1106 上，YOLO 使 DeskBot CPU 均值相对 Home 增加约 32.7 个百分点，系统 busy 增加约 43.5 个百分点；这代表整个 YOLO 场景（VI/VPSS、RKNN、后处理、LVGL 与 FB）的场景差异，不能归因于显示后端单一组件。

YOLO RSS 比 Home 均值高约 6391 KiB。YOLO 采样中 RSS 从 13828 KiB 降到 13684 KiB，在本次约 2 分钟窗口内没有持续增长迹象；这不替代 2 小时或 8 小时长稳测试。VmHWM 是进程启动以来峰值，只用于记录上界。

## 应用发布与屏幕刷新口径

YOLO 日志最后一个累计点：

```text
uptime_s=138.897 frames=1200 published=1230 fps=8.889
e2e_p95_ms=193 infer_p95_ms=94 queue_p95_ms=76
queue_drop=2227 timeouts=0 recoveries=0 failures=0
media_pts=1200 software_start=0
```

该 `8.889 FPS` 是完成推理、绘图和 RGB565 front buffer 发布的应用吞吐。fbtft 在采集尾部报告 8～11 FPS，与应用发布率量级一致。

`3821 KiB/s ÷ 153600 bytes` 得到的 25.47 次/s 仅是一次传输发生时的 payload 吞吐折算值，并不意味着 LCD 每秒呈现 25.47 个不同画面。Home 同理：传输吞吐可以折算为 28.80 次/s，但静态首页的 fbtft 日志实际 FPS 为 0。

## 数据质量与证据限制

- DTS 设置 `debug=7`，dmesg 含大量 fbtft/SPI 调试输出。每个场景只能从 ring buffer 尾部得到 3 条完整吞吐记录；这些数据受打印行为干扰，仅作数量级基线。
- 采集期间 dmesg delta 除 fbtft debug 外未发现新增错误；但高量调试输出可能挤掉更早日志。
- 当前固件未提供 `/proc/config.gz` 或其他内核 config；debugfs 未挂载或未启用，因此 GPIO/pinctrl 和 clock summary 无法采集。
- 两个采集目录中的 `operator_notes.txt` 是采集时的原始产物，字段仍为 `TODO`，未做事后回写。用户已于 2026-08-27 在采集后确认：颜色和方向正常、无偏移、无明显撕裂、闪烁或花屏；该人工证据记录在本汇总及 `fb_driver_migration_baseline.md`。
- 端到端 p95 的终点是应用 RGB565 front buffer 发布，不包含 LVGL 调度、fbdev deferred I/O、SPI 传输和面板光学响应。DRM 阶段需要新增 submit/complete 或物理高速相机口径。

初始化寄存器、reset/DC 时序、SPI、rotation、RGB/BGR、分辨率、地址窗口与 PWM 背光的完整迁移基准见 `fb_driver_migration_baseline.md`。

## DRM A/B 必须保持的条件

- 同一 DeskBot 二进制功能版本、模型、Home/YOLO 场景、采样时长与 CPU/RSS 算法。
- 记录 `/dev/dri`、connector/mode、像素格式、rotation、SPI 频率以及 display submit/complete 统计。
- 同时报告应用发布 FPS、KMS commit/flush FPS 和等效 SPI 吞吐，禁止混用三种口径。
- 关闭或严格控制 per-transfer debug，再做正式 CPU 和吞吐对比；否则调试输出会污染结果。
- 人工复查颜色、方向、偏移、撕裂、闪烁和瞬时花屏。
