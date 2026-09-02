# RV1106 + ST7789V：从 FB/fbtft 迁移到 DRM/TinyDRM/MIPI-DBI 技术复盘

> 审计日期：2026-08-28；目标分支：`feat/st7789v-drm`；内核：Linux 5.10.110
>
> 迁移提交：`2b0fb8b38`、`aad007f4a`、`917685b59`；硬件：RV1106 Echo-Mate、SPI0 CS0、ST7789V、320×240 逻辑分辨率
>
> 文档目的：解释这次迁移真实做了什么、为什么这样做、怎样验证，以及哪些结论目前仍不能下。

---

## 0. 阅读约定与结论边界

本文统一使用以下证据标签：

- **【代码中确认】**：当前仓库源码可以直接证明。
- **【Git diff确认】**：相关迁移提交或提交间 diff 可以直接证明。
- **【运行日志确认】**：板端 dmesg、sysfs、进程 fd、性能原始数据或人工验收记录可以证明。
- **【根据 Linux 机制推导】**：不是本项目日志直接打印的结论，但可由 Linux/DRM/SPI 的确定机制解释。
- **【当前证据不足】**：存在现象或合理假设，但缺少足够实验关闭结论。

工作区在审计时还有摄像头/BSP、启动脚本和其他文档的未提交改动。**【Git diff确认】** 当前未提交的 DTS/defconfig 改动没有新增 ST7789V 显示差异；本文以三个已提交迁移 commit 和已归档板端数据为显示迁移事实基线，不把其他脏工作区内容混入本次成果。

### 一句话结论

**【代码中确认】【Git diff确认】【运行日志确认】** 本项目把 ST7789V 从 `fbdev → fbtft → fb_st7789v` 的传统路径，迁移为 `DRM/KMS → DRM tiny ST7789V driver → drm_mipi_dbi → SPI`，并把 DeskBot/LVGL 从 `/dev/fb0` 切换为直接打开 `/dev/dri/card0`。迁移完成了驱动、Kconfig/Kbuild、DT Binding、DTS、defconfig、libdrm 和 LVGL backend 的整套接入，解决了 16-bit SPI 奇数分片导致的 `-EINVAL` 花屏问题，最终通过 320×240、RGB565、方向/颜色/offset、重复 YOLO 生命周期和约 30 分钟持续刷新验证。

**【当前证据不足】** 这次迁移不能被包装成“DRM 天然消除撕裂”或“性能确定提升 XX%”：屏幕没有已验证的 TE/真实 vblank 同步，性能也只有一轮 A/B，且旧 fbtft 开启 `debug=7`，会污染系统开销。

---

## 1. 此次迁移的实际修改清单

### 1.1 Git 提交范围

| 提交 | 真实作用 | 主要证据 |
| --- | --- | --- |
| `2b0fb8b38 docs(drm): capture ST7789V framebuffer baseline` | 固化旧 FB/fbtft 运行链路、显示参数、Home/YOLO 性能基线和采集工具 | **【Git diff确认】** 41 个文件，主要位于 `docs/`；没有修改显示驱动 |
| `aad007f4a feat(drm): add ST7789V mipi-dbi display driver` | 增加 DRM tiny 驱动、Kconfig/Kbuild、Binding；切 DTS compatible 和 defconfig；修复 mipi-dbi SPI 分片 | **【Git diff确认】** 12 个文件，新增 `st7789v.c`，关闭 `FB_TFT` |
| `917685b59 feat(deskbot): switch LVGL display to DRM` | 将 ARM 端 LVGL 后端切到 linux_drm，接入 libdrm，归档板端功能/性能验证 | **【Git diff确认】** 应用配置、CMake、Buildroot libdrm 和板端日志 |

### 1.2 内核显示改动

**【Git diff确认】** 驱动提交实际涉及：

- 新增 `drivers/gpu/drm/tiny/st7789v.c`；
- 在 `drivers/gpu/drm/tiny/Kconfig` 增加 `CONFIG_TINYDRM_ST7789V`；
- 在 `drivers/gpu/drm/tiny/Makefile` 增加 `obj-$(CONFIG_TINYDRM_ST7789V) += st7789v.o`；
- 新增 `Documentation/devicetree/bindings/display/sitronix,st7789v-dbi.txt`；
- 把 DTS 节点从 `fbtft@0` / `sitronix,st7789v` 改为 `display@0` / `sitronix,st7789v-dbi`；
- defconfig 增加 `CONFIG_TINYDRM_ST7789V=y`，并设置 `# CONFIG_FB_TFT is not set`；
- 在 `drm_mipi_dbi.c` 的 SPI 分片函数中把最大分片向下对齐到 2 bytes。

### 1.3 用户态改动

**【Git diff确认】【代码中确认】** 应用提交实际涉及：

- ARM 配置从 `LV_USE_LINUX_FBDEV=1` 切为 `LV_USE_LINUX_DRM=1`；
- 保留 x86 SDL 仿真，并增加“FBDEV/DRM/SDL 恰好启用一个”的编译期检查；
- `main.c` 默认从 `/dev/dri/card0` 创建 LVGL DRM display；
- CMake 要求 ARM sysroot 提供 libdrm，并把 libdrm include path 加到实际编译 `lv_linux_drm.c` 的 `lvgl` target；
- Buildroot 增加 `BR2_PACKAGE_LIBDRM=y`。

### 1.4 没有做过或不能证明做过的事

- **【当前证据不足】** 没有 `modetest` 实测结果。`docs/ROADMAP.md` 只记录了计划命令；实际验收使用 sysfs、进程 fd、应用显示和人工观察。
- **【代码中确认】** 新 ST7789V probe 没有获取 panel regulator，Binding 也没有 supply 属性；不能写成“完成 regulator 接入”。面板供电在当前实现里被视为板级常供电或由其他逻辑管理。
- **【当前证据不足】** 没有逻辑分析仪捕获，60 MHz 是 DTS 配置上限，不是实测 SCLK；reset/DC 的物理波形也未直接测量。
- **【当前证据不足】** 可读取的 shell history 中没有找到可补充本次迁移的命令记录；本文不把 shell history 当证据。
- **【当前证据不足】** 没有独立、可审计的 Codex 会话操作流水；实际依据是 Git、源码、构建记录和已归档板端日志。

---

## 2. 项目背景：为什么值得从 FB 迁移到 DRM

### 2.1 原系统是什么

**【代码中确认】【运行日志确认】** 迁移前 ARM 配置启用 LVGL linux_fbdev，应用打开 `/dev/fb0`。设备树中 SPI0 CS0 的节点 compatible 为 `sitronix,st7789v`，最终匹配 staging 目录下的 `fb_st7789v`；板端 `spi0.0/driver` 指向 `fb_st7789v`，没有 `/dev/dri`。

原链路可以概括为：

```text
LVGL / Home / YOLO 已绘制 RGB565 画面
                  │
                  ▼
             /dev/fb0
                  │
                  ▼
        Linux framebuffer core
                  │
                  ▼
       fbtft 通用小屏辅助层
                  │
                  ▼
      fb_st7789v 面板专用逻辑
                  │
                  ▼
      Linux SPI core + Rockchip SPI0
                  │
                  ▼
            ST7789V GRAM/LCD
```

### 2.2 Linux Framebuffer 和 `/dev/fb0` 到底是什么

**【根据 Linux 机制推导】** Linux framebuffer（fbdev）是一套较老的显示设备接口。驱动注册一个 `struct fb_info`，提供分辨率、像素格式、stride、显存和 `fb_ops`；framebuffer core 再向用户态暴露 `/dev/fbN`。

**【根据 Linux 机制推导】** `/dev/fb0` 不是“LCD 本身”，而是某个内核 framebuffer 对象的字符设备入口。应用对它 `write()` 或 `mmap()`，操作的是驱动提供的 framebuffer 内存；什么时候、怎样把这些内存发送到物理屏幕，由具体驱动决定。

**【代码中确认】** 本项目 fbtft 分配一块 `vzalloc()` 的 RGB565 framebuffer，注册 `fb_read/fb_write/fillrect/copyarea/imageblit`，并启用 deferred I/O。内存被写脏后，fbtft 合并脏行并调度延迟工作，设置 ST7789V 的列/页窗口，再调用 `fbtft_write_vmem16_bus8()`；后者把 little-endian RGB565 转成线上高字节优先，最后通过 SPI 发往面板。

三层职责是：

- **fbdev core**：提供 `/dev/fb0`、`fb_info`、mmap/write 等通用 ABI；
- **fbtft**：为 SPI/并口小屏提供 framebuffer 分配、deferred I/O、GPIO、地址窗口、刷新和 SPI 写入等通用能力；
- **fb_st7789v**：提供 ST7789V 的初始化寄存器、rotation/MADCTL、gamma 和 blank 等面板差异。

### 2.3 原路径能工作，为什么仍要迁移

原方案并非“错误方案”。**【运行日志确认】** 它已经能稳定显示 320×240 RGB565，颜色、方向、offset 正常，Home/YOLO 可工作。迁移的价值主要是显示栈现代化和可维护性，而不是为了制造一个“旧方案完全不可用”的前提。

主要动机如下：

1. **统一现代用户态接口。** **【根据 Linux 机制推导】** DRM/KMS 提供标准设备枚举、mode、connector、plane、framebuffer object、buffer allocation 和 atomic commit；应用不再只面对一块没有明确显示拓扑的线性 `/dev/fb0`。
2. **复用内核公共显示框架。** **【代码中确认】** `drm_mipi_dbi` 已经处理 GEM framebuffer、RGB565/XRGB8888 转换、damage clip、窗口设置、SPI command/data 和 simple display pipe；ST7789V 驱动只保留面板差异。
3. **应用可直接使用 KMS buffer。** **【代码中确认】** 当前 LVGL backend 创建两个 DRM dumb buffer，使用 direct render 和 atomic commit，而不是继续依赖 fbtft deferred I/O。
4. **更容易诊断和扩展。** **【运行日志确认】** 迁移后可以从 `/sys/class/drm/card0-SPI-1` 读取 connector 状态与 mode，并通过 `/dev/dri/card0` 使用标准 libdrm API。未来若做 dma-buf/RGA 集成，会有更合适的接口基础；但这次尚未完成 RGA→DRM 零拷贝。
5. **淘汰 staging fbtft 的项目依赖。** **【Git diff确认】** 目标内核关闭了 `CONFIG_FB_TFT`，ST7789V 不再由 staging fbtft 管理。

### 2.4 为什么没有 GPU 的 SPI 小屏也能用 DRM

**【根据 Linux 机制推导】** DRM 不等于 GPU，也不只服务 HDMI/MIPI-DSI。DRM/KMS 的核心是“管理显示对象、buffer 和显示状态”。ST7789V 内部有 GRAM，内核可以把 GEM framebuffer 中的像素通过 SPI 写入 GRAM，因此仍然能够抽象成一个固定 mode、一个 connector、一个 primary plane 和一条简单 display pipe。

这块屏没有硬件 scanout engine 去持续读取系统内存。**【根据 Linux 机制推导】** 所谓 plane 更新或 page flip，最终仍会触发一次 CPU/SPI 像素搬运；它不是 HDMI/VOP 那种把 framebuffer 地址交给显示控制器后自动扫描输出。

