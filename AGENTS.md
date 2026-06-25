# AGENTS.md

本文件是 Echo-Mate 从 Claude Code 迁移到 Codex 后的项目上下文。它保留 `CLAUDE.md` 中的核心信息，并补充 Codex 协作约定。后续 Codex 会话应优先读取和遵守本文件。

## Highest Priority Rules

- 默认只修改 `Demo/DeskBot_demo/` 相关文件，除非任务明确涉及 SDK、内核、uboot、buildroot 或板级配置。
- 修改前必须先查看 `git status --short`，不得覆盖用户已有改动。
- 不要主动重构无关代码。
- 不要修改大模型文件、二进制库、生成图片数组、SDK 产物或第三方源码，除非用户明确要求。

## Project Overview

Echo-Mate 是一个基于 RV1106 芯片的嵌入式桌面 AI 机器人项目。核心应用是 `Demo/DeskBot_demo/`，集成 LVGL 图形界面、AI 语音助手（VAD + ASR + LLM + TTS）、YOLO 目标检测、天气、日历、计算器和小游戏等功能。

仓库同时包含 SDK 层（Linux kernel、uboot、buildroot、Rockchip media stack、板级配置等），是一套从底层系统到上层桌面助手应用的完整嵌入式方案。

## Codex Collaboration Rules

- 默认用中文回复，除非用户明确要求英文。
- 先保护用户现有改动：动手前用 `git status --short` 看工作区；不要覆盖无关修改。
- 只修改与当前任务相关的文件。对 SDK、大模型、二进制库、生成图片数组等大文件要格外谨慎。
- 文件搜索优先使用 `rg` / `rg --files`。
- 本仓库是嵌入式项目，遇到交叉编译、驱动、内核模块、实时/硬件相关问题时，优先使用仓库内 `.agents/skills` 对应技能：
  - `cross-gcc`：交叉编译、sysroot、工具链、QEMU。
  - `embedded-systems`：固件、外设、中断、实时性、资源约束。
  - `linux-kernel-modules`：内核模块、Kbuild、sysfs、procfs、模块签名。
- 进行实现类任务时，完成后尽量给出已执行/未执行的验证命令。不能验证时说明原因。
- 不要依赖 `.claude/settings.local.json` 中的 Claude 权限配置；Codex 的命令权限以当前会话环境为准。

## Build & Run

### DeskBot_demo（核心应用）

```bash
# x86 桌面仿真
cd Demo/DeskBot_demo
mkdir -p build
cd build
cmake ..
make

# ARM 交叉编译（RV1106 实机）
cd Demo/DeskBot_demo
mkdir -p build
cd build
cmake .. -DTARGET_ARM=ON
make
```

### SDK（系统固件）

```bash
cd SDK/rv1106-sdk
./build.sh lunch   # 选择 Echo-Mate 板级配置
./build.sh         # 编译 uboot + kernel + rootfs + media + firmware
```

### Validation Priority

优先级从高到低：

1. 只改 DeskBot 应用时，优先在 `Demo/DeskBot_demo/build` 下运行 `make`。
2. 修改 C/C++ 接口、页面注册或 CMake 时，重新运行 `cmake .. && make`。
3. 只有涉及 SDK、kernel、uboot、buildroot、rootfs、media stack 时，才考虑运行 `SDK/rv1106-sdk/build.sh`。
4. 如果无法在当前环境验证，必须说明未验证原因和建议用户在实机上运行的命令。

### 其他 Demo

- `Demo/AIChat_demo/`：AI 语音助手 Client/Server 独立验证。
- `Demo/yolov5_demo/`：YOLO 目标检测，ARM 平台为主，依赖 RKNN。
- `Demo/rkmpi_demos/`：RKMPI 媒体处理示例，依赖 Luckfox SDK 环境。
- `Demo/lvgl_demo/`：早期 LVGL 测试。

## Architecture Map

| 模块 | 路径 | 职责 |
| --- | --- | --- |
| DeskBot 核心应用 | `Demo/DeskBot_demo/` | 主应用入口，集成所有功能；`main.c` 初始化 LVGL 和 UI |
| GUI 页面层 | `Demo/DeskBot_demo/gui_app/` | UI 业务逻辑；`ui.c` 页面管理，`pages/` 下每个目录是一个功能页面 |
| LVGL 图形引擎 | `Demo/DeskBot_demo/lvgl/` | 图形渲染、事件处理、动画能力 |
| 硬件抽象层 | `Demo/DeskBot_demo/common/` | `sys_manager/`、`gpio_manager/`、`event_manager/` 等硬件/事件封装 |
| AI 语音助手 | `Demo/AIChat_demo/` | Client/Server 架构，通过 WebSocket + JSON + Opus 通信 |
| SDK 系统层 | `SDK/rv1106-sdk/` | kernel、uboot、buildroot、media、板级配置和固件构建 |

