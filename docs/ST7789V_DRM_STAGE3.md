# ST7789V DRM 阶段三：LVGL 直连 DRM/KMS

## 当前状态

阶段三已完成。DeskBot 已从 `linux_fbdev` 切换为 `linux_drm`，板端进程直接持有 `/dev/dri/card0` 且不持有 `/dev/fb0`；Home/YOLO 的画面、页面生命周期、约 30 分钟持续刷新和单轮同口径性能采样均已通过。现有数据足以证明迁移无功能回退，但旧 FB 基线的 fbtft `debug=7` 会扰动系统开销，因此本阶段不宣称排除干扰后的最终性能提升百分比。

本阶段基于分支 `feat/st7789v-drm` 开始，前置提交为：

- `2b0fb8b38 docs(drm): capture ST7789V framebuffer baseline`
- `aad007f4a feat(drm): add ST7789V mipi-dbi display driver`

## 应用链路

```text
LVGL RGB565 direct render
  -> two DRM dumb buffers
  -> atomic page flip
  -> /dev/dri/card0
  -> st7789v-dbi DRM simple display pipe
  -> mipi-dbi/SPI0
  -> ST7789V 320x240
```

与阶段二的过渡链路相比，本阶段不再由 DeskBot 打开 `/dev/fb0`。内核的 DRM fbdev emulation 可以继续存在作为诊断和回退设施，但它不是新应用的显示数据路径。

## 主机侧修改

- `Demo/DeskBot_demo/conf/dev_conf.h`
  - RV1106：`LV_USE_LINUX_FBDEV=0`、`LV_USE_LINUX_DRM=1`、`LV_USE_EVDEV=1`。
  - x86 模拟器：保持 SDL，显式关闭 DRM 和 FBDEV。
  - 增加互斥检查，保证 DRM、FBDEV、SDL 三个显示后端恰好启用一个。
- `Demo/DeskBot_demo/lv_conf.h`
  - 不再无条件把 `LV_USE_LINUX_DRM` 覆盖为 0，由板级配置决定后端。
- `Demo/DeskBot_demo/CMakeLists.txt`
  - ARM 配置阶段强制检查目标 sysroot 的 libdrm。
  - 将 libdrm include 目录挂到实际编译 `lv_linux_drm.c` 的 `lvgl` target，避免只在最终可执行文件链接 libdrm、编译 LVGL 时却找不到 `drm.h`。

`main.c` 已经具备 DRM 初始化分支，无需重复实现：默认使用 `/dev/dri/card0`，也可通过 `LV_LINUX_DRM_CARD` 指定其他 card。connector id 传入 `-1`，由 LVGL 选择第一个 connected 且具有 mode 的 connector；当前板端只有 `card0-SPI-1`，其 mode 已在阶段二确认为 320x240。

LVGL 9.2.2 的当前 DRM 后端在 `LV_COLOR_DEPTH=16` 时申请 `DRM_FORMAT_RGB565`，创建两个全屏 dumb buffer，以 `LV_DISPLAY_RENDER_MODE_DIRECT` 渲染，并通过 non-blocking atomic commit/page-flip event 交换 buffer。因此本轮不引入额外的软件色彩转换，也不修改阶段二已经验证的面板 rotation、RGB/BGR、offset 和初始化寄存器。

## RootFS 前置依赖

目标系统必须包含 libdrm 运行库。当前工作区的 `echo_mate_defconfig` 已有：

```text
BR2_PACKAGE_LIBDRM=y
```

当前 RV1106 sysroot 已包含 `xf86drm.h`、`libdrm/drm.h` 和 `libdrm.so.2.4.0`。该 defconfig 同时带有用户已有的其他 Buildroot 改动，阶段三提交只选择性纳入 `BR2_PACKAGE_LIBDRM=y`，不覆盖或提交其他依赖改动。

SDK 的 `project/app/deskbot/Makefile` 已确认以根目录 `Demo/DeskBot_demo` 作为 `DESKBOT_SOURCE`，因此正式执行 SDK app/OEM 打包时会使用本轮配置，不需要把源码复制到 SDK 工作目录。该 SDK 应用目录是用户已有的未跟踪打包工作副本，本轮只读核对，没有改写。

## 主机验证结果

完整命令和产物信息见 `docs/logs/st7789-drm/2026-08-27/stage3_host_build.md`。已确认：

- fresh CMake 目录使用 RV1106 ARM/uClibc toolchain 配置成功。
- `lv_linux_drm.c` 编译成功，完整 DeskBot target 构建到 100%。
- 可执行文件为 32-bit ARM EABI5，解释器为 `/lib/ld-uClibc.so.0`。
- dynamic section 包含 `NEEDED libdrm.so.2`。
- 最终程序定义 `lv_linux_drm_create` 和 `lv_linux_drm_set_file`，不包含启用后的 `lv_linux_fbdev_create/set_file`。
- 最终程序包含 `/dev/dri/card0`，证明默认设备路径已进入 ARM 产物。
- `git diff --check` 对三个应用配置文件通过。

编译期间仍有项目既有 warning，本轮未顺带重构；DRM 接入本身没有产生编译错误。

## 板端部署与验收