### 2.5 DRM 不自动等于“无撕裂”

**【运行日志确认】** 用户在迁移前后都观察到“无明显撕裂”，说明当前场景视觉可接受。

**【当前证据不足】** DTS 和驱动没有 TE GPIO，日志也没有真实 vblank/面板扫描同步证据。DRM atomic commit 能保证一次状态变更的一致性，LVGL page-flip event 能同步提交生命周期，但不能证明 SPI 写 GRAM 与 LCD 面板扫描完全同步。因此正确表述是：

> 迁移引入了标准 atomic state/buffer 提交机制，并在当前场景未观察到明显撕裂；但没有 TE 或外部测量，不能声称 DRM 从物理层彻底消除了撕裂。

---

## 3. 迁移前后的完整软件架构

### 3.1 迁移前：直接 fbdev/fbtft

```text
┌──────────────────────────────────────────────┐
│ 用户态：DeskBot / LVGL / Home / YOLO         │
│ LVGL linux_fbdev，RGB565，mmap/write fb0     │
└──────────────────────┬───────────────────────┘
                       │ /dev/fb0
┌──────────────────────▼───────────────────────┐
│ Linux 通用：Framebuffer Core                 │
│ fb_info / fb_ops / mmap / write              │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Linux staging：fbtft                         │
│ vmem、deferred I/O、dirty lines、窗口和刷帧  │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ 面板专用：fb_st7789v                         │
│ reset/init/MADCTL/gamma/blank                │
└──────────────────────┬───────────────────────┘
                       │ SPI transfer
┌──────────────────────▼───────────────────────┐
│ Linux SPI Core + Rockchip RV1106 SPI0 驱动   │
└──────────────────────┬───────────────────────┘
                       │ CS0 + MOSI + SCLK + DC
┌──────────────────────▼───────────────────────┐
│ 硬件：ST7789V controller + LCD panel         │
└──────────────────────────────────────────────┘
```

### 3.2 阶段二过渡状态：内核已 DRM，应用仍走 fb0

```text
LVGL linux_fbdev
      │
      ▼
/dev/fb0（DRM fbdev emulation，不是 fbtft）
      │
      ▼
DRM framebuffer helper / GEM CMA
      │
      ▼
st7789v-dbi + drm_mipi_dbi + SPI0 + ST7789V
```

**【运行日志确认】** 此时 `/sys/class/graphics/fb0/name` 已是 `st7789v-dbidrmf`，不是 `fb_st7789v`。这一步允许先验证内核驱动，再切应用 backend，降低一次性变更多层带来的排障复杂度。

### 3.3 迁移后：LVGL 直接使用 DRM/KMS

```text
┌──────────────────────────────────────────────────────┐
│ 用户态：DeskBot / LVGL linux_drm / libdrm           │
│ RGB565 direct render；两个 dumb buffer；atomic commit│
└─────────────────────────┬────────────────────────────┘
                          │ /dev/dri/card0
┌─────────────────────────▼────────────────────────────┐
│ DRM Core / KMS / Atomic Helper                       │
│ drm_device、GEM、framebuffer object、atomic state    │
└─────────────────────────┬────────────────────────────┘
                          │
┌─────────────────────────▼────────────────────────────┐
│ DRM simple display pipe                              │
│ primary plane + CRTC + fixed SPI connector           │
└─────────────────────────┬────────────────────────────┘
                          │ update / damage
┌─────────────────────────▼────────────────────────────┐
│ Linux 通用 drm_mipi_dbi                              │
│ GEM CMA 映射、格式转换、dirty rect、DCS 窗口、flush  │
└─────────────────────────┬────────────────────────────┘
                          │ command/data
┌─────────────────────────▼────────────────────────────┐
│ 面板专用 st7789v-dbi DRM tiny driver                 │
│ mode/reset/init/MADCTL/gamma/backlight/probe         │
└─────────────────────────┬────────────────────────────┘
                          │ MIPI DBI Type C option 3
┌─────────────────────────▼────────────────────────────┐
│ Linux SPI Core + Rockchip RV1106 SPI0 驱动           │
└─────────────────────────┬────────────────────────────┘
                          │ 8-bit command / 16-bit pixel
┌─────────────────────────▼────────────────────────────┐
│ 硬件：ST7789V GRAM + LCD panel                       │
└──────────────────────────────────────────────────────┘

兼容旁路（仍存在但 DeskBot 不使用）：
DRM fbdev emulation ──► /dev/fb0 = st7789v-dbidrmf
```

框架归属：

- **Linux 通用框架**：Device Tree、SPI core、GPIO、backlight、CMA/DMA；
- **DRM 提供**：DRM device、KMS 对象、GEM、framebuffer object、atomic helper、simple display pipe、fbdev emulation；
- **MIPI DBI 公共层提供**：DCS command/data、dirty region、像素转换和 SPI flush；
- **本项目 ST7789V 驱动提供**：固定 mode、reset、初始化寄存器、rotation/MADCTL、gamma 和 probe glue；
- **SoC-specific**：Rockchip RV1106 SPI controller 驱动；
- **真实硬件**：SPI0、GPIO、PWM9 背光、ST7789V 和 LCD panel。

---

## 4. DRM/TinyDRM/MIPI-DBI 方案选择

### 4.1 DRM 提供了哪些现代抽象

**【根据 Linux 机制推导】** 与“一个可写 framebuffer 设备”相比，DRM/KMS 把显示拆成明确对象：

- `drm_device`：整张显示卡；
- connector：连接到哪块显示设备；
- CRTC：一条正在输出的显示管线状态；
- plane：使用哪个 framebuffer、源/目标区域；
- framebuffer object：描述宽高、pitch、format 与 GEM buffer 的绑定；
- GEM：内核管理 buffer object 生命周期与 mmap；
- atomic state/commit：一次性检查并提交 connector/CRTC/plane 状态。

**【代码中确认】** 本项目驱动声明 `DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC`，并使用 GEM CMA helper、simple display pipe 和一个 SPI connector。

### 4.2 TinyDRM/DRM tiny 解决什么

**【根据 Linux 机制推导】** 大型显示控制器可能有多 CRTC、多 plane、硬件 scaler、encoder 和复杂时序；SPI 小屏通常只有固定 mode、一个 framebuffer 和“把像素写进面板 GRAM”这一条路径。DRM tiny driver 使用 DRM 标准接口，但通过 helper 把这种简单硬件所需的样板代码压缩到很小。

这里“tiny”指驱动和硬件模型简单，不表示功能是假 DRM，也不表示一定是内核模块。

### 4.3 ST7789V 为什么适合 MIPI DBI

**【代码中确认】** ST7789V 的控制方式是命令 + 参数/像素数据，例如：

- `0x11`：Sleep Out；
- `0x3A`：pixel format；
- `0x36`：MADCTL；
- `0x2A/0x2B`：列/页地址窗口；
- `0x2C`：开始写显示内存。

**【根据 Linux 机制推导】** 这正符合 MIPI Display Bus Interface 中“处理器向显示控制器发送命令/数据并写入 framebuffer/GRAM”的模型。

**【代码中确认】** 旧 `fb_st7789v` 本身也使用 `MIPI_DCS_*` 命令常量；迁移并不是把屏幕电气接口从“SPI 改成另一根 MIPI 总线”，而是把同一套 SPI command/data 硬件交互从 fbtft 私有刷新框架接入 DRM 的通用 MIPI-DBI/KMS 模型。

### 4.4 SPI 和 MIPI DBI 的正确关系

不能说“MIPI DBI 就是 SPI”。

- **SPI** 描述串行总线传输：时钟、数据、片选、bits-per-word、频率和 transfer；
- **MIPI DBI** 描述处理器与显示控制器的命令/数据接口及 DCS 语义；
- **本项目** 使用 **MIPI DBI Type C option 3**：普通 8-bit SPI 加一根独立 DC GPIO。DC 低表示 command，DC 高表示 parameters/pixels。

通俗地说：**SPI 是运输卡车和道路，MIPI DBI/DCS 是货物的包装与指令格式，ST7789V 驱动则决定这块屏具体要收哪些指令。**

### 4.5 为什么参考 ili9341，而不是复制 ili9341 初始化表

**【代码中确认】** 新驱动 probe 结构参考内核已有 mipi-dbi tiny driver 的组织方式：分配 DRM 对象、获取 GPIO/背光、初始化 DBI SPI、创建 simple pipe、注册 DRM、建立 fbdev emulation。

**【代码中确认】** 面板初始化值没有照搬其他模组，而是以本板已验证的 `fb_st7789v` 为基准。这样复用的是“Linux DRM 接入框架”，不是错误复用“另一个面板的电气参数”。

---

## 5. FB → DRM 完整迁移过程

### 5.1 阶段一：固化旧 FB 链路和不可丢失的显示参数

#### 问题

如果直接改驱动而没有基准，一旦出现颜色交换、方向错误、整体偏移或花屏，就无法区分是新框架问题、初始化表问题还是板级 GPIO/SPI 问题。

#### 原因

**【代码中确认】** ST7789V 驱动能否点亮不仅由分辨率决定，还依赖 reset 极性、DC 语义、COLMOD、MADCTL、RGB/BGR、gamma、地址窗口和线上字节序。

#### 修改

**【Git diff确认】** 提交 `2b0fb8b38` 没改驱动，新增采集脚本、汇总脚本、旧链路源码审计和 Home/YOLO 原始数据。

#### 确认的迁移基准

| 项目 | 旧 FB 基准 | 证据 |
| --- | --- | --- |
| SPI | SPI0、CS0、`spi-max-frequency=60000000` | **【代码中确认】【运行日志确认】** |
| 接口 | 8-bit SPI + DC GPIO | **【代码中确认】** |
| DC | GPIO1_D0；物理低 command、物理高 data | **【代码中确认】【运行日志确认】** |
| reset | GPIO1_C4，active-low；物理低 20～40 µs，释放后 120 ms | **【代码中确认】** |
| native mode | 240×320 | **【代码中确认】** |
| rotation | 270°，MADCTL=`MV|MX`=`0x60` | **【代码中确认】【运行日志确认】** |
| logical mode | 320×240、16 bpp、stride 640 | **【运行日志确认】** |
| 全帧 | 153600 bytes | **【代码中确认】【运行日志确认】** |
| RGB/BGR | RGB565，BGR bit 未设置 | **【代码中确认】【运行日志确认】** |
| offset | 0/0；窗口 X=0..319、Y=0..239 | **【运行日志确认】** |
| inversion | 未开启 | **【代码中确认】** |
| 背光 | 独立 PWM9 `pwm-backlight` | **【代码中确认】** |

#### 关键初始化值

**【代码中确认】** 实际有效分支包含：

```text
HW reset
11                         Sleep Out
delay 120 ms
3A 05                      RGB565
B2 08 08 00 22 22          PORCTRL
B7 35                      GCTRL
C2 01 FF                   VDV/VRH enable
C3 0B                      VRHS
C4 20                      VDVS
BB 20                      VCOMS
C5 20                      VCOM offset
D0 A4 A1                   Power control
29                         Display On
36 60                      rotation 270, RGB order
E0 ...                     positive gamma
E1 ...                     negative gamma
```

