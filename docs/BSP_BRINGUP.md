# RV1106 BSP Bring-up：简历第一条实现与面试证据

## 1. 对应的简历描述

> BSP Bring-up 与系统定制：基于硬件原理图和 RV1106 SDK 完成 U-Boot、Linux Kernel、Device Tree 与 Buildroot 板级配置，定制 SD 启动、分区布局、Kernel Defconfig、RootFS 软件包及启动服务；沿“DTB 解析--总线枚举--Driver Match--Probe”链路排查设备节点缺失与驱动未绑定问题，完成系统镜像构建及板端启动验证。

本文件只建立该描述的工程事实和面试证据，不修改简历。

## 2. 当前完成状态

| 能力点 | 当前状态 | 证据 | 简历边界 |
| --- | --- | --- | --- |
| BoardConfig / SD 启动 / 分区布局 | 已完成主机侧配置与构建验证 | 生效 BoardConfig、构建输出的 `GLOBAL_PARTITIONS`、DTB 与 U-Boot env bootargs | 可以讲配置选择和验证；现有 Git 历史不足以证明所有初始分区值均由本人从零设计 |
| U-Boot | 已完成本轮构建 | `echo_rv1106_uboot_defconfig` + `rk-emmc.config`，生成 `uboot.img`、`idblock.img`、`download.bin` | 可以讲 defconfig/fragment 合并及 FIT/SPL 产物，不应说从零移植 U-Boot |
| Kernel / Defconfig | 已完成本轮构建 | `echo_rv1106_linux_defconfig`，生成 `Image`、`zImage`、`vmlinux` 和 `boot.img` | 可以讲 `=y`/`=m`、模块安装与验证，不应说开发了 Rockchip CIF/ISP/NPU 驱动 |
| Device Tree | 已完成源码修改、DTC 和 DTB 反编译验证 | 仅保留 SC3336 `0x30` 实例及一条闭合 media graph；已有 LED DTS commit | 可以讲板级资源描述与候选 sensor 冲突修复；新 DTB 尚待烧录验证 probe 结果 |
| Buildroot / RootFS | 已完成配置、构建和镜像验证 | 受控 `echo_mate_defconfig`、rootfs 动态库、init 脚本、`rootfs.img` | 可以讲 RootFS 定制与依赖闭环 |
| DeskBot 启动服务 / OEM 打包 | 已完成主机侧实现与镜像验证 | ARM/uClibc ELF、`RkLunch.sh`、`S21appinit`、OEM 镜像内容 | 可以讲交叉编译、启动管理、只读资源与持久数据分离 |
| 完整固件 | 已完成主机侧构建 | `update.img` 包含 env/idblock/uboot/boot/oem/userdata/rootfs | 可以讲镜像构建；不能把本轮新镜像写成“已完成板端启动验证” |
| 新固件板端验证 | 未执行 | 当前环境无 ADB，libusb 也不可用；没有自动烧录 | 烧录并留存日志前，简历中的“板端启动验证”只能由旧基线支撑，不能当作本轮结果 |

## 3. 本轮解决的真实问题

### 3.1 问题一：三个不同 sensor 同时占用 I2C4 的 `0x30`

现象与证据：

- 历史板端 media graph 明确枚举到 `m00_b_sc3336 4-0030`，实物 sensor 为 SC3336。
- 原 DTSI 同时将 SC3336、SC4336、SC530AI 设为 `status = "okay"`。
- 三个节点均位于 I2C4、地址均为 `0x30`，并共享 `MCLK_REF_MIPI0` 与 `GPIO3_C5` PWDN。

问题分析：

1. 内核展开 DTB 后，I2C core 根据启用的子节点创建 `i2c_client`。
2. 同一 adapter 上不能稳定注册多个相同从地址的 client。
3. 即使驱动源码都存在，错误的板级实例也会造成无意义的匹配、probe 失败或枚举结果依赖节点处理顺序。
4. 这不是 sensor 驱动算法问题，而是 Device Tree 对实际硬件描述不唯一。

修改：

