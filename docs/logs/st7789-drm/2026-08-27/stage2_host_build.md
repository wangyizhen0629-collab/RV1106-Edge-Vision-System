# ST7789V DRM 阶段二构建与板端验证记录

- 日期：2026-08-27
- 状态：完成。首版复现全帧更新 `-EINVAL (-22)`；16-bit SPI 分片对齐修复版已通过 probe、DRM connector/mode、视觉项和 Home/YOLO 约 30 分钟持续刷新验收。
- 工具链：`arm-rockchip830-linux-uclibcgnueabihf-gcc 8.3.0`
- 内核目标：`ARCH=arm`、`echo_rv1106_linux_defconfig`、`rv1106g-echo-mate.dtb`

## 静态检查

- `drivers/gpu/drm/tiny/st7789v.c`：checkpatch 0 error、0 warning。
- `Documentation/devicetree/bindings/display/sitronix,st7789v-dbi.txt`：checkpatch 0 error、0 warning。
- `drivers/gpu/drm/drm_mipi_dbi.c` 修复 patch：checkpatch 0 error、0 warning。
- `.config`：

```text
CONFIG_DRM_MIPI_DBI=y
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_DRM_KMS_CMA_HELPER=y
# CONFIG_DRM_PANEL_SITRONIX_ST7789V is not set
CONFIG_TINYDRM_ST7789V=y
# CONFIG_FB_TFT is not set
```

- `vmlinux` 符号：已确认 `mipi_dbi_spi_transfer`、`st7789v_probe` 和 `st7789v_driver`。
- 反编译 DTB：只保留 `display@0`/`sitronix,st7789v-dbi`，包含 CS0、60 MHz、rotation 270、backlight、DC active-high 和 reset active-low；未发现 fbtft/spidev enabled 节点。

## 构建产物

```text
aec8045c8e73f5e5691f08b26d9c038847b39986dec3aad245a10ed06f707774  sysdrv/source/kernel/arch/arm/boot/zImage
aa3696cb59831ce40703021177f37474e752da0d0e5546f2066ad6f37eb28ea6  sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-echo-mate.dtb
a6b0dfea211aa38744367c48506709a7ff3f0bbc57e835b3ac861995fd679082  output/image/boot.img
```

- zImage：3,053,296 bytes，2026-08-27 12:01:01 +0800。
- DTB：36,073 bytes，2026-08-27 12:01:02 +0800。
- boot.img：3,167,232 bytes，2026-08-27 12:01:10 +0800。
- `./build.sh kernel` 成功；`dumpimage -l` 确认 boot.img 是包含 kernel、FDT 和 resource 的 ARM FIT 镜像。
- 从 boot.img 提取的 FDT SHA-256 同为 `aa3696cb59831ce40703021177f37474e752da0d0e5546f2066ad6f37eb28ea6`；反编译后确认 `sitronix,st7789v-dbi` 节点和最终 SD 卡 rootfs bootargs。
- 烧录前执行 `upgrade_tool ld`，返回 `List of rockusb connected(0)`；当前主机没有枚举到 Loader/Maskrom 设备，因此未执行任何分区写入。

DTB 反编译时出现的 `unit_address_vs_reg`、`simple_bus_reg`、GPIO `#address-cells`、`alias_paths` 和 `graph_child_address` 警告来自现有板级设备树；本次目标 DTB 的正常构建没有失败。未把这些无关警告与 ST7789V 驱动结果混写。

## 首次板端结果与修复

首版镜像 SHA-256 为 `cbccdba23696fd185622e62972bac525823bfb04e94469bc91fc0ce1f965f43e`。烧录启动后屏幕花屏，板端日志为：

```text
[drm] Initialized st7789v-dbi 1.0.0 20260827 for spi0.0 on minor 0
st7789v-dbi spi0.0: [drm] *ERROR* Failed to update display -22
st7789v-dbi spi0.0: [drm] fb0: st7789v-dbidrmf frame buffer device
```

驱动 probe、初始化和 DRM fbdev 创建均已成功，错误发生在第一次像素更新。Rockchip SPI 驱动的最大传输长度为 `0xffff`，mipi-dbi 又因控制器支持 16 bpw 而直接发送 RGB565。原公共层第一个分片为 65,535 bytes，不是 2-byte word 的整数倍，被 SPI core 返回 `-EINVAL`。

已在 `mipi_dbi_spi_transfer()` 中把 `max_chunk` 向下对齐到 2 bytes；153,600-byte 全帧现在拆为 65,534 + 65,534 + 22,532 bytes。对象文件和完整 FIT 均重新构建成功，FIT 中提取的 FDT 与输出 DTB 哈希一致。后续修复版板端结果见下一节。

## 修复版板端复测

修复版烧录后，屏幕不再花屏，DeskBot 应用正常启动。用户回传：

```text
[    0.079378] rockchip-spi ff500000.spi: no high_speed pinctrl state
[    0.080758] [drm] Initialized st7789v-dbi 1.0.0 20260827 for spi0.0 on minor 0
[    0.436049] st7789v-dbi spi0.0: [drm] fb0: st7789v-dbidrmf frame buffer device
[    0.437358] spi-nand spi2.0: unknown raw ID 00000000
[    0.437394] spi-nand: probe of spi2.0 failed with error -524
```

```text
/sys/bus/spi/devices/spi0.0/driver -> ../../../../../../bus/spi/drivers/st7789v-dbi
/sys/class/graphics/fb0/name = st7789v-dbidrmf
```

补充 KMS 枚举结果：

```text
/dev/dri/card0
/sys/class/drm/card0
/sys/class/drm/card0-SPI-1
card0-SPI-1/status = connected
card0-SPI-1/modes = 320x240
```

用户人工确认颜色、方向和 offset 正常，无明显撕裂/闪烁；Home 和 YOLO 页面持续刷新约 30 分钟，运行正常。

结论：mipi-dbi 分片对齐修复在实机生效；ST7789V probe、SPI0 绑定、DRM fbdev、KMS connector/mode 和应用显示链路均通过，旧 `fb_st7789v` 未重新绑定。日志中没有 `Failed to update display -22`。SPI0 的 `no high_speed pinctrl state` 未阻塞本次显示；SPI NAND 报错位于 `spi2.0`，不作为 ST7789V 显示失败处理。

证据限制：实际 SCLK/reset/DC 波形未使用逻辑分析仪测量；本阶段未采集 CPU、RSS 或端到端时延。这些项目不阻塞驱动迁移和显示功能验收，性能量化转入后续 linux_drm 接入与 Performance Report。

## 说明

独立 `O=/tmp/...` 构建因源码树已有 in-tree 产物而被 Kbuild 拒绝。为了不执行 `mrproper` 清除用户已有 SDK 输出，本次改用现有源码树进行 in-tree 构建并成功完成完整 zImage 链接。
