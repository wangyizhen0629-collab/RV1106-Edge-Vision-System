# ST7789V DRM 阶段一：FB 显示基线与硬件约束

## 目标与状态

阶段一只建立迁移前证据，不修改显示驱动、设备树或 DeskBot 显示后端。首页与 YOLO 的板端原始数据已经归档并完成量化分析，运行时硬件链路以及初始化寄存器、reset/DC 时序、SPI、rotation、RGB/BGR、分辨率和地址窗口均已固化。用户已确认颜色和方向正常、无偏移、无明显撕裂、闪烁或花屏，因此阶段状态为“完成”。运行内核 config、GPIO debugfs 与 clock summary 在当前固件上不可用，作为阶段二实机验证的已知证据限制保留，不阻塞阶段一收口。

需要回答四个问题：

1. 当前实际运行固件是否与仓库 DTS、defconfig 和 RootFS 配置一致。
2. ST7789V 的 SPI、CS、DC、reset、背光、旋转和 RGB/BGR 状态是什么。
3. 当前 FB 链路在首页与 YOLO 场景下的 CPU、RSS、刷新吞吐和视觉稳定性如何。
4. DRM 迁移后必须保持或改善的验收基线是什么。

## 已确认的主机侧事实

| 项目 | 当前结论 | 证据 |
| --- | --- | --- |
| 板端历史显示节点 | `/dev/fb0`，名称 `fb_st7789v`，320×240，16 bpp | `docs/logs/baseline/2026-07-08/board_display.txt` |
| DTS 显示总线 | SPI0 CS0，`spi-max-frequency = 60000000` | `rv1106-echo-mate-ipc.dtsi` |
| 控制信号 | DC=`GPIO1_D0`，reset=`GPIO1_C4` active-low | `rv1106-echo-mate-ipc.dtsi` |
| 方向与格式 | fbtft native 240×320，DTS rotation=270，最终 320×240 RGB565 | DTS 与 `fb_st7789v.c` |
| 当前驱动 | staging fbtft `fb_st7789v` | `CONFIG_FB_TFT_ST7789V=y` |
| DRM 基础能力 | 内核已启用 DRM；源码包含 atomic、GEM CMA、MIPI DBI 和 TinyDRM 框架 | kernel defconfig/source |
| ST7789V DRM 缺口 | `drivers/gpu/drm/tiny/` 没有 SPI pixel ST7789V 驱动 | tinyDRM Kconfig/Makefile |
| 现有 DRM panel 限制 | `panel-sitronix-st7789v` 是 SPI 控制＋DPI/RGB pixel bus，不适合当前 SPI pixel 链 | panel driver 的 `DRM_MODE_CONNECTOR_DPI` |
| 用户态依赖 | RootFS defconfig 已启用 `BR2_PACKAGE_LIBDRM=y` | `echo_mate_defconfig` |
| LVGL | RGB565；FBDEV 使用双全屏 buffer；DRM 路径已存在但关闭 | `lv_conf.h`、`dev_conf.h`、LVGL drivers |

### 已发现的迁移风险

- 公共 DTSI 同时声明 `spidev@0` 和 `fbtft@0`，顶层 `rv1106g-echo-mate.dts` 又显式将 `spidev@0` 设为 disabled。2026-08-26 生成 DTB 的反编译结果只保留了 `fbtft@0`；仍需用运行时 DT 和 sysfs 确认板端实际绑定者，避免后续 DRM 节点与 spidev 抢占 CS0。
- DTS 设置 `debug = <0x7>`。历史日志包含大量 4 KiB SPI 数据打印，会提高 klog/syslog CPU 和 I/O 开销，不能作为干净性能基线。
- 当前 reset 属性为 active-low，而 DRM `mipi_dbi` 使用 descriptor logical value 操作。迁移时必须以板端波形或至少 probe/复位现象确认极性，不能只机械复制数值。
- 当前 DTS 已确认独立 PWM9 `pwm-backlight`，但没有 TE 信号证据。DRM atomic 不等于物理无撕裂；若屏幕未引出 TE，只能测量并控制撕裂概率。
- 当前 YOLO “端到端延迟”终点是 RGB565 front buffer 发布，不是 LCD 完成刷新。DRM A/B 需要新增 display submit/complete 口径，不能直接复用 189 ms 作为实屏延迟。