- 保留 `sc3336@30` 为 `okay`。
- 将 SC4336 和 SC530AI 设为 `disabled`。
- 删除 DPHY 输入端和禁用 sensor 中对应的无效 endpoint，避免悬空 `remote-endpoint`。
- Echo-Mate 专用 Kernel defconfig 只保留 `CONFIG_VIDEO_SC3336=m`，不再构建未装配的 OS04A10、SC4336 和 SC530AI sensor 模块。
- BoardConfig 只打包 `sc3336_CMK-OT2119-PC1_30IRC-F16.json`。
- SDK 中不存在该 SC3336 模组对应的 CAC 标定目录，因此不再错误打包 SC4336 CAC；没有虚构替代标定文件。

验证：

- 第一次只禁用 sensor 后，DTC 报出两条无效 graph phandle 警告。
- 删除未使用 endpoint 后重新构建，Kernel 目标中的 DTC 阶段不再出现这两条 graph 警告。
- 反编译最终 DTB，只能检索到 `sc3336@30` 和两条 data lane，未出现 SC4336/SC530AI 节点。
- OEM 暂存目录和 `oem.img` 中只包含 SC3336 IQ JSON。

这是一条完整的面试叙述：

> 根据历史 media graph 确认板载器件为 SC3336 后，发现参考 DTS 同时启用了三个同地址候选 sensor。我沿 DTB 实例化到 I2C client/probe 的链路分析，禁用非板载节点并删除悬空 media endpoint，同时将 IQ 资源收敛到 SC3336。DTC 构建无新增 graph 警告，反编译 DTB 和 OEM 镜像均确认仅保留 SC3336；新的板端 probe 结果待烧录后补齐。

### 3.2 问题二：应用能单独运行，但没有进入正式固件

原状态：

- DeskBot ARM 程序依赖手工放到 `/root/bin`。
- 原 `S99start_echo_defconfig` 中启动函数被注释。
- BoardConfig 使用 `RKIPC_RV1106`，其 `/oem/usr/bin/RkLunch.sh` 会启动另一套 IPC 应用并占用 camera pipeline。
- DeskBot 的模型、证书、音频资源、RGA/RKNN 库没有进入 `project/app/out`，无法由 SDK 自动打入 OEM。

修改：

- 新增独立应用类型 `DESKBOT_RV1106`，避免开机同时启动 RKIPC 与 DeskBot 争用摄像头。
- 新增 `project/app/deskbot/Makefile`，调用项目现有 CMake/toolchain 交叉构建 `main`。
- 将程序和不可变资源安装到 `/oem/usr/share/deskbot`，将启动器安装到 `/oem/usr/bin`。
- ELF RPATH 收敛为 `$ORIGIN/lib`，去掉开发机绝对路径。
- `RkLunch.sh` 实现 `start/stop/restart/status`、PID 校验、驱动加载检查、日志轮转和启动失败检测。
- 启动器将空参数视为 `start`，与 SDK 自动生成的 `S21appinit -> sh /oem/usr/bin/RkLunch.sh` 调用约定一致；驱动检查同时覆盖 RKCIF、RKISP、SC3336 和 RKNPU。
- 可变配置与日志放在 `/userdata/deskbot`；模型、证书、标签和音频资源从 OEM 建立符号链接，避免重复占用 userdata。
- 将原 `S99start_echo_defconfig` 收敛为 `S20audio_echo`，只负责 codec mixer 初始化，并保证它在 `S21appinit` 前执行；驱动和应用生命周期统一交给 OEM 的 `S21appinit -> RkLunch.sh`，避免重复加载/启动。

验证：

- `./build.sh app` 成功发现 `project/app/deskbot` 并输出到 `output/out/app_out`。
- 主程序为 32 位 ARM EABI5、解释器 `/lib/ld-uClibc.so.0`、已 strip。
- `project/app/deskbot/out` 与 `output/out/app_out` 中主程序 SHA-256 一致。
- OEM 镜像中存在可执行的 `/usr/bin/RkLunch.sh` 和 `/usr/share/deskbot/main`。
- 核对最终 RootFS 中的 `S21appinit`，确认无参数调用与启动器接口一致；脚本通过 `sh -n` 语法检查，运行结果仍待上板验证。

### 3.3 问题三：Buildroot 配置源不唯一

原状态：

