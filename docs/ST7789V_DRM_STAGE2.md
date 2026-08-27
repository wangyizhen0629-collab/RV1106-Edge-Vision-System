# ST7789V DRM 阶段二：mipi-dbi 驱动接入

## 状态

阶段二已完成。首版 `boot.img` 中，`st7789v-dbi` 成功 probe 且 DRM fbdev 成功创建，但第一次全帧更新返回 `-EINVAL (-22)`，实物表现为花屏。问题定位为 Rockchip SPI 的奇数最大传输长度与 16-bit RGB565 word 不对齐，并按 Linux 上游方案修复。修复版实机确认：`-22` 与花屏消失，SPI0 驱动绑定和 DRM fbdev 正常；`card0-SPI-1` connector 为 connected，mode 为 320×240；颜色、方向、offset、撕裂/闪烁均无异常，Home/YOLO 持续刷新约 30 分钟正常。

本阶段只替换内核显示驱动，不切换 DeskBot/LVGL 的显示后端。内核开启 DRM fbdev emulation，因此过渡验证期间 `/dev/fb0` 仍可能存在，但它应由 DRM 提供，不再由 `fb_st7789v` 提供。后续阶段再把 LVGL 从 fbdev 切换到 `/dev/dri/card*`。

## 实现范围

### 1. 屏幕定义

- 新增 `CONFIG_TINYDRM_ST7789V`，复用内核 5.10 的 DRM mipi-dbi、CMA 和 atomic helpers。
- 定义 ST7789V 原生模式为 240×320；DTS 中 `rotation = <270>` 经 mipi-dbi 公共层旋转后，对用户空间暴露的逻辑模式应为 320×240。
- 像素格式沿用 DRM mipi-dbi 的 RGB565/XRGB8888 输入支持，面板输出为 RGB565。
- 地址窗口偏移保持阶段一基线的 `left_offset = 0`、`top_offset = 0`。
- 新 compatible 使用 `sitronix,st7789v-dbi`，避免与内核已有的 `sitronix,st7789v` SPI-control + DPI-pixel panel binding 冲突。

### 2. 初始化

初始化序列逐项沿用已经实机确认的 fbtft 基线：

- reset：物理低电平 20～40 µs，释放后等待 120 ms。
- `SLPOUT (0x11)` 后等待 120 ms。
- `COLMOD (0x3a) = 0x05`，即 RGB565。
- 保留 `PORCTRL/GCTRL/VDVVRHEN/VRHS/VDVS/VCOMS/VCMOFSET/PWCTRL1` 参数和正负 gamma 表。
- `rotation = 270` 生成 `MADCTL = 0x60`；不设置 BGR 位，保持 RGB 顺序。
- 不额外启用 display inversion，避免引入未在旧链路验证过的面板行为。

reset GPIO 在 DTS 中是 `GPIO_ACTIVE_LOW`。gpiod descriptor 接口传递的是逻辑值，因此驱动使用逻辑 1 拉低并断言 reset、逻辑 0 拉高并释放。没有直接调用本内核版本的 `mipi_dbi_hw_reset()`，因为它的固定 0→1 逻辑序列与本板 active-low 描述组合后会得到相反的物理时序。

### 3. probe 与公共辅助层

probe 流程参考内核现有 `ili9341.c`：

1. 通过 `devm_drm_dev_alloc()` 分配 DRM/mipi-dbi 私有对象。
2. 获取必需的 reset、DC GPIO 和背光设备。
3. 读取 `rotation`，调用 `mipi_dbi_spi_init()` 建立 DBI Type C option 3 SPI 命令/数据通道。
4. 将控制器标记为 write-only，避免板上无 MISO 时触发读取。
5. 调用 `mipi_dbi_dev_init()`、`drm_dev_register()` 和 `drm_fbdev_generic_setup()`。
6. 复用 `mipi_dbi_pipe_update()` 完成脏矩形窗口设置、RGB565 转换和 SPI 刷新。

普通短命令受 mipi-dbi 公共层的安全频率约束；像素数据按 DTS 的 60 MHz 上限发送。Rockchip SPI 控制器支持 16-bit word，因此 RGB565 全帧可走公共层的 16 bpp 发送路径。60 MHz 实际 SCLK、字节顺序和稳定性仍需用板端画面或逻辑分析仪验证。