## 带宽边界

320×240 RGB565 单帧大小：

```text
320 × 240 × 2 = 153600 bytes
```

60 MHz SPI 忽略命令、片选和软件开销时的理论上限：

```text
60,000,000 / 8 / 153600 = 48.83 full frames/s
理论最短全屏像素传输时间 = 20.48 ms
```

历史 dmesg 中三个 `Display update` 样本平均约 4505 KiB/s，对应约 30.03 次全屏传输/秒、约 33.3 ms/帧。该数据受 `debug=0x7` 干扰，仅作为链路量级参考，不作为最终验收值。

当前 YOLO 约 9.1 个不同画面/秒，仅像素 payload 约为：

```text
9.1 × 153600 ≈ 1.40 MB/s
```

因此在正常 60 MHz SPI 链路上，YOLO 发布率不应由 LCD payload 带宽限制；DRM 的主要收益应来自缓冲所有权、减少 FB/fbtft 复制与后续 damage 更新，而不是提高 RKNN FPS。

## 板端采集步骤

将脚本复制到板端：

```bash
scp docs/tools/collect_fb_display_baseline.sh root@<board-ip>:/tmp/
ssh root@<board-ip> chmod +x /tmp/collect_fb_display_baseline.sh
```

先停留在首页至少 10 秒，再采集首页场景：

```bash
DISPLAY_SCENARIO=home \
sh /tmp/collect_fb_display_baseline.sh \
  /userdata/display-baseline/home 60 1
```

进入 YOLO 页面，确认画面和检测框已经稳定，再采集：

```bash
DISPLAY_SCENARIO=yolo \
sh /tmp/collect_fb_display_baseline.sh \
  /userdata/display-baseline/yolo 120 1
```

填写两个目录内的 `operator_notes.txt`，然后复制回主机：

```bash
mkdir -p docs/logs/st7789-drm/2026-08-27
scp -r root@<board-ip>:/userdata/display-baseline/home \
  docs/logs/st7789-drm/2026-08-27/
scp -r root@<board-ip>:/userdata/display-baseline/yolo \
  docs/logs/st7789-drm/2026-08-27/
```

板端 RTC 已知可能停留在 2021 年；归档目录以主机真实日期命名，并保留 `manifest.txt` 中的板端 uptime。

## 数据汇总

```bash
python3 docs/tools/summarize_fb_display_baseline.py \
  docs/logs/st7789-drm/2026-08-27/home \
  docs/logs/st7789-drm/2026-08-27/yolo
```

脚本使用 `/proc/stat` 和 `/proc/<pid>/stat` 进行低扰动采样，不运行 `top`，避免旧基线中 `top` 自身占用较高 CPU。它输出 DeskBot CPU、系统 busy、RSS/HWM，以及 dmesg 若存在的 fbtft 吞吐记录。

### 2026-08-27 板端结果

两次采集使用同一个 PID 540，期间未发现 DeskBot 重启。采集文件中的 `elapsed_s` 覆盖首页 59 秒、YOLO 119 秒；由于每次采样本身还要读取 proc/sysfs，首末 `/proc/uptime` 实际跨度分别约 62.01 秒和 129.78 秒。