旧 fbtft 注册流程是 init → MADCTL → 首次全帧 → gamma；新驱动是 init/MADCTL/gamma → 首次 flush。**【代码中确认】** 两者保留了面板寄存器参数，但完整调用顺序并非逐事件完全相同，因此准确说法是“保持已验证寄存器值和关键时序”，不是“整个生命周期逐字节完全相同”。

#### 结果

**【运行日志确认】** 旧链路为 `/dev/fb0 → fb_st7789v → spi0.0`，没有 `/dev/dri`；颜色、方向、offset 正常，无明显撕裂、闪烁、花屏。

### 5.2 阶段二：内核驱动迁移到 DRM/mipi-dbi

#### 问题

需要让同一个 SPI ST7789V 变成标准 DRM/KMS device，同时避免旧 fbtft 与新驱动争用。

#### 原因

仅新增一个 `st7789v.c` 不会自动进入内核，也不会自动匹配 DTS；必须打通 Kconfig、Kbuild、Device Tree、SPI driver match、DRM register 和用户态节点创建的完整链路。

#### 修改

**【Git diff确认】** 提交 `aad007f4a`：

1. 新增 ST7789V DRM tiny driver；
2. 复用 `mipi_dbi_spi_init()`、`mipi_dbi_dev_init()`、`mipi_dbi_pipe_update()`；
3. 增加 Kconfig/Makefile/DT Binding；
4. compatible 切为 `sitronix,st7789v-dbi`；
5. 关闭 `CONFIG_FB_TFT`；
6. 保留 `CONFIG_FB=y` 和 DRM fbdev emulation；
7. 修复 mipi-dbi 的 16-bit SPI 分片对齐。

#### 结果

**【运行日志确认】** 修复版启动后：

```text
[drm] Initialized st7789v-dbi ... for spi0.0 on minor 0
/sys/bus/spi/devices/spi0.0/driver -> .../st7789v-dbi
/sys/class/graphics/fb0/name = st7789v-dbidrmf
/dev/dri/card0
card0-SPI-1/status = connected
card0-SPI-1/modes = 320x240
```

阶段二应用仍可通过 `/dev/fb0` 工作，但该 fb0 已是 DRM fbdev emulation。

### 5.3 阶段三：DeskBot/LVGL 直接切到 DRM

#### 问题

如果应用仍打开 `/dev/fb0`，内核虽已 DRM 化，用户态仍没有真正使用 KMS/dumb buffer/atomic commit。

#### 修改

**【Git diff确认】** 提交 `917685b59` 切换 LVGL backend、补齐 libdrm 编译/运行依赖，并归档应用二进制与板端验证。

#### 结果

**【运行日志确认】** 最终 DeskBot 二进制 SHA-256 为 `f74590f30d4d2027b2a8f9b1ae7cfcdc401e3713922180747acdee1737c9cf8e`；进程 fd 3 指向 `/dev/dri/card0`，maps 中有两个 card0 映射，未持有 `/dev/fb0`。Home/YOLO 画面和页面生命周期正常，持续刷新约 30 分钟通过。

---

## 6. ST7789V DRM Driver 源码分析

### 6.1 驱动保留的 panel-specific 内容

**【代码中确认】** `drivers/gpu/drm/tiny/st7789v.c` 主要负责：

- 定义 native mode 240×320、offset 0/0、RGB 顺序、write-only；
- 硬复位时序；
- ST7789V 初始化寄存器和 gamma；
- 根据 DTS rotation 生成 MADCTL；
- 在 pipe enable 时初始化面板、首帧 flush、开启背光；
- 组织 SPI driver、OF match 和 DRM 注册流程。

### 6.2 probe 执行顺序：SPI LCD 如何变成 DRM device

#### 1. `device_get_match_data()`

- 做什么：从 OF match 或 SPI ID 取得 `st7789v_cfg`。
- 为什么：把 fixed mode、offset、RGB/BGR、write-only 等板型配置交给 probe。
- 没有会怎样：驱动不知道要创建什么 mode 或采用什么面板差异。

#### 2. `devm_drm_dev_alloc()`

- 做什么：按 `st7789v_driver` 分配并初始化受设备生命周期管理的 `drm_device` 和私有对象。
- 为什么：后续 GEM、mode_config、connector、pipe 都必须挂在这个 DRM device 上。
- 没有会怎样：无法注册 `/dev/dri/cardN`，也没有 DRM 对象容器。

#### 3. 获取 reset 和 DC GPIO

**【代码中确认】** reset 使用 `GPIOD_OUT_HIGH`，DC 使用 `GPIOD_OUT_LOW`。

- reset：active-low descriptor 中逻辑 1 表示物理低，即保持复位；pipe enable 时逻辑 1→0，形成物理低脉冲再释放。
- DC：Type C option 3 必需，用来区分 command 与 data。
- 没有会怎样：reset/DC 获取失败会直接终止 probe；没有 DC 就不是当前硬件连接方式。

#### 4. `devm_of_find_backlight()`

- 做什么：通过 DTS `backlight = <&backlight>` 找到 PWM backlight device。
- 为什么：初始化和首帧期间先灭背光，flush 完整画面后再开启，减少上电过程可见花屏。
- 没有会怎样：按当前代码，找不到所引用背光会返回错误；若完全无背光引用，helper 行为取决于查找结果和内核实现。

#### 5. 读取 `rotation`

- 做什么：读取 DTS 的 270°。
- 为什么：一方面 `drm_mipi_dbi` 把 fixed mode 从 240×320 旋转为用户态看到的 320×240，另一方面 panel init 设置 MADCTL=`0x60`。
- 没有会怎样：mode 几何和面板地址方向可能与实物安装方向不一致。

#### 6. `mipi_dbi_spi_init()`

- 做什么：绑定 SPI device，设置 32-bit DMA mask，选择 Type C option 3 command 函数，设置 DC、读取能力、16-bit word/字节交换策略和 command mutex。
- 为什么：把普通 SPI transfer 变成具有 MIPI DCS command/data 语义的传输接口。
- 没有会怎样：`dbi->command` 为空，后续 DBI 初始化无法工作。

#### 7. 标记 write-only

**【代码中确认】** `dbi->read_commands = NULL`。

- 做什么：禁用 DBI read command。
- 为什么：本板显示连接按 write-only 使用，不依赖 MISO 读取控制器寄存器。
- 没有会怎样：公共 helper 可能尝试不可靠或硬件未连接的读操作。

#### 8. 设置 offset

**【代码中确认】** left/top 都是 0。

- 做什么：为后续 column/page address 增加 panel-specific 偏移。
- 为什么：部分 ST7789 模组可视区域不从 GRAM 0/0 开始。
- 本项目结果：基线已经证明无需偏移，因此保持 0/0。

#### 9. `mipi_dbi_dev_init()`

- 做什么：初始化 mode_config，分配全屏 tx buffer，旋转 fixed mode，创建 SPI connector 和 simple display pipe，启用 plane damage clips，并声明 RGB565/XRGB8888。
- 为什么：这是从“会发送 DBI 命令”到“拥有 KMS 对象”的关键桥梁。
- 没有会怎样：即使 SPI 初始化成功，也不会形成 connector/CRTC/plane/framebuffer 更新模型。

#### 10. `drm_mode_config_reset()`

- 做什么：为 connector/CRTC/plane 建立初始 atomic state。
- 为什么：注册给用户态前必须有一致的 KMS 初始状态。
- 没有会怎样：atomic property/state 不完整，后续 modeset/commit 不能按标准流程工作。

#### 11. `drm_dev_register()`

- 做什么：把已构造完成的 DRM device 发布给 DRM core 和用户态。
- 为什么：这是出现 `/dev/dri/card0` 的关键注册点。
- 没有会怎样：内部对象即使已分配，用户态也看不到 card device。

#### 12. `drm_fbdev_generic_setup()`

- 做什么：在 DRM device 之上建立 fbdev compatibility layer。
- 为什么：允许旧 `/dev/fb0` 应用在阶段二继续工作，也便于兼容/诊断。
- 没有会怎样：直接 DRM 应用仍可工作，但不会得到当前的 `st7789v-dbidrmf` `/dev/fb0`。

### 6.3 probe 中没有 regulator

**【代码中确认】** 虽然 `mipi_dbi_dev` 通用结构和 disable helper 支持 `regulator`，本项目 `st7789v_probe()` 没有调用 regulator get，也没有在 Binding 声明 supply。因此当前驱动只管理 reset、DC 和 backlight，不管理 panel 电源 rail。

**【根据 Linux 机制推导】** 这意味着 panel 供电必须在别处保证。面试中应说“当前板级电源为既有条件，驱动未建模 regulator”，不能把 generic helper 的能力说成本项目已经实现。

### 6.4 enable/update/disable

- **enable**：先关背光，执行 reset 和 ST7789V init，再调用 `mipi_dbi_enable_flush()` 全屏更新并开背光；
- **update**：直接复用 `mipi_dbi_pipe_update()`，合并 damage 后只刷新脏矩形；
- **disable**：复用 `mipi_dbi_pipe_disable()`，关闭背光；无背光时会刷黑。

**【代码中确认】** `prepare_fb` 使用 GEM framebuffer helper，为 display pipe 准备 buffer。

---

## 7. drm_mipi_dbi 为什么让我们不用从零写 DRM 驱动

### 7.1 如果完全自己写，需要处理什么

如果不使用公共层，panel driver 至少需要正确处理：

- DRM mode_config、connector、plane、CRTC 和 atomic callbacks；
- GEM framebuffer 的查找、mmap 和格式；
- RGB565/XRGB8888 转换和端序；
- dirty rect 合并；
- ST7789V column/page address；
- command/data DC 切换；
- SPI 控制器最大 transfer 分片；
- 首帧 flush、backlight、disable/blank；
- framebuffer object 和 simple pipe 的生命周期。

这些大部分不是 ST7789V 独有逻辑。

### 7.2 公共层实际帮了什么

#### DRM/KMS 对象搭建

**【代码中确认】** `mipi_dbi_dev_init_with_formats()` 完成 mode_config、fixed SPI connector、simple display pipe、damage clips 和 mode 边界。

#### GEM/CMA framebuffer 访问

**【代码中确认】** `mipi_dbi_fb_dirty()` 从 DRM framebuffer 取得 GEM CMA object；全屏 RGB565 且无需 swap 时可直接使用 CMA vaddr，否则复制到 tx buffer。

#### 像素格式转换

**【代码中确认】** `mipi_dbi_buf_copy()` 支持原生 RGB565，也支持 XRGB8888→RGB565，并可执行字节交换。

#### dirty region

**【代码中确认】** `mipi_dbi_pipe_update()` 使用 atomic damage merge 得到更新矩形；`mipi_dbi_fb_dirty()` 只为该矩形设置窗口并发送 `width × height × 2` bytes。

#### DCS 地址窗口和写 GRAM

**【代码中确认】** 公共层发送：

```text
MIPI_DCS_SET_COLUMN_ADDRESS (0x2A)
MIPI_DCS_SET_PAGE_ADDRESS   (0x2B)
MIPI_DCS_WRITE_MEMORY_START (0x2C)
```

