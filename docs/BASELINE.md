# Echo-Mate 原项目基线

## 基线说明

当前基线是“原开源 Echo-Mate / DeskBot 已在 RV1106 板子上完整复现全部功能”。本文先依据仓库现状梳理软件结构和数据流；板卡修订、固件版本、运行指标及原始日志仍需在实机补采。

任何性能优化开始前，应记录对应 Git commit、SDK/固件版本、BoardConfig、顶层 DTS/DTB、内核版本、测试场景和采样方法。

## 主机侧版本记录

2026-07-08 已在主机上创建 Baseline 记录分支：

- 分支：`baseline/board-metrics`
- 记录锚点：`198322396726c401c2cd2e1d69000ca395cc0974`
- 采集前工作区：`git status --short` 无输出，表示干净。
- 远程仓库：`origin` → `https://github.com/wangyizhen0629-collab/Echo-mate-v1.git`
- 主机侧日志：`docs/logs/baseline/2026-07-08/host_git_state.md`

注意：以上只完成主机 Git/仓库状态记录，RV1106 板端系统信息、设备枚举、功能状态和性能数据仍需补采。

## 当前功能

### GUI

- 核心应用位于 `Demo/DeskBot_demo/`。
- `Demo/DeskBot_demo/main.c` 初始化 LVGL、显示、输入和 `ui_init()`，主循环调用 `lv_timer_handler()`。
- 页面管理与注册位于 `Demo/DeskBot_demo/gui_app/ui.c`、`ui.h`，具体页面位于 `gui_app/pages/`。
- 已集成天气、日历、计算器、小游戏、YOLO 和 AI Chat 等功能；实际页面清单以后续基线采集时运行版本为准。

### YOLO

- 主要实现位于 `Demo/yolov5_demo/cpp/`。
- `AIcamera_c_interface.cc` 创建独立 pthread，加载 RKNN YOLOv5 模型并执行推理、后处理和画框。
- 当前输出方式是将已画框画面的 RGB565 像素复制到 `yolo_pic_buf`，DeskBot/LVGL 侧读取像素缓冲刷新页面。
- 当前代码存在采集、缩放、推理、画框、再次缩放、颜色转换和内存复制等环节，后续优化需要逐段计时，不能只比较最终 FPS。

当前主要链路：

```text
SC3336 / video node
→ OpenCV-mobile cv::VideoCapture
→ V4L2 backend
→ cv::Mat (BGR)
→ cv::resize 到 RKNN 输入缓冲
→ RKNN inference
→ post-process / NMS
→ OpenCV rectangle / text
→ resize 到 320×240
→ BGR 转 RGB565
→ memcpy 到 yolo_pic_buf
→ LVGL 页面刷新
```

**基线事实：当前 YOLO 采图链路仍主要使用 OpenCV-mobile / V4L2 / `cv::VideoCapture`，尚未由 DeskBot 主链路直接使用 RKMPI VI/VPSS 取帧。**

### AI Chat

- 独立 Client/Server 实现位于 `Demo/AIChat_demo/`。
- DeskBot 接入点位于：
  - `Demo/DeskBot_demo/gui_app/pages/ui_ChatBotPage/app_ChatBotPage.c`
  - `Demo/DeskBot_demo/gui_app/pages/ui_ChatBotPage/ui_ChatBotPage.c`
- 板端通过 WebSocket + JSON + Opus 与服务端通信；服务端组合 VAD、ASR、Intent、LLM 和 TTS。
- ChatBot 页面使用独立线程处理可能阻塞的网络/音频工作，需关注退出、重连和共享状态同步。

### 显示

- `Demo/DeskBot_demo/main.c` 已包含 LVGL 的 fbdev、DRM 和 SDL 三种编译期初始化路径。
- 当前 `Demo/DeskBot_demo/conf/dev_conf.h` 中：
  - 仿真模式使用 SDL。
  - 非仿真模式使用 Linux fbdev。
- DRM 路径默认设备为 `/dev/dri/card0`，但是否已在当前板端固件和屏幕链路实际启用，仍需实机验证。
- Echo-Mate DTSI 中已发现 `compatible = "sitronix,st7789v"` 的节点；这只证明仓库存在配置，不能替代驱动绑定、DRM 枚举和显示效果验证。

### 摄像头