- 受 Git 管理的 `sysdrv/tools/board/buildroot/echo_mate_defconfig` 未完整包含 DeskBot 运行库。
- 解压后的 Buildroot `configs/echo_mate_defconfig` 可能由历史构建残留修改。
- `sysdrv/Makefile` 只同步 Luckfox 默认 defconfig，clean build 与 incremental build 可能使用不同配置。

修改：

- 在受控 defconfig 中启用 Opus、PortAudio、libdrm、json-c、jsoncpp、libcurl、websocketpp、Boost、OpenBLAS 和 iw。
- `buildroot_create` 解压时复制 Echo-Mate defconfig。
- 每次 `buildroot` 前都以受控 defconfig 覆盖解压树中的同名配置，使 Git 文件成为唯一配置源。

验证：

- 使用独立 `/tmp` 输出目录执行 Buildroot `defconfig`，所有目标符号均解析为 `y`。
- `./build.sh rootfs` 成功。
- RootFS 中存在 `libopus.so.0`、`libportaudio.so.2`、`libdrm.so.2`、`libjson-c.so.5`、`libjsoncpp.so.25`、`libcurl.so.4`、`libopenblas.so.0`。

### 3.4 问题四：构建凭据和增量 OEM 残留

Wi-Fi：

- 原顶层 `build_app` 要求 BoardConfig 必须包含 SSID/密码，否则跳过全部 app 构建。
- 现在只有显式提供凭据时才生成 network block；默认生成不含密码、允许 `wpa_cli` 更新的基础配置。
- 生成文件已加入 `.gitignore`，避免把本地凭据误提交。

OEM：

- 原 `__PACKAGE_OEM` 只 `mkdir -p`，改变 IQ 选择后旧 SC4336/CAC 仍残留在新镜像。
- 现在每次 OEM 打包先重建暂存目录，再复制本次选中的资源。
- 验证结果为 OEM 暂存目录和 ext4 镜像中均只剩 SC3336 IQ JSON。

Kernel modules：

- `modules_install` 的本轮临时目录原本会清理，但复制到 `output/out/sysdrv_out/kernel_drv_ko` 时仍会保留旧 `.ko`。
- 现在复制当前模块集合前先重建该 driver 输出目录，避免 defconfig 关闭的 sensor driver 继续进入 OEM。
- 验证当前 Kernel `.config` 和模块输出均只保留 `CONFIG_VIDEO_SC3336=m` / `sc3336.ko`，不再包含 OS04A10、SC4336 或 SC530AI。

## 4. 构建链和产物

```text
BoardConfig-SD_CARD-Buildroot-RV1106_Echo_Mate-DeskMate.mk
  |
  +-- U-Boot: echo_rv1106_uboot_defconfig + rk-emmc.config
  |     +-- idblock.img / uboot.img / download.bin
  |
  +-- Kernel: echo_rv1106_linux_defconfig
  |     +-- rv1106g-echo-mate.dts -> DTB
  |     +-- Image / zImage / vmlinux / boot.img
  |     +-- sc3336.ko / video_rkcif.ko / video_rkisp.ko / rknpu.ko
  |
  +-- Buildroot: echo_mate_defconfig
  |     +-- rootfs_uclibc_rv1106.tar -> rootfs.img
  |
  +-- App: DESKBOT_RV1106
  |     +-- project/app/deskbot -> app_out
  |     +-- RkLunch.sh + main + model/resources -> oem.img
  |
  +-- env.img + userdata.img
        |
        +-- update.img
```

本轮最终镜像（构建产物不提交 Git）：

| 产物 | SHA-256 |
| --- | --- |
| `uboot.img` | `b515c9320b70f1fcdc167e616e9e1428a2d5a5db80e0b22e3acff0ac275b79fb` |
| `boot.img` | `7733db2288e4cdd1ca243a3362b4bf35e6d18666d6ad341244c8e1580eb8dcbd` |
| `oem.img` | `7721ed18b7a92a88a81e56cc25e2a5bb7bd3403307bb108710c9bec1372988ae` |
| `rootfs.img` | `c4388eed7ef58128c10755e2f39026170fd7689989e6aa9734534d09775a622d` |
| `update.img` | `d6a274d0df4d584524a37d6eb90044e34590a940836ec9010cd621c84f4b4c5e` |

说明：ext4 镜像包含随机 UUID 和构建时间，再次封装时哈希可能变化；哈希用于标识本轮待烧录镜像，不表示构建是 bit-for-bit reproducible。