#### SPI Type C option 3

**【代码中确认】** command 时 DC=0、8 bpw；parameters/data 时 DC=1。对于 `WRITE_MEMORY_START` 且不需要 swap 的像素数据，公共层选择 16 bpw。

**【代码中确认】** `mipi_dbi_spi_cmd_max_speed()` 对不超过 64 bytes 的短命令/参数使用 `min(10 MHz, spi-max-frequency)`；长像素数据返回 0，表示使用 SPI device 的默认上限 60 MHz。因此迁移后的准确说法是“短控制命令最高 10 MHz，长像素传输按 DTS 60 MHz 上限”，而不是所有传输都固定 60 MHz。**【当前证据不足】** 两者都是软件配置值，尚无逻辑分析仪实测波形。

#### 首帧与背光

**【代码中确认】** `mipi_dbi_enable_flush()` 先全帧更新，再打开背光。

### 7.3 ST7789V 驱动最终只需要负责什么

本项目专用驱动保留：

- native mode 与 offset；
- active-low reset 的板级正确时序；
- ST7789V init sequence；
- COLMOD/RGB565；
- MADCTL、rotation、RGB/BGR；
- gamma 和是否 inversion；
- write-only 属性；
- SPI/OF match 和 probe glue。

### 7.4 20 秒面试解释

> `drm_mipi_dbi` 是 DRM 给 SPI 命令式小屏提供的“通用显示管线适配层”：它把 GEM framebuffer、simple pipe、脏矩形、RGB565 转换、DCS 地址窗口和 SPI 刷新都做好；我的 ST7789V 驱动只需要描述屏幕模式、复位、初始化寄存器和旋转等面板差异。

---

## 8. Device Tree、driver matching、Kconfig、Makefile 与 Binding

### 8.1 compatible 如何一路找到 probe

本项目真实链路为：

```text
DTS: display@0
  compatible = "sitronix,st7789v-dbi"
  reg = <0>
          │
          ▼
内核解析 &spi0 子节点，创建 spi_device spi0.0
          │
          ▼
SPI bus 尝试匹配已注册 spi_driver
          │
          ▼
st7789v_spi_driver.driver.of_match_table
  st7789v_of_match[] = "sitronix,st7789v-dbi"
          │
          ▼
匹配成功，调用 st7789v_probe(spi0.0)
          │
          ▼
创建 connector/simple pipe/GEM/mode_config
          │
          ▼
drm_dev_register()
          │
          ├── /dev/dri/card0
          ├── /sys/class/drm/card0-SPI-1
          └── drm_fbdev_generic_setup() → /dev/fb0
```

**【运行日志确认】** 最终 uevent 同时给出：

```text
DRIVER=st7789v-dbi
OF_FULLNAME=/spi@ff500000/display@0
OF_COMPATIBLE_0=sitronix,st7789v-dbi
MODALIAS=spi:st7789v-dbi
```

### 8.2 为什么使用新的 compatible

**【Git diff确认】** 旧 compatible 是 `sitronix,st7789v`，新的是 `sitronix,st7789v-dbi`。

**【代码中确认】** 当前内核已有 `sitronix,st7789v` panel binding 描述的是“SPI 传控制命令、像素走独立 DPI/RGB 总线”的另一种硬件连接；本板像素也走 SPI。新 compatible 明确表达 MIPI DBI Type C option 3，并避免与原有语义和旧 fbtft match 混淆。

### 8.3 Kconfig 做什么

**【根据 Linux 机制推导】** 把 `.c` 文件放进源码树不会自动编译。Kconfig 定义用户可选择的配置符号、依赖和自动选择项。本项目：

```text
CONFIG_TINYDRM_ST7789V
  depends on DRM && SPI
  selects DRM_KMS_HELPER
  selects DRM_KMS_CMA_HELPER
  selects DRM_MIPI_DBI
  selects BACKLIGHT_CLASS_DEVICE
```

- `=y`：对象链接进 `vmlinux`，启动时随内核注册；本项目最终就是 `=y`；
- `=m`：构建为 `st7789v.ko`，需要模块装载；
- 未设置：不会构建，DTS compatible 也找不到对应 driver。

`module_spi_driver()` 宏并不代表一定生成 `.ko`；当配置为 `=y` 时，它生成 built-in 初始化/退出注册逻辑。

### 8.4 Makefile 做什么

**【代码中确认】** Kbuild 规则是：

```make
obj-$(CONFIG_TINYDRM_ST7789V) += st7789v.o
```

Kconfig 决定符号值，Makefile 把符号值映射到实际 object。只有两者同时接通，驱动代码才进入 built-in 或 module 构建。

### 8.5 DT Binding 做什么、不做什么

**【Git diff确认】** 新 Binding 规定 required properties：compatible、reg、SPI max frequency、DC、reset；optional properties：backlight、rotation。

**【根据 Linux 机制推导】** Binding 的作用是规范“DTS 应该怎样描述硬件”，供开发者、review 和静态检查使用。运行时内核不会读取 `Documentation/.../sitronix,st7789v-dbi.txt` 来创建设备；真正告诉内核运行时硬件的是编译进 DTB 的 DTS node。

准确区分：

- Binding：文档/契约；
- DTS/DTSI：某块板的硬件实例描述；
- DTB：启动时传给内核的二进制设备树；
- driver/of_match：消费 compatible 并执行 probe。

当前 Binding 是 `.txt`，不是 YAML schema。**【当前证据不足】** 现有记录只有 checkpatch 通过，没有 `dt_binding_check` 的 schema 验证结果。

### 8.6 DTS 中保留和删除了什么

**【Git diff确认】** 保留：

- SPI0 CS0；
- 60 MHz 上限；
- GPIO1_D0 DC；
- GPIO1_C4 active-low reset；
- rotation 270；
- PWM backlight phandle。

**【Git diff确认】** 删除 fbtft-only properties：`fps`、`buswidth`、`debug`、`rotate`，改用 DRM binding 的 `rotation`。顶层 DTS 中旧 `fbtft@0` override 也被删除。

### 8.7 为什么关闭旧 `CONFIG_FB_TFT`

一个 `spi_device` 在 Linux driver model 中正常情况下只能绑定一个 `spi_driver`。**【根据 Linux 机制推导】** 如果两个驱动都能 match 同一个设备，先成功 probe 的驱动会占有 binding；另一个不会同时正常管理该 device。若通过错误的节点复制、手工 override 或其他方式强行制造双实例，还会争用同一 CS/GPIO/面板状态，造成 reset、初始化和 SPI 传输冲突。

本项目采用双保险：

1. compatible 从旧 fbtft match 改为新 DRM-specific match；
2. defconfig 彻底关闭 `CONFIG_FB_TFT`。

这使目标镜像的归属明确、可审计、可回退。

### 8.8 旧 FB_TFT 与 DRM fbdev emulation 不是一回事

| 项目 | 旧 FB_TFT/fbtft | DRM fbdev emulation |
| --- | --- | --- |
| 谁拥有硬件 | `fb_st7789v` | `st7789v-dbi` DRM driver |
| 核心对象 | `fb_info` + fbtft private data | DRM device/GEM/KMS，上面套 fbdev helper |
| `/dev/fb0` 名称 | `fb_st7789v` | `st7789v-dbidrmf` |
| `/dev/dri/card0` | 无 | 有 |
| 本项目最终 DeskBot 是否使用 | 否 | 兼容节点存在，但 DeskBot 不使用 |

保留 `CONFIG_FB=y` 不等于保留旧 fbtft。**【代码中确认】【运行日志确认】** 本项目用它承载 DRM fbdev emulation。

---

## 9. 实际问题排查与解决

### 问题 1：独立 O= 目录构建被 Kbuild 拒绝

#### 现象

**【运行日志确认】** 第一次尝试 `O=/tmp/...` 独立构建时，Kbuild 检测到源码树已有 in-tree 产物并要求清理。

#### 第一判断

这不是 `st7789v.c` 编译语法错误，而是同一 kernel source tree 混用了历史 in-tree output 与新的 out-of-tree output。

#### 根因

**【根据 Linux 机制推导】** 源码树已有生成文件时，Kbuild 为避免输出树互相污染，会拒绝不一致的 O= 构建布局。

#### 修改/处理

没有执行 `mrproper`，因为它会清除用户已有 SDK 输出；改用现有源码树的 in-tree build。

#### 验证

**【运行日志确认】** `st7789v.o`、DTB、zImage 和 FIT `boot.img` 均成功生成。

#### 面试价值

> 我先区分了构建环境问题和驱动代码问题。由于源码树已有 in-tree 产物，独立 O= 构建被 Kbuild 拒绝；为保护已有 SDK 输出，我没有直接 mrproper，而是在确认配置后沿用 in-tree 构建并验证最终 FIT 内容。

### 问题 2：首次烧录后花屏，内核报 `Failed to update display -22`

#### 现象

**【运行日志确认】** 首版镜像启动时 DRM device、fbdev 都创建成功，但第一次像素更新失败：

```text
[drm] Initialized st7789v-dbi ... for spi0.0 on minor 0
st7789v-dbi spi0.0: [drm] *ERROR* Failed to update display -22
st7789v-dbi spi0.0: [drm] fb0: st7789v-dbidrmf frame buffer device
```

实物表现为花屏。

#### 第一判断

DRM 注册已经成功，错误发生在 update 而不是 compatible/probe；因此优先检查 full-frame flush、RGB565 字节序和 SPI transfer 边界，而不是继续怀疑“驱动完全没绑定”。

#### 根因

**【代码中确认】【运行日志确认】** Rockchip SPI 返回最大 transfer `0xffff`=65535 bytes；mipi-dbi 因控制器支持 16 bpw，对 RGB565 使用 16-bit word。65535 不是 2 bytes 的整数倍，SPI core 的 partial-word 校验返回 `-EINVAL`，即 `-22`。

#### 修改

**【Git diff确认】** 在公共层 `mipi_dbi_spi_transfer()` 增加：

```c
max_chunk = ALIGN_DOWN(max_chunk, 2);
```

153600-byte 全帧由原来的首个 65535-byte 非法分片，变成：

```text
65534 + 65534 + 22532 = 153600 bytes
```

三个分片都是完整 16-bit word。

#### 原理

修复的是“公共 helper 与 SPI controller 最大传输边界的组合问题”，不是改 ST7789V 初始化表。保持 16 bpw 和原字节序，只让每个 transfer 长度满足 `len % 2 == 0`。

#### 验证

**【运行日志确认】** 修复版不再出现 `-22`，花屏消失；spi0.0 绑定 `st7789v-dbi`，connector connected，mode 320×240，Home/YOLO 约 30 分钟正常。

#### 面试价值

> DRM probe 已成功但首帧 update 返回 -EINVAL。我沿数据路径定位到 mipi-dbi 使用 16 bpw，而 Rockchip SPI 的 max transfer 是奇数 0xffff，导致分片不是完整 word。把公共层分片向下对齐到 2 bytes 后，全帧拆成三个偶数分片，错误和花屏同时消失。

### 问题 3：active-low reset 的 descriptor 语义陷阱

#### 现象