## Display & Video Pipeline

显示驱动在 `Demo/DeskBot_demo/main.c` 中通过编译期宏选择：

- `LV_USE_LINUX_FBDEV`：`/dev/fb0`，简单可靠，ARM 嵌入式兜底路径。
- `LV_USE_LINUX_DRM`：`/dev/dri/card0`，支持 KMS/page flip，适合实机流畅显示。
- `LV_USE_SDL`：x86 桌面仿真窗口。

视频能力储备链路：

```text
Camera Sensor → MIPI/CSI → VI → VPSS → RGA → DRM → LCD
```

注意：当前 YOLO 页面主要使用 OpenCV `cv::VideoCapture`（底层 V4L2），而不是直接走 RKMPI VI/VPSS。

## Important Data Flows

### YOLO

典型链路位于 `Demo/yolov5_demo/cpp/AIcamera_c_interface.cc`：

```text
V4L2/OpenCV capture
→ resize 到 RKNN input DMA buffer
→ rknn_run
→ post_process + NMS
→ OpenCV 画框
→ BGR/RGB565 转换
→ 写入 yolo_pic_buf
→ LVGL timer 刷新图像
```

设计上向 LVGL 传递“已画好框的像素帧”，而不是检测结果结构体，以降低 UI 层和 YOLO 逻辑耦合。

### AI Chat

```text
RV1106 Client
  Mic → PCM → Opus → WebSocket
  Speaker ← PCM ← Opus ← WebSocket
  idle ⇄ listening ⇄ thinking ⇄ speaking
        ↓
PC Server
  VAD(FSMN) → ASR(SenseVoice) → Intent(FastText) → LLM(通义千问) → TTS(CosyVoice)
```

DeskBot 集成点：

- `Demo/DeskBot_demo/gui_app/pages/ui_ChatBotPage/app_ChatBotPage.c`
- `Demo/DeskBot_demo/gui_app/pages/ui_ChatBotPage/ui_ChatBotPage.c`

## Threading Model

- 主应用大多数逻辑运行在单线程 LVGL 主循环：`lv_timer_handler()`。
- 例外：
  - ChatBot 页面会创建 AI chat 线程处理 WebSocket 阻塞 I/O。
  - YOLO 推理使用独立 pthread。
- 处理线程共享数据时，优先评估嵌入式实际约束和 C/C++ 标准层面的 data race 风险。必要时用 `volatile`、原子变量、互斥锁或双缓冲明确同步语义。

## Incremental Development Plan

当前二次开发以“保留原项目能力、形成可验证增量”为原则，不以重写 DeskBot 为目标。长期路线与阶段状态记录在根目录 `docs/`：

1. Baseline：固化当前功能、构建方式、运行链路和性能基线。
2. LED DTS：通过设备树控制蓝色 LED，并使用 `/sys/class/leds`、`dmesg` 和实物状态验证。
3. SC3336 DTS / Camera Pipeline：对比原厂 SDK 与当前 Echo-Mate SDK，梳理 I2C、`compatible`、`xvclk`、reset/pwdn GPIO、MIPI endpoint、`data-lanes`、rkcif、rkisp 和 VI 节点。
4. VI/VPSS Optimization：用 RV1106 原生 VI/VPSS 逐步替换 OpenCV-mobile / V4L2 / `cv::VideoCapture` 采图路径，并保留可回退方案。
5. ST7789V DRM：调研并适配 DRM/tinyDRM/mipi-dbi 链路，最终接入 LVGL。
6. Performance Report：汇总 FPS、CPU、内存、启动时间、端到端延迟和稳定性对比。

上述路线会涉及 SDK、kernel、uboot、buildroot 和设备树，但它不自动扩大任何单次任务的修改范围。**只有当前任务明确要求时，才允许修改这些目录或文件。**仅做调研、文档或应用层任务时，继续遵守默认不修改底层系统的规则。

## Documentation Maintenance Rules

- 根目录 `docs/ROADMAP.md` 维护阶段目标、涉及目录、预期产出、验证命令和完成标准。
- 根目录 `docs/PROGRESS.md` 是持续更新的任务日志。每次任务记录日期、目标、修改文件、验证命令、日志路径、结果和遗留问题。
- 根目录 `docs/BASELINE.md` 描述原项目基线。优化前先补齐对应指标，优化后使用相同输入、分辨率、模型、运行时长和测量方法复测。
- 设备树、驱动和媒体链路结论必须尽量附带证据：源码/DTS 路径、关键节点、板端命令、内核日志或采样数据。不能验证的内容标为“待验证”，不要写成已确认事实。
- 板端日志和性能数据建议保存在 `docs/logs/<stage>/<YYYY-MM-DD>/`；大体积二进制、原始视频和固件镜像不提交 Git，只记录外部存放位置与校验信息。
- 修改行为与文档状态同步：任务未验证时保持 TODO 或“部分完成”，不得仅凭代码合入标记为完成。