## 5. Kernel Config 的面试证据

当前关键配置：

| 配置 | 值 | 含义 |
| --- | --- | --- |
| `CONFIG_MODULES` | `y` | 支持可加载模块框架 |
| `CONFIG_VIDEO_ROCKCHIP_CIF` | `m` | Rockchip CIF 作为模块构建 |
| `CONFIG_VIDEO_ROCKCHIP_ISP` | `m` | Rockchip ISP 作为模块构建 |
| `CONFIG_VIDEO_SC3336` | `m` | SC3336 sensor driver 作为模块构建 |
| `CONFIG_ROCKCHIP_RKNPU` | `m` | RKNPU driver 作为模块构建 |
| `CONFIG_DRM` | `y` | DRM 核心内建；当前 ST7789V 业务仍走 framebuffer |
| `CONFIG_FB` | `y` | framebuffer 核心内建 |
| `CONFIG_LEDS_CLASS` / `CONFIG_LEDS_GPIO` | `y` | GPIO LED class 内建 |
| `CONFIG_LEDS_TRIGGER_TIMER` | `y` | 支持蓝灯 timer trigger |
| `CONFIG_MMC` / `CONFIG_MMC_DW` | `y` | SD/MMC 启动所需控制器支持内建 |
| `CONFIG_EXT4_FS` | `y` | 根文件系统为 ext4，必须在挂载 rootfs 前可用 |

为什么有的用 `y`，有的用 `m`：

- 启动早期必须使用、且加载模块前就要可用的 MMC 与 ext4 采用 `y`。
- Camera/ISP/NPU 不负责挂载根文件系统，可采用 `m`，便于调整加载顺序、缩短基础内核和独立替换。
- `=m` 不只要求生成 `.ko`，还要求镜像中有模块及正确加载顺序；本轮 `modules_install` 已生成并复制相应模块。

## 6. 新固件上板验证清单

烧录属于设备状态变更，本轮未自动执行。烧录 `update.img` 后应把以下输出保存到 `docs/logs/bsp/<date>/`。

### 6.1 启动与分区

```sh
uname -a
cat /proc/cmdline
cat /proc/mounts
cat /proc/partitions
dmesg | grep -i -E 'mmc|ext4|VFS|rootfs'
```

通过标准：

- cmdline 中包含 `root=/dev/mmcblk1p7 rootfstype=ext4 rk_dma_heap_cma=66M`。
- 根文件系统从 `/dev/mmcblk1p7` 以 ext4 正常挂载。
- `/oem` 与 `/userdata` 正常挂载且可访问。

### 6.2 驱动、probe 和 media graph

```sh
lsmod | grep -E 'sc3336|video_rkcif|video_rkisp|rknpu'
dmesg | grep -i -E 'sc3336|sc4336|sc530ai|rkcif|rkisp|csi|dphy|rknpu|probe'
ls -l /dev/video* /dev/media* /dev/v4l-subdev* 2>/dev/null
media-ctl -p -d /dev/media0
media-ctl -p -d /dev/media1
```

通过标准：

- SC3336 能读取 chip ID 并注册 V4L2 subdev。
- 不再出现 SC4336/SC530AI 的实例化或 probe 日志。
- media graph 保持 `SC3336 -> DPHY -> CSI2 -> RKCIF -> ISP` 闭合链路。

### 6.3 RootFS 和应用服务

```sh
ls -l /oem/usr/bin/RkLunch.sh /oem/usr/share/deskbot/main
/oem/usr/bin/RkLunch.sh status
ps | grep -E '[m]ain|[r]kipc'
tail -n 100 /userdata/deskbot/deskbot.log
ls -l /userdata/deskbot
reboot
```

重启后再次执行 status、ps 和日志检查。通过标准：

- DeskBot 由 `S21appinit` 自动启动且只有一个实例。
- RKIPC 不与 DeskBot 同时占用 camera pipeline。
- `/userdata/deskbot/system_para.conf` 和日志在重启后保留。
- 主程序不出现缺库、模型缺失、camera/NPU driver 未注册等启动错误。

## 7. 面试高频问题与回答要点

### Q1：RV1106 从上电到应用启动的链路是什么？