**【当前证据不足】** 没有记录显示它已经造成一次实际启动失败；这是迁移时通过源码审计提前规避的问题，不能包装成“现场故障根因”。

#### 风险原因

旧 fbtft 路径中的 0/1 接近物理电平语义；新 `devm_gpiod_get()` 会按 `GPIO_ACTIVE_LOW` 把 descriptor value 解释为逻辑 assert/deassert。当前 5.10 的 `mipi_dbi_hw_reset()` 固定逻辑 0→1，与本板的 active-low 描述组合会产生相反的物理时序。

#### 修改

**【代码中确认】** 新驱动实现 `st7789v_hw_reset()`：

```text
logical 1 → physical LOW，assert reset，20～40 µs
logical 0 → physical HIGH，release reset，等待 120 ms
```

#### 验证

**【运行日志确认】** 实机可稳定初始化且无 reset 相关错误；**【当前证据不足】** 没有逻辑分析仪波形。

#### 面试价值

> 我没有机械照抄旧驱动的 GPIO 0/1，而是区分了物理电平和 gpiod descriptor 的逻辑值。DTS active-low 下逻辑 1 才是物理拉低复位，所以我写了板级 reset helper，避免通用 helper 的固定序列在该 5.10 BSP 上反向。

### 问题 4：ARM 应用首次编译找不到 `drm.h`

#### 现象

**【运行日志确认】** fresh ARM build 首次在编译 `lv_linux_drm.c` 时找不到 `drm.h`。

#### 第一判断

目标 sysroot 已有 libdrm headers/library，问题不是“没装 libdrm”，而是 include directory 没有加到实际编译该源文件的 target。

#### 根因

**【代码中确认】** `lv_linux_drm.c` 属于 `lvgl` target；仅给最终 executable 添加 include/link 信息，不能影响 `lvgl` object 的编译命令。

#### 修改

**【Git diff确认】** 增加：

```cmake
target_include_directories(lvgl PRIVATE ${Libdrm_INCLUDE_DIRS})
```

并在 `TARGET_ARM` 时强制检查 `Libdrm_FOUND`。

#### 验证

**【运行日志确认】** fresh build 到 100%；产物为 ARM EABI5/uClibc ELF，依赖 `libdrm.so.2`，包含 `/dev/dri/card0` 和 DRM backend 符号。

#### 面试价值

> 这是 CMake target 作用域问题：谁编译源文件，谁就必须拿到 include path。libdrm 最终链接正确并不等于 lvgl target 编译时能找到 drm.h；把 include 挂到 lvgl target 后 fresh cross build 通过。

### 问题 5：板端一度仍在运行旧 FBDEV 产物

#### 现象

**【运行日志确认】** 首次应用部署后，仅凭“应用能启动”不能证明它已走 DRM；一度检查到旧进程/旧产物仍属于 FBDEV 路径。

#### 第一判断

问题可能在应用发布而非内核 DRM：内核已有 card0，但运行进程未必是刚交叉编译的二进制。

#### 根因

**【运行日志确认】** 部署/进程生命周期不一致，旧进程或旧文件未被正确替换。发生异常时没有完整的旧产物 hash 归档，因此不补造细节。

#### 修改/处理

停止服务、替换正确二进制并重新启动；使用 `readlink /proc/$pid/exe`、`sha256sum /proc/$pid/exe`、`strings` 和 `/proc/$pid/fd` 交叉确认。

#### 验证

**【运行日志确认】** 运行文件与主机产物 hash 同为 `f74590...`，fd 3 指向 `/dev/dri/card0`，没有进程 fd 指向 `/dev/fb0`。

#### 面试价值

> 我没有把“文件传上板”当作部署成功，而是从正在运行的 `/proc/<pid>/exe` 做 hash，再查 fd。最终证明运行进程确实打开 card0，而不是旧程序继续打开 fb0。

### 问题 6：OEM 模块目录/`insmod_ko.sh` 不完整导致重启后应用异常

#### 现象

**【运行日志确认】** 重启后一度出现 `/oem/usr/bin/RkLunch.sh start` 报缺少 `/oem/usr/ko/insmod_ko.sh`，摄像头/YOLO 相关模块和节点无法按预期就绪。

#### 根因

**【运行日志确认】** OEM 内核模块目录和模块加载脚本部署不完整或与当前内核不一致。这是系统部署一致性问题，不是 ST7789V DRM probe 失败。

#### 修改/处理

恢复与当前内核匹配、可执行的模块目录和加载脚本，再启动 DeskBot。

#### 验证

**【运行日志确认】** 应用恢复正常，多次进入/退出 YOLO 正常，后续 30 分钟刷新测试通过。

#### 面试价值

> 我把显示驱动、应用二进制和 OEM 模块部署分层排查。card0/显示驱动已正常，但摄像头依赖的模块加载脚本缺失，所以启动问题属于 rootfs/OEM 一致性，而不是 DRM 回归。

### 问题 7：历史日志出现 DRM dumb buffer 与 RKNN 内存分配失败

#### 现象

**【运行日志确认】** 滚动日志中出现过：

```text
DRM_IOCTL_MODE_CREATE_DUMB fail
No draw buffer
RKNN: failed to allocate model memory, errno 12
CmaFree: 0 kB
```

#### 第一判断

DRM dumb buffer 和 RKNN 同时出现 ENOMEM，说明系统/连续内存压力值得检查；但滚动日志跨越多次失败和恢复，不能直接当作正式 A/B 窗口的新错误。

#### 根因

**【当前证据不足】** 现有证据无法区分 CMA 真正耗尽、可迁移页暂时占用、buffer 生命周期泄漏、碎片、旧进程残留或部署状态等具体原因。不能写成“已确认 DRM 内存泄漏”，也不能写成“确定由 CMA=0 导致”。

#### 处理与验证

重启并修复部署状态后，正确 DRM 应用能分配两个 dumb buffer，RKNN/VI/VPSS 正常，多次 YOLO 和正式采样通过。正式 Home/YOLO 采样前后 dmesg 完全一致，YOLO metrics failures=0。

#### 面试价值

> 我观察到 dumb buffer 和 RKNN 同时 ENOMEM，并结合 CmaFree=0 判断有连续内存压力风险；但因为日志跨生命周期且没有分配追踪，我没有强行归因。恢复后功能正常，所以把它保留为待用 cold boot、多轮生命周期和 CMA trace 继续关闭的问题。

### 问题 8：dmesg 同时有 SPI0 pinctrl 警告和 SPI2 NAND 错误

#### 现象

**【运行日志确认】** 正常点屏日志仍包含：

```text
rockchip-spi ff500000.spi: no high_speed pinctrl state
spi-nand spi2.0: unknown raw ID 00000000
spi-nand: probe of spi2.0 failed with error -524
```

#### 第一判断

不能因为日志中出现 `spi` 和 `failed` 就把所有错误归给 LCD；必须按 bus/device identity 区分 `spi0.0` 显示与 `spi2.0` NAND。

#### 结论

- **【运行日志确认】** ST7789V 位于 `spi0.0`，已经绑定 `st7789v-dbi` 并正常刷新；`no high_speed pinctrl state` 没有阻塞本轮显示功能验收。
- **【运行日志确认】** NAND 报错来自 `spi2.0`/`ffac0000`，与显示所在的 SPI0 是不同 controller/device，不能作为 ST7789V 失败证据。
- **【当前证据不足】** 未测实际 SCLK 和高频信号裕量，因此 SPI0 警告虽然不是当前失败根因，仍应作为后续电气验证边界保留。

#### 面试价值

> 排查内核日志时我按 `controller + bus.cs` 关联设备，而不是看到 SPI 错误就统一归因。显示是 spi0.0 且已成功绑定、持续刷新；NAND 失败在 spi2.0，是独立问题。SPI0 的 high-speed pinctrl 警告未阻塞功能，但因为没有波形，我仍保留信号完整性验证边界。

---

## 10. 迁移后用户态到底发生了什么

### 10.1 是否还存在 `/dev/fb0`

是。**【运行日志确认】** `/dev/fb0` 名称为 `st7789v-dbidrmf`，320×240、16 bpp、stride 640。它由 `drm_fbdev_generic_setup()` 提供，是 DRM compatibility layer。

### 10.2 是否出现 `/dev/dri/card0`

是。**【运行日志确认】** `/dev/dri/card0` major/minor 为 226:0，sysfs 有 `card0-SPI-1`，状态 connected、enabled，mode 320×240。

### 10.3 LVGL 当前使用什么 backend

**【代码中确认】** ARM 构建为 `LV_USE_LINUX_DRM=1`、`LV_USE_LINUX_FBDEV=0`；`main.c` 默认调用：

```c
lv_linux_drm_create();
lv_linux_drm_set_file(disp, "/dev/dri/card0", -1);
```

也可用 `LV_LINUX_DRM_CARD` 环境变量选择其他 card。

### 10.4 应用是否直接使用 DRM，是否经过 fbdev emulation

**【运行日志确认】** DeskBot fd 指向 `/dev/dri/card0`，没有 fd 指向 `/dev/fb0`；因此最终应用直接使用 DRM，**不经过** fbdev emulation。fb0 只是兼容节点仍然存在。

### 10.5 用户态 buffer 到 LCD 的新路径

```text
LVGL 以 RGB565 直接渲染到两个 mmap 的 dumb buffer
                         │
                         ▼
libdrm atomic request：connector/CRTC/plane/FB_ID
                         │
                         ▼
DRM atomic helper 提交 plane state
                         │
                         ▼
mipi_dbi_pipe_update 合并 damage
                         │
                         ▼
从 GEM CMA object 取 vaddr，必要时格式转换/复制
                         │
                         ▼
设置 2A/2B 窗口，2C 写 RGB565 pixels
                         │
                         ▼
SPI0 分片传输到 ST7789V GRAM
```

**【代码中确认】** LVGL 创建两个 dumb buffer，`DRM_FORMAT_RGB565`，使用 `LV_DISPLAY_RENDER_MODE_DIRECT`。第一次 atomic commit 带 modeset，之后更新 plane 的 `FB_ID` 并请求 nonblocking page-flip event。

### 10.6 “双缓冲/page flip”在 SPI 屏上的真实含义

**【根据 Linux 机制推导】** 两个 dumb buffer 是系统内存中的两个 GEM buffer，LVGL 在它们之间切换。ST7789V 并不会像 SoC VOP 一样直接 scanout 其中一个物理地址；内核仍需把被提交 buffer 的像素通过 SPI 写到 panel GRAM。

因此：

- atomic/page flip 提供标准 buffer/state 提交流程；
- dirty clip 可能减少 SPI 数据量；
- 但每次画面变化仍受 SPI 带宽与 panel GRAM 更新限制；
- page-flip event 不等于已测得的面板光学刷新完成。

---

## 11. 构建与产物证据

### 11.1 内核

**【运行日志确认】** 使用 `arm-rockchip830-linux-uclibcgnueabihf-gcc 8.3.0`，完成驱动 object、DTB、zImage 和 `./build.sh kernel`。

生成 `.config` 确认：

```text
CONFIG_DRM_MIPI_DBI=y
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_DRM_GEM_CMA_HELPER=y
CONFIG_DRM_KMS_CMA_HELPER=y
CONFIG_TINYDRM_ST7789V=y
# CONFIG_FB_TFT is not set
```