## Git Workflow

- 每次任务开始前运行 `git status --short`，识别并保护用户已有修改。
- 一个分支聚焦一个可验证主题，建议使用 `feat/led-dts`、`research/sc3336-pipeline`、`perf/vi-vpss`、`feat/st7789v-drm` 等命名。
- 不覆盖、不清理、不顺带格式化与当前任务无关的文件；发现重叠修改时先缩小改动范围，无法安全处理再向用户确认。
- 默认不自动执行 `git commit`、`git push`、变基或历史重写。完成任务后提供建议分支名和 commit message。
- SDK 或内核阶段优先将“配置/设备树改动”“驱动实现”“应用接入”“性能报告”拆分为可独立审查、可回退的提交。
- 提交前再次运行 `git status --short` 和 `git diff --check`，确认只有预期文件且无空白错误。

## Per-Stage Validation Requirements

- Baseline：记录固件/代码版本、板型、摄像头、屏幕、模型、构建参数和测试场景；至少采集 FPS、CPU、内存、启动时间、端到端延迟的原始值或明确 TODO。
- LED DTS：验证 DTB/固件构建成功、LED class 设备出现或按预期消失、亮灭/trigger 可控，并保存 `dmesg` 与 `/sys/class/leds` 结果。
- SC3336：验证 I2C 探测、sensor probe、media graph、video 节点、取帧和稳定运行；保存 `dmesg`、`media-ctl`、`v4l2-ctl` 输出。
- VI/VPSS：在同一测试条件下比较旧链路和新链路；至少记录有效帧率、进程 CPU、RSS/峰值内存、丢帧/超时和端到端延迟。
- ST7789V DRM：验证驱动 probe、SPI/DBI 通信、DRM connector/mode、测试图、LVGL 刷新、方向/颜色和持续运行稳定性。
- Performance Report：说明测量工具、采样周期、测试次数和环境；报告平均值、波动或分位值，并保留原始日志路径。
- 只改文档时无需执行应用或 SDK 全量构建，但必须进行路径核对、`git diff --check` 和文档内容检查，并明确“未执行板端验证”。

## Key File Index

| 文件 | 角色 |
| --- | --- |
| `Demo/DeskBot_demo/main.c` | 应用入口：LVGL/display/input/UI 初始化和主循环 |
| `Demo/DeskBot_demo/gui_app/ui.c` | 页面管理器、页面注册表、系统定时器 |
| `Demo/DeskBot_demo/gui_app/ui.h` | 页面数据结构和系统参数 |
| `Demo/DeskBot_demo/gui_app/common/lv_lib.h` | 页面管理器、动画、栈等 UI 扩展库入口 |
| `Demo/DeskBot_demo/docs/` | 架构、权衡、复盘、新页面开发等设计文档 |
| `Demo/yolov5_demo/cpp/yolov5.h` | YOLO RKNN 推理上下文和 API |
| `Demo/yolov5_demo/cpp/AIcamera_c_interface.cc` | YOLO 采集、推理、后处理、输出缓冲和线程管理 |
| `Demo/AIChat_demo/Client/c_interface/AIchat_c_interface.h` | AI Chat C 接口 |
| `Demo/AIChat_demo/Server/service_manager.py` | 服务端 AI 服务编排 |
| `docs/ROADMAP.md` | 二次开发阶段路线与完成标准 |
| `docs/PROGRESS.md` | 后续任务进度、验证与遗留问题日志 |
| `docs/BASELINE.md` | 原项目功能、链路和性能基线 |

## Adding a New DeskBot Page

`Demo/DeskBot_demo/gui_app/CMakeLists.txt` 使用 `file(GLOB_RECURSE GUI_APP_SOURCES "*.c")` 自动扫描 `.c` 文件。新增页面通常放在：

```text
Demo/DeskBot_demo/gui_app/pages/ui_<Name>Page/
├── ui_<Name>Page.c
├── ui_<Name>Page.h
├── app_<Name>Page.c      # 可选：业务逻辑
└── app_<Name>Page.h      # 可选
```

然后在 `gui_app/ui.c` 的页面注册表中接入即可。

## Out-of-Scope / Do Not Touch By Default

除非用户明确要求，不要修改：

- `SDK/rv1106-sdk/` 下的 kernel、uboot、buildroot、toolchain、media stack
- 第三方库源码
- 大模型、RKNN 模型、二进制库、固件镜像
- 自动生成的图片数组、字体数组、资源数组
- `.claude/` 旧配置

## Migration Notes

- `CLAUDE.md` 保留为旧 Claude Code 上下文，不在迁移时删除。
- `.claude/` 目录属于 Claude Code 配置；Codex 不读取其中的权限配置。
- `AGENTS.md` 是 Codex 项目说明的主入口。后续如果更新项目长期约定，请优先更新本文件。