回答要点：BootROM 加载 idblock/SPL 和 DDR 初始化代码，之后进入 U-Boot；U-Boot 加载包含 Kernel、DTB、resource 的 `boot.img`；Kernel 解压并解析 DTB，初始化 MMC 和 ext4 后挂载 SD 第 7 分区；BusyBox init 按顺序执行 `/etc/init.d/S*`，`S21appinit` 再调用 OEM 中的 `RkLunch.sh` 启动 DeskBot。

### Q2：BoardConfig 在这个 SDK 中起什么作用？

回答要点：它不是内核 `.config`，而是 SDK 顶层产品配置入口；向下选择 chip、启动介质、U-Boot/Kernel/Buildroot defconfig、DTS、分区、IQ 文件、应用类型和打包位置，最终影响多个子构建系统。

### Q3：为什么 rootfs 是 `/dev/mmcblk1p7`？

回答要点：启动介质是 SD，RV1106 上对应 `mmcblk1`；分区表按 env、idblock、uboot、boot、oem、userdata、rootfs 排列，rootfs 是第 7 个分区。构建脚本根据分区定义生成 bootargs，本轮 DTB 中验证到 `root=/dev/mmcblk1p7`。

### Q4：`defconfig` 和 fragment 有什么区别？

回答要点：defconfig 提供完整基线，fragment 只覆盖少量产品差异。U-Boot 本轮先加载 `echo_rv1106_uboot_defconfig`，再合并 `rk-emmc.config`，日志确认它把 `CONFIG_ROCKCHIP_EMMC_IOMUX` 改为 `y`。

### Q5：为什么 MMC 和 ext4 必须设为 `y`，camera 可以是 `m`？

回答要点：挂载 rootfs 前还无法从 rootfs 读取模块，因此启动介质控制器和根文件系统驱动通常必须内建。Camera/CIF/ISP/NPU 在根文件系统挂载后使用，可以模块化，但必须保证 `.ko` 被打包并按依赖顺序加载。

### Q6：设备树是怎么变成 `i2c_client` 的？

回答要点：Kernel unflatten DTB 后，I2C controller driver probe 并注册 adapter；I2C core 遍历 controller 下可用子节点，为 `sc3336@30` 创建 client；再通过 OF compatible 匹配 `i2c_driver`，进入 `sc3336_probe()`。

### Q7：为什么三个不同 sensor 不能都放在 `0x30` 且设为 okay？

回答要点：DTS 描述的是实际装配而不是驱动候选列表。同一 I2C adapter 的地址必须唯一；多个同地址 enabled node 会造成 client 地址冲突和无效 probe。板上确认是 SC3336 后，应只启用它，其他参考节点禁用或移除。

### Q8：你为什么还删除了 endpoint，而不只改 `status`？

回答要点：第一次只禁用 sensor 时，DPHY 输入 endpoint 仍引用被裁剪的远端节点，DTC 给出无效 graph phandle 警告。media graph 的 `remote-endpoint` 应成对闭合，因此同时删除未使用的两条 DPHY 输入和 sensor port。

### Q9：probe 会不会配置 sensor 的全部工作寄存器？

回答要点：通常不会。probe 主要获取 clock/GPIO/regulator，完成上电和 chip ID 检查，初始化 V4L2 subdev、controls 和 media entity。分辨率/帧率对应的 mode register 通常在 stream-on 前写入。

### Q10：设备节点不存在时你怎么排查？

回答要点：先确认硬件供电、reset/PWDN、MCLK 和 I2C ACK；再检查实际 DTB 的 node/status/compatible；检查内核配置和 `.ko`；看总线设备是否创建、driver 是否 match、probe 返回码；最后检查 V4L2 subdev、media link 和 video node。不能一上来只改应用层设备号。

### Q11：`status = "disabled"` 的效果是什么？

回答要点：通用 OF 判断会把该节点视为不可用，正常总线 populate 不会据此创建实际设备，因此不会进入 driver match/probe。它适合在公共 dtsi 中保留候选硬件、由板级 dts 选择启用。

### Q12：为什么要反编译 DTB？看 DTS 不够吗？