| 指标 | 首页 | YOLO | 解释 |
| --- | ---: | ---: | --- |
| DeskBot CPU 均值 / p95 | 3.00% / 4.00% | 35.71% / 37.76% | 单核 RV1106 上按进程 jiffies / 全系统 jiffies 计算 |
| 系统 busy 均值 / p95 | 7.64% / 10.78% | 51.19% / 53.40% | idle 与 iowait 计为空闲 |
| RSS 均值 / 最大值 | 7300 / 7300 KiB | 13691 / 13828 KiB | YOLO 采样末尾为 13684 KiB，119 秒内未见持续上涨 |
| VmHWM 最大值 | 10952 KiB | 14360 KiB | HWM 是进程启动以来峰值，不能当作场景瞬时增量 |
| fbtft 传输吞吐 | 4319 KiB/s | 3821 KiB/s | 各只有 3 条 debug 样本，且 `debug=7` 会扰动测量 |
| 吞吐折算全屏传输能力 | 28.80 次/s | 25.47 次/s | 只表示 153600-byte 全屏 payload 的等效传输能力，不是画面 FPS |
| fbtft 日志实际 update FPS | 0 | 8～11 | 首页静态；YOLO 与应用约 8.9 FPS 的不同画面发布率一致 |
| SPI0 IRQ 增量 | 65 | 5825 | 按 uptime 跨度约 1.05/s 与 44.88/s，仅作链路负载旁证 |

YOLO 日志中的最后一条累计应用统计为 `fps=8.889`、`e2e_p95=193 ms`、`infer_p95=94 ms`、`queue_p95=76 ms`，1200 个测量帧全部采用 media PTS，`timeouts/recoveries/failures=0/0/0`。这里的 e2e 终点仍是 RGB565 front buffer 发布，不包含 LVGL、fbdev、SPI 和 LCD 光学响应。

运行时证据确认：

- 只有 `/dev/fb0`，名称 `fb_st7789v`，320×240、16 bpp、stride 640、rotation 270；没有 `/dev/dri`。
- `spi0.0` 绑定 `fb_st7789v`，OF 节点为 `/spi@ff500000/fbtft@0`，compatible 为 `sitronix,st7789v`；没有活动的 SPI0 spidev 冲突。
- 运行时 DT 与主机反编译 DTB 一致：SPI0 CS0、60 MHz、buswidth 8、fps 60、debug 7、DC=`GPIO1_D0`、reset=`GPIO1_C4` active-low。
- 首页和 YOLO 的 DeskBot、YOLOv5 模型及 libdrm 校验值一致，保证两组场景来自同一软件基线。

完整结论和数据质量说明见 `docs/logs/st7789-drm/2026-08-27/board_fb_baseline_summary.md`。

FB 驱动到 TinyDRM 的逐字节等价基准见 `docs/logs/st7789-drm/2026-08-27/fb_driver_migration_baseline.md`。其中记录了：

- 实际初始化命令和 gamma 表；
- reset 的物理时序及 active-low descriptor 迁移陷阱；
- DC 命令/数据电平；
- SPI0 CS0、60 MHz、8-bit、mode/bit order；
- `MADCTL=0x60`、RGB565 和线上高字节优先；
- 原生 240×320、逻辑 320×240、0/0 offset 及全屏地址窗口；
- 独立 PWM9 背光约束。

## 阶段一完成标准

- [x] 首页与 YOLO 均有 60/120 个低扰动原始样本；采集跨度满足约 60/120 秒目标。
- [x] 已确认运行时 DT、SPI0 CS0 driver binding、DC/reset、SPI 最大频率和 spidev 冲突状态。
- [x] 已确认 `/dev/fb*`、`/dev/dri/*` 和 SPI/DRM 相关 dmesg。
- [x] CPU、RSS/HWM、fbtft 吞吐及其 debug 干扰有明确记录。
- [x] 用户已确认颜色、方向正常、无偏移、无明显撕裂、闪烁或花屏；确认记录写入迁移基准和板端汇总，原始 `operator_notes.txt` 不回写。
- [x] 已从当前驱动源码固化初始化命令、reset/DC、rotation、RGB/BGR、分辨率、地址窗口、像素线上字节序和 PWM 背光约束。
- [x] 阶段一完成，可以进入 TinyDRM 驱动实现。
- [ ] 当前运行固件未暴露内核 config、GPIO debugfs 和 clock summary；60 MHz 实际 SCLK 及 GPIO 波形也未用仪器测量，在驱动实现或新固件验证时补采，不阻塞阶段一。