- 当前传感器目标为 SC3336。
- 仓库中已确认存在：
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-ipc.dtsi`
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-evb-cam.dtsi`
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi`
  - SC3336 ISP IQ 文件。
- Echo-Mate DTSI 中存在 SC3336 的 I2C sensor 节点和 MIPI endpoint；实际生效的顶层 DTS、时钟/GPIO 接线、lane 配置及完整 media graph 待板级配置和实机日志确认。

预期硬件/媒体链路：

```text
SC3336
→ I2C 控制 + xvclk + reset/pwdn GPIO
→ MIPI D-PHY / CSI-2
→ rkcif
→ rkisp
→ V4L2 video node / RKMPI VI
→ VPSS（后续优化）
→ RKNN / display
```

### 构建方式

DeskBot x86 仿真：

```bash
cd Demo/DeskBot_demo
mkdir -p build
cd build
cmake ..
make
```

DeskBot ARM 交叉编译：

```bash
cd Demo/DeskBot_demo
mkdir -p build
cd build
cmake .. -DTARGET_ARM=ON
make
```

SDK 固件：

```bash
cd SDK/rv1106-sdk
./build.sh lunch
./build.sh
```

TODO：记录当前实机所选 lunch/BoardConfig、输出固件名、烧录方式和可重复构建环境。

## 已确认的参考实现

- 当前 OpenCV 采图：`Demo/yolov5_demo/cpp/AIcamera_c_interface.cc`
- RKMPI VI + YOLO 示例：`Demo/rkmpi_demos/example/luckfox_pico_rtsp_yolov5/`
- RKMPI VI/VPSS 示例：`Demo/rkmpi_demos/example/luckfox_pico_rtsp_opencv_capture/`
- LVGL DRM 驱动：`Demo/DeskBot_demo/lvgl/src/drivers/display/drm/`
- LVGL ST7789 通用驱动：`Demo/DeskBot_demo/lvgl/src/drivers/display/st7789/`

## Baseline 待补采数据

| 指标 | 当前值 | 建议方法 | 测试约束 |
| --- | --- | --- | --- |
| YOLO 有效 FPS | TODO | 应用内单调时钟统计固定窗口内成功显示/推理帧数 | 同一模型、分辨率、场景，至少 3 轮 |
| 采集耗时 | TODO | 在取帧前后打点 | 区分阻塞等待与处理时间 |
| 预处理耗时 | TODO | 分别统计 resize、格式转换、拷贝 | 记录输入输出格式与尺寸 |
| RKNN 推理耗时 | TODO | 推理调用前后打点 | 预热后统计 |
| 后处理/画框耗时 | TODO | NMS、绘制、最终 resize 分段统计 | 同一目标数量或记录场景差异 |
| 端到端延迟 | TODO | LED/屏幕事件、高速相机或统一时间戳方案 | 明确定义起点和终点 |
| 进程 CPU 占用 | TODO | `top`/`pidstat` 或 `/proc/<pid>/stat` 周期采样 | 固定采样周期和运行时长 |
| RSS/峰值内存 | TODO | `/proc/<pid>/status` 的 VmRSS/VmHWM | 记录进入/退出 YOLO 前后 |
| 启动时间 | TODO | 从进程启动到首页可交互；YOLO 页面另测首次出帧 | 每项至少 3 次 |
| 丢帧/超时 | TODO | 应用计数器、VI/V4L2 日志 | 记录总帧数和错误类型 |
| 长稳运行 | TODO | 连续运行 30 分钟起步 | 记录温度、内存趋势、错误日志 |
| 显示刷新率/撕裂 | TODO | DRM/fbdev 状态与可视化测试 | 记录接口、像素格式、旋转 |
| AI Chat 时延 | TODO | listening→thinking→speaking 分段时间戳 | 固定网络与服务端模型 |

## Baseline 采集清单

- [x] 记录 Git commit 和工作区状态（主机侧已记录，见 `docs/logs/baseline/2026-07-08/host_git_state.md`）。
- [ ] TODO：记录板型、硬件修订、SC3336 模组、ST7789V 屏幕和供电方式。
- [ ] TODO：记录 BoardConfig、顶层 DTS/DTB、内核版本和 rootfs 信息。
- [ ] TODO：保存完整启动 `dmesg`。
- [ ] TODO：保存 `/dev/video*`、`/dev/media*`、`/dev/fb*`、`/dev/dri/*` 枚举。
- [ ] TODO：保存 `media-ctl -p` 和关键 `v4l2-ctl --all` 输出。
- [ ] TODO：采集上表中 YOLO 性能数据。
- [ ] TODO：记录 GUI、AI Chat、摄像头和显示的功能性检查结果。
- [ ] TODO：将日志路径和结果回填到 `docs/PROGRESS.md`。