回答要点：最终 DTB 是多层 include、条件和构建脚本修改后的实际输入；源码写对但选错顶层 DTS、使用旧 DTB或被 overlay 覆盖都会导致板端不生效。反编译能确认最终节点、status、endpoint 和 bootargs。

### Q13：Buildroot 的 defconfig 为什么要每次同步？

回答要点：解压源码树通常是构建产物，里面的 config 可能被 menuconfig 或旧构建修改。若不从受控文件刷新，clean build 和 incremental build 结果不同。受 Git 管理的板级 defconfig 应是唯一事实源。

### Q14：为什么应用放 OEM，不直接放 rootfs 或 userdata？

回答要点：OEM 适合随固件发布的产品程序、模型和只读资源；rootfs 放通用系统和运行库；userdata 放日志、用户配置和运行状态。这样升级、持久化和空间职责清晰。

### Q15：`S20audio_echo` 和 `S21appinit` 为什么要拆开？

回答要点：`S20audio_echo` 只做板级音频 mixer 初始化，并在应用启动前执行；SDK 打包自动生成的 `S21appinit` 负责 OEM 产品应用生命周期。若两边都加载模块/启动 main，容易重复加载或产生两个进程。

### Q16：为什么不用 `killall main` 管理进程？

回答要点：进程名过于通用，可能误杀其他程序。脚本使用 pidfile，并通过 `/proc/<pid>/exe` 校验它确实指向 `/oem/usr/share/deskbot/main` 后才认为服务存活。

### Q17：为什么 RPATH 使用 `$ORIGIN/lib`？

回答要点：开发机绝对路径在目标板无效且泄露构建环境；`$ORIGIN` 相对可执行文件解析，可让随应用分发的 RKNN/RGA 库放在固定相对目录，同时系统库仍由目标 rootfs 提供。

### Q18：为什么不能把 Wi-Fi 密码写进 BoardConfig？

回答要点：BoardConfig 受版本管理，硬编码凭据会泄露且让构建依赖个人网络。本轮默认生成无 network block 的基础配置，保留运行时配网；仅在显式传入变量时才嵌入凭据。

### Q19：为什么改了资源列表，旧文件还会进入镜像？

回答要点：增量打包只复制新文件而不删除暂存目录，已经取消选择的文件不会自动消失。修复方式是在可恢复的构建输出目录上执行 clean staging，再按本次配置完整重建 OEM；之后同时检查目录和 ext4 镜像内容。

### Q20：你如何证明生成的程序能在 RV1106 上运行？

回答要点：主机侧可以证明它是 ARM EABI5、使用目标 uClibc loader、动态依赖在 rootfs/OEM 中闭环、RPATH 正确且已进入镜像；但“能在板上完整运行”仍必须通过真实烧录、启动日志、设备节点、media graph 和业务功能验证。要主动区分 build success 与 runtime success。

## 8. 面试中不能越过的边界

- 不说“开发了 SC3336 驱动”；应说“基于 SDK 已有驱动完成板级 DTS、配置、资源和 pipeline 适配”。
- 不说“实现了 Rockchip CIF/ISP/NPU 驱动”；这些是厂商驱动，本人完成的是选择、模块打包、加载和验证链路。
- 不说“本轮新固件已上板验证”；当前只有旧基线板端日志与新镜像主机侧验证。
- 不把 DTC/编译成功等同于 sensor probe 或 stream 成功。
- 不把 ext4 镜像 SHA-256 当作稳定可复现指标；它只标识本次具体产物。
- `compatible = "rockchip,rv1103g-38x38-ipc-v10", "rockchip,rv1106"` 是继承的板级标识，面试若问应说明第二项标识 SoC 兼容，第一项仍需结合硬件版本继续核对，不要虚构修改理由。

## 9. 下一步完成条件

第一条在以下条件全部满足后才能标记为完整闭环：

1. 烧录本轮 `update.img`。
2. 保存完整 U-Boot 和 Kernel 启动串口日志。
3. 确认 SD 第 7 分区挂载 rootfs，OEM/userdata 挂载正常。
4. 确认只有 SC3336 probe，media graph 闭合且能持续取帧。
5. 确认 DeskBot 开机自启、stop/restart/status 正常，重启后配置与日志持久化。
6. 连续运行并检查无新增 kernel oops、camera timeout、NPU fault 或应用反复重启。