首次板端更新暴露出本 SDK 5.10 mipi-dbi 公共层缺少一个传输边界处理：Rockchip SPI 返回的 `spi_max_transfer_size()` 是 `0xffff`（65,535 bytes），公共层据此切出的第一个 16-bit 分片长度为奇数，SPI core 的“禁止 partial word”校验因此返回 `-EINVAL`。现在公共层先执行 `ALIGN_DOWN(max_chunk, 2)`，全帧 153,600 bytes 被拆成 65,534 + 65,534 + 22,532 bytes，三个分片都保持 RGB565 word 完整。这一修复保留了 16-bit 发送与原字节序，没有退回 8-bit bounce buffer。

## fbtft 解绑

本阶段通过两层措施避免旧驱动争用 SPI0 CS0：

- DTS 将节点从 `fbtft@0`/`sitronix,st7789v` 改为 `display@0`/`sitronix,st7789v-dbi`。
- defconfig 设置 `# CONFIG_FB_TFT is not set`，移除 `FB_TFT_ST7789V` 和无关的 `FB_TFT_ST7735R`。

`CONFIG_FB=y` 继续保留，因为 DRM fbdev emulation 依赖 framebuffer core。这不代表 fbtft 仍然启用。板端验收时，`/sys/class/graphics/fb0/name` 应显示 DRM fbdev 名称，`spi0.0/driver` 应指向 `st7789v-dbi`，且不能再出现 `fb_st7789v`。

## 修改文件

- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/tiny/st7789v.c`
- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/drm_mipi_dbi.c`
- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/tiny/Kconfig`
- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/tiny/Makefile`
- `SDK/rv1106-sdk/sysdrv/source/kernel/Documentation/devicetree/bindings/display/sitronix,st7789v-dbi.txt`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-echo-mate.dts`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig`

## 主机验证结果

使用 SDK 自带 `arm-rockchip830-linux-uclibcgnueabihf-gcc 8.3.0` 完成以下检查：

```bash
make -C SDK/rv1106-sdk/sysdrv/source/kernel \
  ARCH=arm \
  CROSS_COMPILE="$PWD/SDK/rv1106-sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-" \
  echo_rv1106_linux_defconfig

make -C SDK/rv1106-sdk/sysdrv/source/kernel \
  ARCH=arm \
  CROSS_COMPILE="$PWD/SDK/rv1106-sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-" \
  drivers/gpu/drm/tiny/st7789v.o rv1106g-echo-mate.dtb

make -C SDK/rv1106-sdk/sysdrv/source/kernel \
  ARCH=arm \
  CROSS_COMPILE="$PWD/SDK/rv1106-sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-" \
  -j2 zImage

cd SDK/rv1106-sdk
./build.sh kernel
```

- 新驱动和 DT binding 的 `scripts/checkpatch.pl --no-tree --file` 均为 0 error、0 warning；mipi-dbi 修复 patch 的 `checkpatch.pl --no-tree` 同样为 0 error、0 warning。
- `st7789v.o`、`rv1106g-echo-mate.dtb`、完整 `zImage` 和 FIT `boot.img` 构建成功。
- 生成 `.config` 包含 `CONFIG_DRM_MIPI_DBI=y`、`CONFIG_TINYDRM_ST7789V=y`、`CONFIG_DRM_FBDEV_EMULATION=y` 和 `# CONFIG_FB_TFT is not set`。
- `vmlinux` 中存在 `mipi_dbi_spi_transfer`、`st7789v_probe` 和 `st7789v_driver`，确认公共层修复和新驱动均已静态链接。
- `dumpimage -l` 确认 `boot.img` 包含 kernel、FDT 和 resource；从 FIT 提取的 FDT 哈希与 SDK 输出 DTB 完全一致。
- 反编译从 `boot.img` 提取的 DTB，只发现 `display@0`，其 compatible、60 MHz、rotation 270、DC、reset 和 backlight 与设计一致；未发现旧 `fbtft@0`。

产物：

| 产物 | 大小 | SHA-256 |
| --- | ---: | --- |
| `sysdrv/source/kernel/arch/arm/boot/zImage` | 3,053,296 bytes | `aec8045c8e73f5e5691f08b26d9c038847b39986dec3aad245a10ed06f707774` |
| `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-echo-mate.dtb` | 36,073 bytes | `aa3696cb59831ce40703021177f37474e752da0d0e5546f2066ad6f37eb28ea6` |
| `output/image/boot.img` | 3,167,232 bytes | `a6b0dfea211aa38744367c48506709a7ff3f0bbc57e835b3ac861995fd679082` |

