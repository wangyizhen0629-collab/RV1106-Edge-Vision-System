# ST7789V DRM 阶段一主机静态清单

- 日期：2026-08-27（Asia/Shanghai）
- 分支：`feat/led-dts`
- 基线 commit：`03ca9f895`
- 阶段收口分支/HEAD：`perf/vi-vpss` / `787f62d8f`
- 状态：阶段一完成；板端 Home/YOLO 数据、运行链路和驱动迁移参数已归档，人工视觉结果已确认
- 修改范围：本轮不修改应用、DTS、defconfig、内核驱动或 SDK 产物

## 工作区保护

开始时 `git status --short` 显示 bring-up、VI/VPSS、SDK 打包和文档均存在未提交修改。与 DRM 阶段可能重叠的文件包括：

- `Demo/DeskBot_demo/CMakeLists.txt`
- `Demo/DeskBot_demo/gui_app/pages/ui_YOLOPage/ui_YOLOPage.c`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig`
- `SDK/rv1106-sdk/sysdrv/tools/board/buildroot/echo_mate_defconfig`
- `docs/PROGRESS.md`

本轮未覆盖或格式化这些文件。正式实现 TinyDRM 前，应先保存当前 VI/VPSS/BSP 工作的可恢复检查点。

## 显示链静态事实

```text
LVGL RGB565
  → linux_fbdev
  → mmap /dev/fb0
  → fb_st7789v deferred I/O
  → RGB565 endian conversion
  → SPI0 CS0, 60 MHz
  → ST7789V, rotation 270
  → 320×240 LCD
```

- `dev_conf.h` 的 ARM 路径启用 FBDEV，`lv_conf.h` 的 DRM 开关为 0。
- LVGL FBDEV 配置为 FULL render、两个全屏 draw buffer。
- fbtft 分配一个 240×320×16-bit framebuffer；默认 4 KiB tx buffer 对 RGB565 逐像素执行 `cpu_to_be16()`。
- DTS 的 `fps=60` 是 deferred I/O 调度目标，不代表 SPI 能完成 60 次全屏刷新。60 MHz 下仅 RGB565 payload 的理论极限为 48.83 次/秒。
- Rockchip SPI controller 源码声明支持 8/16-bit word；这使 DRM mipi-dbi 的 RGB565 full-frame direct transmit 路径具备静态前提，但实际字节序和 DMA 行为仍须板端验证。

## 已生成 DTB 核对

- 文件：`SDK/rv1106-sdk/output/out/sysdrv_out/board_uclibc_rv1106/rv1106g-echo-mate.dtb`
- 构建时间：2026-08-26 11:28:21 +0800
- 大小：36081 bytes
- SHA-256：`af779eae0cb77f58120ec56fff91e2719412a2e1fc2e0db90f9c920a94150b1b`
- 反编译显示 SPI0 下只有 `fbtft@0`：60 MHz、fps=60、buswidth=8、debug=7、rotate=270、DC=`GPIO1_D0`、reset=`GPIO1_C4` active-low。
- 公共 DTSI 中虽然存在同为 CS0 的 `spidev@0`，但顶层 `rv1106g-echo-mate.dts` 将其 `status` 覆盖为 disabled，因此生成 DTB 未保留可用 spidev 设备。运行板固件是否正是该 DTB仍待板端校验。

## 内核方案边界

- 当前内核有 `drivers/gpu/drm/drm_mipi_dbi.c`，支持 RGB565/XRGB8888、atomic simple display pipe 和 damage clips。
- `drivers/gpu/drm/tiny/` 只有 ST7735R、ILI9341 等驱动，没有 ST7789V SPI pixel driver。
- `panel-sitronix-st7789v.c` 注册的是 `DRM_MODE_CONNECTOR_DPI`，SPI 只传 9-bit 控制命令，不是本板 SPI pixel 链的可复用驱动。
- 推荐后续新增基于当前内核 API 的最小 ST7789V mipi-dbi driver，而不是将 ST7735R compatible 强行用于 ST7789V。

## 历史日志可用性

- 2026-07-08 日志确认 `/dev/fb0`、`fb_st7789v`、320×240、16 bpp，未枚举到 `/dev/dri/*`。
- 三个 fbtft update 样本为 4479、4514、4522 KiB/s，平均 4505 KiB/s，折算约 30.03 full frames/s。
- DTS `debug=0x7` 导致每个 4 KiB SPI chunk 被打印；历史吞吐和旧 `top` CPU 数据均受日志采样行为影响。
- 旧 CPU 样本属于 VI/VPSS 优化前链路，不能作为当前 RKMPI＋FB 的正式性能基线。

## 板端采集结果

- [x] 首页 60 个低扰动 CPU/RSS 样本，`elapsed_s` 59 秒，uptime 跨度 62.01 秒。
- [x] YOLO 120 个低扰动 CPU/RSS 样本，`elapsed_s` 119 秒，uptime 跨度 129.78 秒。
- [x] 当前运行 DT 的显示节点属性和 SPI0 CS0 driver binding。
- [x] `/dev/fb0`、`/dev/dri`、SPI 枚举、进程 maps/fd 和相关 dmesg。
- [ ] `/proc/config.gz` 或实际运行内核 config：当前固件不可用。
- [ ] GPIO/pinctrl 与 clock summary：当前 debugfs 不可用。
- [x] 用户在采集后确认颜色和方向正常、无偏移、无明显撕裂、闪烁或花屏；结论记录于 `board_fb_baseline_summary.md` 和 `fb_driver_migration_baseline.md`，原始 `operator_notes.txt` 不回写。
- [ ] 干净条件下的 SPI/full-screen 刷新吞吐：现有 `debug=7` 数据只能作为受扰动的数量级参考。

详细板端结论见 `board_fb_baseline_summary.md`；TinyDRM 需要复现的初始化寄存器、reset/DC 时序、SPI、像素格式、rotation、分辨率和背光约束见 `fb_driver_migration_baseline.md`。