最终修复版产物：

| 产物 | SHA-256 |
| --- | --- |
| zImage | `aec8045c8e73f5e5691f08b26d9c038847b39986dec3aad245a10ed06f707774` |
| DTB | `aa3696cb59831ce40703021177f37474e752da0d0e5546f2066ad6f37eb28ea6` |
| boot.img | `a6b0dfea211aa38744367c48506709a7ff3f0bbc57e835b3ac861995fd679082` |

首版失败 boot image 是 `cbccdba2...`，不应再用于复测。

**【运行日志确认】** checkpatch 对 driver、Binding 和公共层修复均为 0 error/0 warning；vmlinux 符号包含 `mipi_dbi_spi_transfer`、`st7789v_probe`、`st7789v_driver`；从 FIT 提取的 FDT hash 与输出 DTB 一致。

### 11.2 应用

**【运行日志确认】** fresh ARM CMake build 到 100%，产物是 32-bit ARM EABI5/uClibc ELF，动态依赖 `libdrm.so.2`，SHA-256 为 `f74590...`。

### 11.3 构建证据边界

构建成功只能证明配置、编译、链接和镜像内容成立，不能单独证明：

- 板端 compatible 一定匹配；
- SPI 电气通信一定正常；
- DRM master/atomic commit 一定成功；
- 颜色、方向和长期刷新一定正常。

这些由后续 dmesg、sysfs、进程 fd 和人工验收补齐。

---

## 12. 性能与内存测试分析

### 12.1 数据来源和口径

**【运行日志确认】** 数据由同一汇总脚本处理四组目录：旧 FB Home/YOLO 与新 DRM Home/YOLO。Home 采样 59 s，YOLO 采样 119 s。当前只有一轮正式 A/B。

| 场景 | 后端 | 进程 CPU avg/p95 | System busy avg | RSS avg/max KiB | VmHWM KiB |
| --- | --- | ---: | ---: | ---: | ---: |
| Home | FB/fbtft | 3.00% / 4.00% | 7.64% | 7300 / 7300 | 10952 |
| Home | DRM/KMS | 2.96% / 3.92% | 6.83% | 10636 / 10636 | 13432 |
| YOLO | FB/fbtft | 35.71% / 37.76% | 51.19% | 13691 / 13828 | 14360 |
| YOLO | DRM/KMS | 36.22% / 38.38% | 39.81% | 10872 / 10872 | 13432 |

YOLO 应用指标：

| 后端 | FPS | e2e avg/p95 | infer avg/p95 | queue avg/p95 | failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| FB/fbtft | 8.889 | 174.010 / 193 ms | 89.000 / 94 ms | 60.057 / 76 ms | 0 |
| DRM/KMS | 8.807 | 174.041 / 191 ms | 91.174 / 95 ms | 58.846 / 76 ms | 0 |

### 12.2 System busy 是什么

**【代码中确认】** 汇总工具根据相邻 `/proc/stat` 样本的 CPU jiffies 差值计算系统 busy：分母是各 CPU time 字段增量之和，分子扣除 `idle + iowait`。在单核 RV1106 上，它反映该窗口内整个系统处于 user/nice/system/irq/softirq/steal 等活动状态的比例。

它不能简单叫“DeskBot CPU 占用率”，因为它包含：

- DeskBot 和其他进程；
- 内核线程；
- SPI/媒体/NPU 相关 system time；
- IRQ/softirq；
- 日志打印等全系统活动。

**【运行日志确认】** 单轮 YOLO System busy 从 51.19% 降到 39.81%，下降 11.38 个百分点，约为旧值的 22.2%。

可说：

> 同口径单轮测试观察到 DRM 场景系统 busy 下降趋势，同时 DeskBot 进程 CPU 和 YOLO FPS 基本持平。

不能说：

> DRM 驱动确定让 CPU 降低 22.2%。

原因是旧 FB DTS 开启 `debug=7`，fbtft per-transfer/draw 日志会增加内核和串口开销；只有一轮，且系统 busy 还包含显示之外的整机负载。

### 12.3 RSS 是什么，为什么不能直接归因 DRM

**【根据 Linux 机制推导】** RSS 是进程当前驻留在物理内存中的页面总量，包括匿名页、文件映射和共享映射的驻留部分，不等于进程全部虚拟地址空间，也不等于系统 CMA 占用。

**【运行日志确认】** YOLO RSS 均值从 13691 KiB 降到 10872 KiB，约 2819 KiB/20.6%；但 Home DRM RSS 反而高于旧 FB。

可能影响 RSS 的因素包括：

- 场景进入顺序和此前页面资源是否已加载；
- allocator 是否把释放内存归还内核；
- 线程栈和共享库页面是否驻留；
- DRM dumb buffer mmap 页面是否被 fault-in；
- RKNN/VI/VPSS buffer 的映射与生命周期；
- 采样时刻。

**【当前证据不足】** 没有分配追踪、同冷启动多轮、smaps 分类对照，无法把 20.6% 直接归因于显示框架。

### 12.4 VmRSS、RSS、VmHWM、VmPeak

- `VmRSS`：读取 `/proc/<pid>/status` 时的当前 resident memory；
- RSS：本测试采样/汇总的 resident memory 概念，与 VmRSS 同类；
- `VmHWM`：进程启动以来 VmRSS 到过的最高值，高水位不会因为当前释放而下降；
- `VmPeak`：进程启动以来虚拟地址空间 `VmSize` 的峰值，不表示这么多物理内存驻留。

**【运行日志确认】** YOLO VmHWM 从 14360 KiB 到 13432 KiB，少 928 KiB，约 6.5%；但 DRM Home 也已经是 13432 KiB，说明它受此前进程生命周期影响。

正确简历表达应避免单轮百分比，可写“完成同口径 CPU/RSS/HWM 对比，观察到 system busy 下降趋势，应用吞吐基本持平”。

### 12.5 SPI throughput 为什么 DRM 是 N/A

**【运行日志确认】** 旧 fbtft 的 throughput 来自 `Display update` debug 日志；DRM 没有相同记录，因此汇总显示 N/A。

N/A 表示“缺少同口径数据”，不是 0，也不是“DRM 不传像素”。若要严格比较，需要：

- 给 DRM flush/commit 增加低扰动统计；或
- 使用 tracepoint；或
- 用逻辑分析仪测 SPI CS/SCLK；或
- 用高速相机测光学完成时间。

---

## 13. CMA 与 RV1106 连续内存压力

### 13.1 CMA 是什么

**【根据 Linux 机制推导】** CMA（Contiguous Memory Allocator）在启动时预留一片可用于大块连续物理内存的区域。空闲时其中部分页面可借给可迁移页；DMA 设备需要连续块时，内核尝试迁移这些页并回收出连续区域。

### 13.2 为什么 DRM/Camera/ISP/NPU 可能关心 CMA

许多 DMA engine 需要物理连续、满足对齐且设备可寻址的 buffer。当前平台 `.config` 有 `CONFIG_CMA=y`、`CONFIG_DMA_CMA=y`，并且 **【代码中确认】** `# CONFIG_IOMMU_SUPPORT is not set`，因此不能普遍依赖 IOMMU 把离散页映射成连续 IOVA。

- DRM GEM CMA helper 用 `dma_alloc_wc()` 创建 dumb/GEM buffer；
- SPI master 通过 DMA API 映射传输 buffer；
- Camera/VI/VPSS/ISP/RGA/NPU/MPP 可能通过 Rockchip dma-heap 或各自 allocator 请求 DMA buffer。

**【根据 Linux 机制推导】** 这些模块可能争用同一类连续内存资源，但具体是否来自同一个 CMA area/heap，需要 allocator trace 或 debugfs 证明。

### 13.3 当前平台实际配置

**【代码中确认】【运行日志确认】** BoardConfig 设置 `RK_BOOTARGS_CMA_SIZE="66M"`，板端 bootargs 为 `rk_dma_heap_cma=66M`。采样值：

```text
CmaTotal:     67584 kB   # 正好 66 MiB
CmaFree:          0 kB
MemAvailable: 约 26～28 MiB
MemFree:      约 1.7～2.1 MiB
```

### 13.4 `CmaFree=0` 是否一定出问题

不一定。

**【根据 Linux 机制推导】** `CmaFree=0` 表示采样时 CMA 区域没有被统计为直接 free 的页；页可能已经被 DMA buffer 使用，也可能借给 movable allocations。已有 buffer 不需要重新分配就能继续运行，新的请求也可能通过迁移/回收成功，所以应用仍可正常。

但它是风险信号：

- 新的较大连续分配更容易失败或产生抖动；
- 重复进入/退出 Camera/YOLO 时，更容易暴露生命周期问题；
- 当前历史日志确实出现过 RKNN ENOMEM 与 DRM dumb create failure。

**【当前证据不足】** 不能仅凭 `CmaFree=0` 判定系统故障，也不能把历史 ENOMEM 唯一归因 CMA。

### 13.5 为什么 CmaTotal 可能“看起来比普通内存还大”

`CmaTotal` 是物理内存中 CMA 区域的总量，`MemFree/MemAvailable` 是当前动态可用性指标，两者不是可相加或直接比较的同一口径。66 MiB 的 CmaTotal 大于当时 26～28 MiB 的 MemAvailable 是正常的：前者是池总量，后者是此刻估算可供普通分配使用的内存。

**【当前证据不足】** 本次 CMA 采集文件没有记录 `MemTotal`，所以没有证据证明本板出现“CmaTotal 真正大于 MemTotal”。标准内核统计下 CMA 属于物理 RAM 的一部分，通常不应真正超过 MemTotal；若别处看到这种现象，应先核对单位、采样时刻、容器/namespace、厂商自定义 heap 统计和日志截断，而不是直接认定内核分配了超过总 RAM 的 CMA。

### 13.6 后续怎样关闭 CMA 问题

建议在相同固件下做至少三轮 cold boot：

1. 启动前后记录完整 `/proc/meminfo`，包括 MemTotal；
2. 记录 dma-buf heap、`/proc/<pid>/smaps_rollup`、相关 debugfs（若内核支持）；
3. 循环进入/退出 YOLO 50～100 次，记录每轮 CmaFree、RSS/HWM、buffer 数；
4. 为 DRM dumb buffer、VI/VPSS 和 RKNN allocation 建立成功/失败计数；
5. 若可复现 ENOMEM，再用 page owner/CMA trace 或厂商 allocator debug 定位具体未释放对象。

---

## 14. 最终结果与仍然存在的边界

### 14.1 最终系统结构

- **迁移前【运行日志确认】**：DeskBot→`/dev/fb0`→fbdev/fbtft→`fb_st7789v`→SPI0→ST7789V；没有 `/dev/dri`。
- **迁移后【运行日志确认】**：DeskBot/LVGL/libdrm→`/dev/dri/card0`→DRM/KMS→`st7789v-dbi`→`drm_mipi_dbi`→SPI0→ST7789V。

### 14.2 新增能力

- 标准 DRM card 和 connector/mode 枚举；
- GEM CMA dumb buffer 与 framebuffer object；
- RGB565 双 buffer direct render；
- atomic modeset/plane commit；
- damage-aware mipi-dbi flush；
- DRM fbdev compatibility layer；
- libdrm 用户态直连。