首版失败镜像的 `boot.img` SHA-256 是 `cbccdba23696fd185622e62972bac525823bfb04e94469bc91fc0ce1f965f43e`，不应继续用于板端复测。

第一次尝试使用独立 `O=/tmp/...` 构建时，Kbuild 检测到源码树已有历史 in-tree 产物并要求 `mrproper`。为保护用户现有 SDK 构建输出，没有执行清理，而是沿用该源码树的 in-tree 构建；这不是驱动编译错误。

## 修复版板端验收结果

修复版已完成驱动枚举、KMS mode、应用画面和 30 分钟持续刷新验收，用户回传结果如下：

- `st7789v-dbi` 在 `spi0.0` 上初始化成功，DRM minor 为 0。
- 启动日志不再出现 `Failed to update display -22` 或其他 ST7789V/DRM error。
- `/sys/bus/spi/devices/spi0.0/driver` 指向 `drivers/st7789v-dbi`。
- `/sys/class/graphics/fb0/name` 为 `st7789v-dbidrmf`，确认 `/dev/fb0` 来自 DRM fbdev emulation，而不是旧 fbtft。
- 屏幕不再花屏，DeskBot 应用正常启动并显示。
- `/dev/dri/card0` 存在，sysfs 中枚举出 `card0` 和 `card0-SPI-1`。
- `card0-SPI-1/status` 为 `connected`，`modes` 为 `320x240`。
- 人工确认颜色、方向和 offset 正常，无明显撕裂或闪烁。
- Home 和 YOLO 页面持续刷新约 30 分钟，运行正常。

`no high_speed pinctrl state` 是 SPI0 的既有警告，本次没有阻止显示传输；`spi-nand spi2.0` 的探测失败属于另一控制器，与 SPI0 上已经正常工作的显示设备分开记录。实际 SCLK/reset/DC 波形未使用逻辑分析仪测量，作为非阻塞证据限制保留。

## 板端验收

烧录包含上述 zImage/DTB 的 boot 镜像后，先采集枚举和绑定证据：

```sh
dmesg | grep -i -E 'drm|st7789|mipi|dbi|spi'
readlink /sys/bus/spi/devices/spi0.0/driver
ls -l /dev/dri /sys/class/drm
cat /sys/class/drm/card*/status 2>/dev/null
cat /sys/class/drm/card*/modes 2>/dev/null
cat /sys/class/graphics/fb0/name 2>/dev/null
zcat /proc/config.gz 2>/dev/null | grep -E 'DRM_MIPI_DBI|TINYDRM_ST7789V|FB_TFT'
```

预期：

- `spi0.0` 绑定 `st7789v-dbi`，无 `fb_st7789v` probe/绑定。
- 出现 `/dev/dri/card0`（编号以实际枚举为准）和一个 connected SPI/unknown connector。
- mode 为 320×240；如果 connector 命名与预期不同，以实际 sysfs 节点为准。
- `/dev/fb0` 即使存在，也来自 DRM fbdev emulation。

随后按同一测试图检查四角定位线和纯红/绿/蓝/白/黑色块，再启动 Home 与 YOLO 页面：

- [x] 颜色正常，红蓝未互换。
- [x] 方向与原 FB 基线一致。
- [x] 画面无整体偏移。
- [x] 启动和应用冒烟期间无花屏，且无 ST7789V/DRM update error。
- [x] 持续刷新期间无明显撕裂或闪烁。
- [x] Home/YOLO 页面持续刷新约 30 分钟，运行正常。
- [ ] 记录实际 SCLK/reset/DC 波形；若无逻辑分析仪，明确标记为未测。

修复版的首要判据是启动日志中不再出现 `Failed to update display -22`。如果该错误消失但画面仍异常，再按 `MADCTL/RGB-BGR → mode/offset → reset/DC 极性 → SPI 字节序 → 60 MHz 信号完整性` 的顺序排查。必要时先降低 `spi-max-frequency` 到 40 MHz 做诊断，但不能把降频结果直接写成最终基线。

## 下一步

阶段二板端验收已经通过。阶段三切换 DeskBot/LVGL 到 linux_drm 后端，直接打开 `/dev/dri/card*`，并用与阶段一相同的 Home/YOLO 场景采集 CPU、RSS、刷新率、错误计数和长稳数据。旧 fbtft 已从目标内核配置移除，回退应通过保留的阶段一 patch/commit 或单独回退分支完成，而不是运行时双驱动共存。