先通过现有 OEM/应用部署流程替换 DeskBot 文件，再执行以下检查。若只替换可执行文件，建议先保留旧的 FBDEV 程序副本，以便发现 DRM 初始化问题时快速回退。

```sh
ls -l /dev/dri/card0 /sys/class/drm/card0-SPI-1
cat /sys/class/drm/card0-SPI-1/status
cat /sys/class/drm/card0-SPI-1/modes
ls -l /usr/lib/libdrm.so.2*

/oem/usr/bin/RkLunch.sh stop
/oem/usr/bin/RkLunch.sh start
/oem/usr/bin/RkLunch.sh status

pid="$(cat /var/run/deskbot.pid)"
ls -l "/proc/$pid/fd"
tail -n 100 /userdata/deskbot/deskbot.log
dmesg | grep -i -E 'st7789|drm|spi|failed|error'
```

`/proc/$pid/fd` 中应出现指向 `/dev/dri/card0` 的文件描述符；不应出现 DeskBot 打开的 `/dev/fb0`。如果程序启动即退出，优先查看：

- `Permission denied`：card0 设备权限或启动用户不允许打开。
- `Device or resource busy` / `drmModeAtomicCommit failed`：另一个用户态进程占用 DRM master，先停止该进程；不要先删除内核 fbdev emulation。
- `No atomic modesetting support`：内核 DRM atomic 能力与应用预期不一致。
- `suitable connector not found`：connector 未 connected 或没有 mode。
- `DRM buffer allocation failed`：dumb buffer/GEM CMA 分配失败。
- `libdrm.so.2` 加载失败：RootFS 未带运行库，重新生成 rootfs/OEM，而不是从主机复制错误架构的库。

首次启动后检查 Home 页面：分辨率、方向、红蓝色、触摸坐标、边缘 offset、局部刷新、撕裂/闪烁/花屏。再进入 YOLO 页面观察连续刷新，并至少保持 30 分钟，与阶段二功能稳定性口径一致。

本轮板端结果：

- 板端运行文件与主机 ARM 产物 SHA-256 均为 `f74590f30d4d2027b2a8f9b1ae7cfcdc401e3713922180747acdee1737c9cf8e`。
- Home/YOLO 采样中，DeskBot fd 3 均指向 `/dev/dri/card0`，并可见两个 DRM dumb buffer 映射；没有发现应用持有 `/dev/fb0`。
- 用户确认颜色、方向、页面操作、offset 正常，无明显撕裂、闪烁或花屏；重复进入/退出 YOLO 与约 30 分钟持续刷新均正常。
- Home、YOLO 的采样前后 dmesg 分别逐字节一致，窗口内没有新增 DRM/SPI 内核错误。
- 首次部署曾因旧 FBDEV 可执行文件和不完整的 OEM 内核模块目录导致启动异常；恢复与内核匹配且可执行的 `/oem/usr/ko/insmod_ko.sh` 及模块目录、再部署正确 DRM 产物后问题消失。这是部署一致性问题，不是最终 DRM 数据路径错误。

完整原始数据和限制说明见 `docs/logs/st7789-drm/2026-08-27/drm/board_drm_summary.md`。

## 性能与完成标准

已使用阶段一相同场景和采样工具分别记录 Home、YOLO。单轮结果如下：

| 场景 | 后端 | 进程 CPU 均值 | 系统 busy 均值 | RSS 均值 | YOLO FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| Home | FB/fbtft | 3.00% | 7.64% | 7300 KiB | N/A |
| Home | DRM/KMS | 2.96% | 6.83% | 10636 KiB | N/A |
| YOLO | FB/fbtft | 35.71% | 51.19% | 13691 KiB | 8.889 |
| YOLO | DRM/KMS | 36.22% | 39.81% | 10872 KiB | 8.807 |

YOLO 进程 CPU 和应用 FPS 基本持平，系统 busy 在本轮下降 11.38 个百分点，RSS 均值下降约 2819 KiB。旧 FB 的 debug 输出和两次采样的内存起点不同，故只把系统 busy/RSS 差值视为趋势，不把它写成最终优化比例。DRM 没有 fbtft `Display update` debug 记录，因此 SPI throughput 为 N/A；若要比较显示提交速率，应增加 DRM flush/commit 计时或外部波形测量。

当前还有两个量化限制：

- `/proc/meminfo` 在采样中持续显示 `CmaTotal=67584 kB`、`CmaFree=0 kB`。当前 DRM 与 RKNN/VI/VPSS 可正常运行，但后续仍需观察冷启动和重复生命周期下的分配余量。
- 当前只有一轮正式 A/B；最终 Performance Report 应补至少两轮冷启动同条件采样，并报告中位数和范围。

阶段三只有同时满足以下条件才能标记完成：

- [x] ARM 配置只启用 LVGL linux_drm，交叉构建成功。
- [x] 产物链接目标 libdrm 并包含 DRM 后端符号。
- [x] DeskBot 进程直接持有 `/dev/dri/card0`，且不持有 `/dev/fb0`。
- [x] Home/YOLO 的颜色、方向、触摸/页面操作、offset 和刷新均正常。
- [x] 当前采样窗口的 dmesg 无新增 DRM/SPI error，YOLO 最终 metrics 的 failures 为 0。
- [x] 完成约 30 分钟持续刷新和一轮同口径性能采样。