### 14.3 淘汰的路径

**【Git diff确认】** 目标 defconfig 关闭 `CONFIG_FB_TFT`，旧 `fb_st7789v` 不再管理 SPI0.0；旧 compatible 和 fbtft-only DTS properties 被移除。

### 14.4 功能与稳定性

**【运行日志确认】** 最终：

- spi0.0 绑定 `st7789v-dbi`；
- card0-SPI-1 connected/enabled，mode 320×240；
- RGB565、方向、颜色、offset 正常；
- 无明显撕裂、闪烁、花屏；
- Home/YOLO 重复进入退出正常；
- 约 30 分钟持续刷新正常；
- 正式采样前后 dmesg 无新增内容，YOLO failures=0。

### 14.5 性能结论

**【运行日志确认】** YOLO FPS 8.889→8.807，进程 CPU 35.71%→36.22%，基本持平；单轮 system busy 51.19%→39.81%，RSS 13691→10872 KiB，呈下降趋势。

**【当前证据不足】** 由于旧 fbtft debug、采样顺序和单轮测试限制，不宣称最终提升百分比。

### 14.6 仍未完成

- 没有 `modetest` 实测记录；
- 没有 SPI/reset/DC 逻辑分析仪波形；
- 没有 TE/vblank 物理同步验证；
- 没有 DRM flush/SPI throughput 同口径统计；
- CMA/历史 ENOMEM 根因尚未关闭；
- 只有一轮正式 A/B，缺少至少三轮 cold boot 的中位数和波动范围；
- 没有完成 RGA/dma-buf 到 DRM 的零拷贝链路。

---

## 15. 专有名词通俗解释

> 下表“专业定义”属于 **【根据 Linux 机制推导】**；“本项目”列按代码/日志给出具体落点。

| 术语 | 专业定义 | 在本项目里负责什么 | 通俗理解 |
| --- | --- | --- | --- |
| Framebuffer | 一段按固定像素格式组织的图像内存，也泛指 Linux 传统显示接口 | **【代码中确认】** 旧驱动维护 320×240 RGB565 vmem | 一张准备送到屏幕的像素画布 |
| fbdev | Linux framebuffer device 子系统和 `/dev/fbN` ABI | **【运行日志确认】** 旧应用通过 `/dev/fb0` 显示；新系统也有 DRM 提供的兼容 fb0 | 把画布包装成可读写设备文件 |
| fbtft | staging 中面向小型 TFT 的 fbdev 辅助框架 | **【代码中确认】** 旧路径负责 vmem、deferred I/O、GPIO、刷屏 | 老式 SPI 小屏的通用脚手架 |
| DRM | Linux Direct Rendering Manager，管理显示/GPU 设备、buffer 和用户态接口 | **【运行日志确认】** 产生 `/dev/dri/card0` | 现代 Linux 显示总管 |
| KMS | DRM 的 Kernel Mode Setting，管理 mode、connector、CRTC、plane | **【运行日志确认】** 暴露 320×240 SPI connector | 内核统一决定“哪张图通过哪条管线送到哪块屏” |
| TinyDRM / DRM tiny driver | 针对简单显示硬件的 DRM 驱动模式与 helper 用法 | **【代码中确认】** ST7789V 只有固定 mode 和 simple pipe | 给小屏使用的精简 DRM 驱动骨架 |
| MIPI DBI | 处理器与显示控制器之间的命令/数据总线接口规范 | **【代码中确认】** 使用 Type C option 3 | 屏幕理解的命令和像素传输规矩 |
| drm_mipi_dbi | Linux DRM 为 MIPI DBI 小屏提供的公共 helper | **【代码中确认】** 负责 pipe、dirty rect、格式、DCS 窗口和 SPI flush | 已经装好的“小屏 DRM 变速箱” |
| GEM | DRM Graphics Execution Manager，管理 buffer object 生命周期和映射 | **【代码中确认】** 承载 dumb buffer/framebuffer 后端对象 | 内核里的显存对象管理员；即使没有独立显存也能用 |
| dumb buffer | 通过 DRM ioctl 创建、CPU 可 mmap 的简单 scanout buffer | **【代码中确认】** LVGL 创建两个 RGB565 buffer | 没有 GPU 加速花活、但能直接画的基础显示缓冲 |
| framebuffer object | DRM 中描述 width/height/pitch/format 并引用 GEM handle 的 KMS 对象 | **【代码中确认】** `drmModeAddFB2()` 创建并交给 plane | 给一块 buffer 加上“怎样解释像素”的标签 |
| DRM device | 一个注册到 DRM core 的显示卡对象 | **【代码中确认】** `drm_dev_register()` 发布 ST7789V card0 | 整套显示资源的总容器 |
| CRTC | DRM 中代表一条 active display pipeline 和 mode 的抽象；名称源于 CRT 历史 | **【代码中确认】** simple pipe 内代表 SPI 屏的活动输出状态 | 不是说板上有显像管，而是“当前显示流水线” |
| Plane | 选择 framebuffer 并描述 source/destination 的显示层 | **【代码中确认】** 一个 primary plane，LVGL更新 FB_ID | 决定拿哪张画布、放到屏幕哪里 |
| Connector | 显示输出端与屏幕连接关系，提供 status 和 modes | **【运行日志确认】** `card0-SPI-1`，connected，320×240 | 系统眼里的“这块屏接口” |
| Encoder | 把 CRTC 输出编码/路由到 connector 的 DRM 抽象 | **【根据 Linux 机制推导】** simple pipe 隐藏了独立 encoder 样板，本硬件没有 HDMI 式 encoder | 复杂硬件里的信号转换站；本项目被简单管线折叠 |
| drm_simple_display_pipe | 把一个 primary plane、一个 CRTC 和简单输出组合起来的 helper | **【代码中确认】** mipi-dbi 用它建固定 SPI 管线 | 把小屏不需要的复杂 DRM 拼装工作打包 |
| Atomic commit | 一次性检查并提交多个 KMS 对象状态 | **【代码中确认】** LVGL 提交 connector/CRTC/plane 属性 | 把一组显示变更当成一个不可拆的订单提交 |
| Damage clip | framebuffer 中真正发生变化的矩形区域 | **【代码中确认】** mipi-dbi 合并 damage 后只刷脏区域 | 只搬动改过的那块画面 |
| SPI controller | SoC 上执行 SPI 时序和 DMA/PIO transfer 的硬件及驱动 | **【运行日志确认】** RV1106 `ff500000.spi` / SPI0 | 真正产生 SCLK/MOSI/CS 的发动机 |
| SPI device | 挂在某个 SPI bus/chip-select 上的从设备实例 | **【运行日志确认】** ST7789V 是 `spi0.0` | SPI0 的 0 号片选上那块屏 |
| compatible | DT 中描述硬件兼容型号的字符串 | **【Git diff确认】** 新值 `sitronix,st7789v-dbi` | 设备树给驱动看的“型号标签” |
| of_match_table | 驱动声明可匹配 compatible 的表 | **【代码中确认】** `st7789v_of_match[]` | 驱动的支持型号清单 |
| probe | bus 匹配 device/driver 后调用的初始化入口 | **【代码中确认】** 创建 GPIO、DBI、KMS 和 card0 | 驱动真正接管设备时的入职流程 |
| Kconfig | 定义内核配置符号、依赖与选择关系 | **【Git diff确认】** 定义 `TINYDRM_ST7789V` | 决定功能能否被选择 |
| Makefile/Kbuild | 根据配置把源码映射为 built-in object 或 module | **【Git diff确认】** 配置符号映射 `st7789v.o` | 决定选中的功能到底编译哪个文件 |
| DT Binding | 规范某类硬件节点允许/必须有哪些属性 | **【Git diff确认】** 定义 DBI compatible、GPIO、rotation 等 | DTS 的填写说明和审查合同，不参与运行时执行 |
| DTS/DTSI | 某板硬件实例的源代码描述；DTSI 通常被多个板复用 | **【Git diff确认】** 描述 SPI0 上的 display@0 | 板子的硬件清单源文件 |
| DTB | DTS 编译后的二进制设备树，启动时交给内核 | **【运行日志确认】** FIT 中 FDT 与输出 DTB hash 一致 | 内核开机时真正读取的硬件清单 |
| CMA | 为连续物理内存分配预留/管理的区域 | **【运行日志确认】** 当前 66 MiB，CmaFree=0 | 给大块 DMA buffer 留的一片可整理土地 |
| DMA | 设备在较少 CPU 搬运参与下访问内存的数据传输机制 | **【代码中确认】** GEM CMA/SPI/媒体模块使用 DMA API 或 DMA buffer | 外设自己搬数据的通道 |
| RSS | 进程当前驻留在物理 RAM 的页面量 | **【运行日志确认】** 采样 DeskBot 当前内存 | 此刻真正住在 RAM 里的进程页面 |
| VmHWM | 进程生命周期内 RSS 的最大值 | **【运行日志确认】** 记录峰值驻留上界 | 进程曾经达到过的最高水位 |
| System Busy | 本测试中为 `/proc/stat` 相邻样本总增量扣除 `idle+iowait` 后的比例 | **【代码中确认】** 汇总脚本计算整机 busy | 整台单核机器有多少时间在执行活动任务，不只是 DeskBot CPU |

---

## 16. 面试官视角：这次工作证明了什么

### 16.1 可以证明的能力

- 能从 Device Tree compatible 追到 SPI driver、probe、内核配置和用户态节点；
- 能基于已工作 FB 驱动提取 reset/DC/SPI/rotation/RGB565/寄存器基准；
- 能复用 DRM tiny/mipi-dbi helper 完成 panel-specific DRM 接入；
- 能完成 Kconfig、Kbuild、Binding、DTS、defconfig、Buildroot 和应用 backend 的跨层集成；
- 能使用交叉工具链构建 kernel/DTB/FIT 和 ARM 用户态程序；
- 能用 dmesg、sysfs、`/proc/<pid>`、hash 和性能原始数据验证真实运行链路；
- 能定位 16 bpw 与 controller max transfer 边界导致的 `-EINVAL`，并在公共层做最小修复；
- 能区分驱动问题、应用部署问题、OEM 模块问题和未关闭的内存问题。

### 16.2 可以合理描述但不要夸大的能力

- “理解 DRM/KMS 基本对象和 tiny driver probe/flush 路径”；
- “完成一个 SPI MIPI-DBI panel 的 DRM 迁移”；
- “具备 BSP 显示链路和内存压力的基础分析能力”；
- “观察到 DRM 场景 system busy 下降趋势，应用吞吐基本持平”。

### 16.3 不应该因此声称的能力

- 精通 Linux DRM 全子系统；
- 设计了 DRM 框架或重构了 Linux 显示子系统；
- 掌握多 CRTC、多 plane、HDMI/DSI、VOP、GPU render node；
- 实现了 RGA/dma-buf/DRM 零拷贝；
- DRM 彻底解决了撕裂；
- 性能确定提升 22.2% 或内存确定下降 20.6%；
- 已解决所有 CMA/ENOMEM 问题；
- 自研了一套通用 TinyDRM 框架。

面试中的最佳定位是：

> 我完成了一个真实、跨 Device Tree—kernel driver—KMS—libdrm—LVGL 的 SPI LCD DRM 迁移，并能解释公共框架、板级差异、失败路径和证据边界；这证明我具备 BSP/driver 项目的系统性排查能力，但不等于已经覆盖 DRM 的所有复杂硬件场景。

---

## 17. 面试问题（暂不提供标准答案）

### 基础问题

1. Linux framebuffer 是什么，`/dev/fb0` 为什么不是 LCD 本身？
   **【考察点】** fb_info、用户态 ABI、显存与物理传输的分层。

2. fbdev、fbtft、`fb_st7789v` 三者分别负责什么？
   **【考察点】** 通用 core、通用小屏 helper、panel-specific driver 的职责边界。

3. DRM 比传统 fbdev 多提供了哪些核心抽象？
   **【考察点】** GEM、framebuffer object、KMS 对象、atomic state，而不是只回答“性能更好”。

4. 没有 GPU 的 SPI LCD 为什么也能使用 DRM？
   **【考察点】** DRM/KMS 与 GPU 解耦、panel GRAM、软件 flush。

5. SPI 与 MIPI DBI 是什么关系？
   **【考察点】** 总线传输与显示命令协议的区别，Type C option 3。

### 项目问题

6. 这次为什么从 FB/fbtft 迁移到 DRM，真正目标是什么？
   **【考察点】** 现代接口、可维护性、标准 buffer/state、避免虚构“必然提速”。

7. 迁移前你为什么先记录 reset、DC、rotation、RGB/BGR、offset 和初始化寄存器？
   **【考察点】** 控制变量、板级基准、首次点屏排障方法。

8. 为什么新 compatible 使用 `sitronix,st7789v-dbi`，而不是继续用 `sitronix,st7789v`？
   **【考察点】** binding 语义、已有 DPI panel 路径、避免 match 冲突。

9. 为什么要关闭 `CONFIG_FB_TFT`，但仍保留 `CONFIG_FB=y`？
   **【考察点】** 旧硬件 owner 与 DRM fbdev emulation 的区别。

10. 最终如何证明 DeskBot 真正使用 `/dev/dri/card0`，而不是只换了内核驱动？
    **【考察点】** 运行二进制 hash、`/proc/<pid>/fd`、maps、进程而非文件静态检查。

### 源码问题

11. 从 DTS compatible 到 `st7789v_probe()`，Linux driver model 经过哪些步骤？
    **【考察点】** DT node→spi_device→of_match→spi_driver→probe。

12. `mipi_dbi_dev_init()` 帮驱动建立了哪些 DRM 对象？
    **【考察点】** mode_config、SPI connector、simple pipe、damage、formats、tx buffer。

13. `drm_mipi_dbi` 如何把一个 DRM framebuffer 刷到 ST7789V？
    **【考察点】** GEM CMA、damage rect、2A/2B/2C、RGB565、SPI command/data。

14. CRTC、plane、connector 在只有一块固定 SPI 屏时分别代表什么？
    **【考察点】** 抽象语义而非字面 CRT；simple display pipe 如何折叠复杂性。

15. Kconfig 和 Makefile 各自解决什么问题，`=y` 与 `=m` 有什么不同？
    **【考察点】** 配置依赖与实际 object 构建/注册方式。

16. DT Binding 是否参与内核运行，和 DTS/DTB 有什么区别？
    **【考察点】** schema/契约与运行时硬件描述的边界。

### 追问问题

17. 首帧为什么会报 `-EINVAL`，`ALIGN_DOWN(max_chunk, 2)` 为什么有效？
    **【考察点】** 16 bpw partial word、0xffff、SPI core validation、最小修复位置。

18. active-low GPIO 使用 descriptor API 时，逻辑 0/1 与物理高低有什么区别？
    **【考察点】** GPIO polarity abstraction、reset 时序、不能机械复制旧代码。

19. atomic commit/page flip 能否保证这块 SPI 屏绝对无撕裂？为什么？
    **【考察点】** TE/vblank、panel GRAM、SPI flush 和 commit event 的边界。

20. `CmaFree=0` 为什么应用仍可能运行？如何进一步判断 ENOMEM 根因？
    **【考察点】** CMA movable pages、已有 buffer、连续分配、生命周期和 trace 方案。

---

## 18. 简历描述

### 版本 A：BSP / Driver 岗推荐版

> 基于 RV1106 + ST7789V 将 DeskBot 显示链路从 fbdev/fbtft 迁移到 DRM/KMS，复用 DRM tiny 与 `drm_mipi_dbi` 完成驱动、Kconfig/Kbuild、DT Binding、DTS/defconfig 及 LVGL/libdrm 接入；上板验证 320×240 RGB565、方向/颜色/offset 和 Home/YOLO 约 30 分钟稳定刷新。

### 版本 B：更加技术化版本

> 为 RV1106 SPI ST7789V 接入 DRM tiny/MIPI-DBI 显示驱动，完成 GEM CMA/simple display pipe/atomic、Kconfig/Kbuild、DT Binding 与设备树集成，并定位修复 Rockchip SPI `0xffff` 最大分片在 16 bpw RGB565 传输下因非 word 对齐导致的 `-EINVAL` 花屏；DeskBot/LVGL 最终直连 `/dev/dri/card0` 并通过板端稳定性验证。

### 版本 C：60～90 秒面试口述版

> 项目原来是 LVGL 打开 `/dev/fb0`，内核通过 framebuffer core、fbtft 和 `fb_st7789v` 把 RGB565 画面发到 SPI0 上的 ST7789V。这个方案能工作，但接口比较旧，也缺少 DRM 的标准 buffer、connector、mode 和 atomic 管理，所以我先把旧驱动里的复位、DC、60 MHz SPI、270 度旋转、RGB/BGR、offset 和初始化寄存器全部固化成基准，再基于内核现有 DRM tiny 和 `drm_mipi_dbi` 做迁移，而不是从零写整套 DRM。
>
> 我完成了 ST7789V panel driver、Kconfig/Kbuild、DT Binding、DTS compatible、defconfig 和 LVGL/libdrm backend 接入。第一次上板 DRM probe 成功，但首帧报 `-22` 并花屏，我定位到 Rockchip SPI 最大传输长度是奇数 `0xffff`，而 RGB565 走 16 bpw，首个分片不是完整 word；在 mipi-dbi 公共层把分片向下对齐到 2 bytes 后恢复正常。最终进程直接持有 `/dev/dri/card0`，颜色、方向、offset、重复 YOLO 和约 30 分钟持续刷新都通过。性能只做了一轮 A/B，观察到 system busy 下降趋势、YOLO FPS 基本持平，但旧 fbtft debug 会干扰，所以我没有写成确定提升百分比。

---

## 19. 最推荐的简历版本

面向 2027 届 Linux BSP / Driver 岗，最推荐使用下面这一条：

> **在 RV1106 + ST7789V 项目中，将 LVGL 显示路径从 fbdev/fbtft 迁移至 DRM/KMS：基于 DRM tiny + `drm_mipi_dbi` 实现 SPI DBI 驱动，完成 Kconfig/Kbuild、DT Binding、DTS/defconfig 及 libdrm 应用接入；定位并修复 Rockchip SPI 16-bit 传输在 `0xffff` 分片下因非 word 对齐导致的 `-EINVAL` 花屏，上板验证 320×240 RGB565、方向/颜色/offset、DRM 直连及 Home/YOLO 约 30 分钟稳定刷新。**

推荐理由：

- 有明确问题背景和完整跨层动作；
- 有真实、可追问的故障定位细节；
- 有板端结果，不依赖未经控制的性能百分比；
- 没有声称自研 DRM 框架、精通 DRM、GPU 提升或彻底消除撕裂。

---

## 20. 复现与后续验证清单

### 20.1 当前成果的最小核对命令

```sh
dmesg | grep -i -E 'st7789|drm|spi|failed to update'
readlink /sys/bus/spi/devices/spi0.0/driver
cat /sys/class/graphics/fb0/name
ls -l /dev/dri /sys/class/drm
cat /sys/class/drm/card0-SPI-1/status
cat /sys/class/drm/card0-SPI-1/modes

pid="$(cat /var/run/deskbot.pid)"
readlink "/proc/$pid/exe"
ls -l "/proc/$pid/fd" | grep -E 'dri|fb'
grep -E 'MemTotal|MemFree|MemAvailable|CmaTotal|CmaFree' /proc/meminfo
```

### 20.2 预期结果

```text
spi0.0 driver              st7789v-dbi
fb0 name                   st7789v-dbidrmf
/dev/dri/card0             exists
card0-SPI-1 status         connected
card0-SPI-1 mode           320x240
DeskBot fd                 /dev/dri/card0
DeskBot fd to /dev/fb0     none
Failed to update display   none
```

### 20.3 下一轮性能报告必须补的内容

1. 至少三轮冷启动同条件 FB/DRM A/B；
2. 关闭旧 fbtft per-transfer debug 后重测；
3. 报告中位数、最小/最大或 p95，而非只给单值；
4. 增加 DRM commit/flush 低扰动计时；
5. 记录完整 MemTotal/CMA/dma-buf 状态；
6. 循环 YOLO 生命周期并追踪 buffer 回收；
7. 有条件时用逻辑分析仪验证 SCLK、DC、reset 和全帧传输；
8. 若目标是物理无撕裂，评估 TE 接线/驱动同步或外部高速相机验证。

---

## 参考证据索引

- 旧驱动：`SDK/rv1106-sdk/sysdrv/source/kernel/drivers/staging/fbtft/fb_st7789v.c`
- fbtft core/bus：`SDK/rv1106-sdk/sysdrv/source/kernel/drivers/staging/fbtft/`
- 新驱动：`SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/tiny/st7789v.c`
- MIPI DBI 公共层：`SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/drm_mipi_dbi.c`
- Kconfig/Kbuild：`SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/tiny/{Kconfig,Makefile}`
- DT Binding：`SDK/rv1106-sdk/sysdrv/source/kernel/Documentation/devicetree/bindings/display/sitronix,st7789v-dbi.txt`
- 板级 DTSI：`SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
- defconfig：`SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig`
- LVGL 应用入口：`Demo/DeskBot_demo/main.c`
- LVGL backend 配置：`Demo/DeskBot_demo/conf/dev_conf.h`
- LVGL DRM 实现：`Demo/DeskBot_demo/lvgl/src/drivers/display/drm/lv_linux_drm.c`
- 阶段一基准：`docs/logs/st7789-drm/2026-08-27/board_fb_baseline_summary.md`
- 显示参数基准：`docs/logs/st7789-drm/2026-08-27/fb_driver_migration_baseline.md`
- 阶段二构建/首错/复测：`docs/logs/st7789-drm/2026-08-27/stage2_host_build.md`
- 阶段三交叉构建：`docs/logs/st7789-drm/2026-08-27/stage3_host_build.md`
- DRM 板端汇总：`docs/logs/st7789-drm/2026-08-27/drm/board_drm_summary.md`
- 性能汇总工具：`docs/tools/summarize_fb_display_baseline.py`
