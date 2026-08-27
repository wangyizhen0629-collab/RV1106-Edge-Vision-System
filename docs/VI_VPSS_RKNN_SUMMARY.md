# VI→VPSS→RKNN 媒体与推理链路优化总结

## 1. 阶段结论

本阶段已完成 DeskBot YOLO 主采集与预处理链路的重构：使用 RV1106 原生 VI/VPSS 代替 OpenCV `VideoCapture` 和 CPU `cv::resize`，并将 VPSS 输出的 RK MPI MB 所对应的 DMABUF 直接导入 RKNN。

当前完成状态：

- 主机侧代码实现完成，RKMPI 与不含 RKMPI 的 ARM 配置均已交叉构建通过。
- 新链已部署到 RV1106 板端，SC3336、VI、VPSS 和 RKNN 初始化成功，检测画面能够持续发布。
- 一次约 70 秒的板端功能冒烟中未出现取帧超时、恢复或处理失败。
- 旧 OpenCV backend 的当前强制回退测试出现花屏，已决定不再修复，也不将其作为性能基线。
- 尚未完成同口径 CPU/RSS 对比、2 小时回归或 8 小时长稳，因此不能声明 CPU 降低百分比、前后性能提升值或稳定运行小时数。

这项工作的准确描述是“移除主采集/预处理路径中的 OpenCV”，不是“彻底移除 OpenCV”。当前检测框绘制、显示缩放和 RGB565 转换仍使用 OpenCV。

## 2. 优化目标

原链路能够完成摄像头采集和 YOLO 推理，但采集、缩放和输入准备依赖 CPU，媒体缓冲区所有权、过期帧处理和异常恢复也不够明确。本阶段主要解决以下问题：

1. 让 VI/VPSS 硬件完成采集、缩放与像素格式转换，减少 CPU 图像预处理负担。
2. 用 MB/DMABUF 在 VPSS 与 RKNN 之间共享像素缓冲，减少应用层整帧复制。
3. 用有界 latest-wins 队列主动丢弃过期帧，避免处理速度低于摄像头帧率时延迟持续累积。
4. 明确每个 VPSS frame 的 Get/Release 所有权，避免泄漏、重复释放和销毁仍在使用的缓冲池。
5. 增加超时检测、链路重建、双缓冲发布和统一性能统计，提高可恢复性与可验证性。

## 3. 原始 OpenCV 链路

### 3.1 数据流

原始实现位于 `Demo/yolov5_demo/cpp/AIcamera_c_interface.cc`，典型数据流为：

```text
SC3336
  → MIPI CSI / RKISP / V4L2 video node
  → OpenCV VideoCapture 获取 BGR 帧
  → CPU cv::resize：摄像头帧 → 640×640 BGR
  → 写入 RKNN input memory
  → rknn_run
  → YOLO 后处理 / NMS
  → OpenCV 绘制检测框和标签
  → CPU cv::resize：检测画面 → 320×240
  → CPU cv::cvtColor：BGR888 → BGR565
  → memcpy 到全局 yolo_pic_buf
  → LVGL timer 复制并刷新 image
  → fbdev / ST7789V
```

原代码请求 `640×480` 摄像头画面，再由 CPU 缩放到模型所需的 `640×640`。每完成一次推理，代码把画框后的画面缩放为 `320×240`，转换为 RGB565，并复制给 LVGL 页面。

### 3.2 主要限制

- `VideoCapture`、模型输入缩放和格式转换都经过 CPU/OpenCV 路径。
- 摄像头帧到 RKNN 输入存在 CPU 写入整帧数据的过程，不能复用媒体子系统已经分配的 DMA buffer。
- 原应用没有显式的有界 latest-wins 用户态队列；当生产与消费速度不匹配时，无法在应用层明确控制保留最新帧。
- 推理线程直接写共享输出缓冲，LVGL 同时复制该缓冲，缺少同步时可能读到未完成帧。
- 缺少统一的取帧超时、缓冲区 outstanding 账本和媒体链路重建策略。
- 原画面 FPS 使用 `clock()` 计算。该接口统计 CPU 时间，不是完整墙钟时间，等待摄像头和 NPU 的时间可能没有被等价计入，因此此前画面上约 `14 FPS` 的数值不能与新统计直接比较。

## 4. 改进后的 VI→VPSS→RKNN 链路

### 4.1 数据流

当前板端配置与实测链路为：

```text
SC3336：2304×1296 RAW10 @ 25 FPS sensor mode
  → RKISP
  → VI channel：864×480 NV12，媒体缓冲为 MB/DMABUF
  → RK_MPI_SYS_Bind(VI, VPSS)
  → VPSS channel：硬件缩放并转换为 640×640 RGB888
  → VPSS private MB pool
  → GetChnFrame producer
  → 容量 2 的 latest-wins 有界队列
  → RK_MPI_MB_Handle2Fd：MB → DMABUF fd
  → rknn_create_mem_from_fd + rknn_set_io_mem
  → rknn_run
  → YOLO 后处理 / NMS
  → cache sync for CPU
  → OpenCV 绘框、缩放到 320×240、转换为 RGB565
  → RGB565 双缓冲发布 + frame sequence
  → LVGL 每 30 ms 检查并复制一个完整的新帧
  → fbdev / ST7789V
```

VI channel 使用绑定模式，depth 设置为 0；VI 到 VPSS 的媒体帧由 RKMPI 在模块间传递，不需要应用执行 Get/Release 或整帧复制。VPSS 负责生成与 RKNN 输入一致的 `640×640 RGB888` 图像。

### 4.2 拷贝边界

| 位置 | 当前处理方式 | 是否有应用层整帧复制 |
| --- | --- | --- |
| VI → VPSS | RKMPI bind | 否 |
| VPSS → RKNN | MB 转 DMABUF fd，RKNN 导入 fd | 否 |
| RKNN 输入描述符 | 每帧创建并销毁导入描述符 | 仅管理对象，不复制像素 |
| VPSS RGB888 → CPU 绘图 | cache 同步后通过虚拟地址读取 | CPU 访问，但不先复制整帧输入 |
| 绘图画面 → RGB565 发布缓冲 | OpenCV 转换并复制到 back buffer | 是 |
| front buffer → LVGL image data | mutex 保护下复制完整 `320×240×2` 帧 | 是 |
| LVGL → fbdev/ST7789V | 由现有显示链处理 | 本阶段未改造 |

因此，当前实现是“VPSS 到 RKNN 输入共享 DMABUF”，不能描述为 Camera 到 LCD 的全链零拷贝。

SDK 随附的 `librknnmrt.so` 没有导出头文件声明的 `rknn_create_mem_from_mb_blk`，但导出了 `rknn_create_mem_from_fd`，所以实现先通过 `RK_MPI_MB_Handle2Fd` 获得 fd，再导入 RKNN。

## 5. 我们完成的工程工作

### 5.1 媒体采集与预处理

- 新增 `RkMediaPipeline`，封装 RKAIQ、RK MPI SYS、VI、VPSS、MB pool 和 VI→VPSS bind 的初始化及逆序销毁。
- VI 输出配置为 `864×480 NV12`，VPSS 输出配置为模型输入大小 `640×640 RGB888`。
- VPSS 使用私有 MB pool，缓冲数量与有限队列容量绑定，避免无界内存增长。
- 校验 VPSS 虚拟 stride、缓冲区大小、DMABUF fd 和虚拟地址，异常帧不会进入 RKNN。

### 5.2 RKNN DMABUF 输入

- 新增 `inference_yolov5_model_dmabuf()` 路径。
- 通过 `rknn_create_mem_from_fd` 导入 VPSS MB 对应的 DMABUF，并通过 `rknn_set_io_mem` 绑定为模型输入。
- 推理完成后恢复 RKNN 内部输入绑定、销毁临时导入描述符，再释放 VPSS frame，保证 RKNN 使用期间媒体缓冲仍然有效。
- 修正 RKNN 初始化失败清理和 tensor memory/context 销毁顺序，避免重复释放。

### 5.3 有限队列与所有权

- 实现容量范围为 1～4、默认容量为 2 的固定 latest-wins 队列。
- 队列满时立即释放最旧的 VPSS frame，只保留更接近当前时刻的帧。
- 每次 `RK_MPI_VPSS_GetChnFrame` 成功后只允许对应一次 `RK_MPI_VPSS_ReleaseChnFrame`。
- 正常完成、队列丢弃、无效帧、页面退出和恢复清空都走明确的 Release 路径。
- 重建媒体链路前清空队列并等待 outstanding frame 归零，避免销毁仍被推理线程借用的 VPSS pool。

### 5.4 超时恢复

默认 VPSS 取帧超时为 200 ms。连续 5 次超时后停止交付新帧、清空队列、等待 outstanding 归零并重建 VI/VPSS 链路；最多尝试 3 次，每次间隔 100 ms。

统计项覆盖取帧 timeout、recovery attempt/success、invalid frame、release failure 和总体 failure。该机制负责媒体取帧停滞，不覆盖 RKNN 驱动内部永久阻塞。

### 5.5 显示线程安全

- 将原单个共享输出缓冲改为两个 `320×240 RGB565` 缓冲。
- 推理线程写 back buffer，完成后在 mutex 保护下切换 front buffer 并增加 frame sequence。
- LVGL 只复制已完成的 front buffer，并通过 sequence 跳过重复帧。
- 页面等待第一个完整帧后才设置图像源，避免显示未初始化数据。

### 5.6 构建、部署与运行

- DeskBot CMake 接入 Rockit、MPP、RGA、RKAIQ 和 RKNN runtime，并将运行库复制到 `bin/lib`。
- ARM 主程序 RPATH 设置为 `$ORIGIN/lib`，OEM 应用可从自身目录加载媒体运行库。
- 支持 `DESKBOT_CAMERA_BACKEND=auto|rkmpi|opencv`；当前板端测试和后续使用统一强制 `rkmpi`：

```bash
DESKBOT_CAMERA_BACKEND=rkmpi /oem/usr/bin/RkLunch.sh restart
```

## 6. 板端冒烟数据

测试条件：RV1106 Echo-Mate、SC3336、VI 请求 `864×480 NV12`、VPSS/RKNN 输入 `640×640 RGB888`、YOLOv5 INT8、队列容量 2、预热丢弃 30 个成功帧。

板端累计统计记录：

```text
[ai_camera_metrics] final=0 backend=rkmpi uptime_s=70.127 frames=600
published=630 warmup=30 acquired=1734 fps=9.091
e2e_avg_ms=171.696 e2e_p95_ms=189.000
infer_avg_ms=88.512 infer_p95_ms=93.000
queue_avg_ms=60.261 queue_p95_ms=76.000
queue_drop=1102 timeouts=0 recoveries=0 failures=0 max_queue=2
media_pts=600 software_start=0
```

| 指标 | 实测值 | 含义 |
| --- | ---: | --- |
| 采集速率 | 约 24.7 FPS | `1734 / 70.127`，接近 25 FPS sensor mode |
| 有效推理/发布帧率 | 9.091 FPS | 完成推理、后处理、绘图并发布到 RGB565 front buffer 的速率 |
| 端到端平均时延 | 171.696 ms | 媒体 PTS 到 RGB565 front buffer 发布 |
| 端到端 p95 | 189 ms | 不包含 LVGL、fbdev、SPI scanout 和面板光学响应 |
| 推理平均耗时 | 88.512 ms | RKNN 推理加 YOLO 后处理 |
| 推理 p95 | 93 ms | 当前吞吐的主要瓶颈 |
| 队列平均等待 | 60.261 ms | VPSS Get 成功到 inference pop |
| 队列 p95 | 76 ms | 容量 2 时的排队延迟 |
| 主动丢弃旧帧 | 1102 | latest-wins 延迟控制，不是驱动丢帧 |
| timeout / recovery / failure | 0 / 0 / 0 | 该次短时冒烟未触发异常路径 |
| 媒体 PTS 覆盖 | 600 / 600 | 测量帧均使用媒体时间戳 |

日志中的 `9.091 FPS` 不是 LCD 的物理刷新率，也不是单独的 `rknn_run` 调用频率，而是完整推理与画面发布吞吐。LVGL timer 约以 33.3 Hz 检查新帧，但屏幕每秒能够获得的不同检测画面不会高于应用发布速率；显示端若阻塞，实际呈现还可能更低。

当前推理 p95 为 93 ms，仅推理阶段的理论上限约为 `1000 / 93 = 10.75 FPS`。加入后处理、绘框、显示缩放和 RGB565 转换后得到约 9.1 FPS 符合现有耗时组成。此前旧页面显示的约 14 FPS 使用不同且不可靠的 CPU-time 口径，不能据此认定新链 FPS 下降。

## 7. 为什么体感更流畅

体感流畅不仅由吞吐 FPS 决定，还取决于画面新鲜度、帧间抖动和是否出现撕裂：

- 摄像头约产生 25 FPS，而处理链只能发布约 9.1 FPS；latest-wins 队列主动丢弃旧帧，避免按顺序播放过期画面。
- 有界队列限制了最大积压量，延迟不会因运行时间增加而无界增长。
- RGB565 双缓冲确保 LVGL 只读取完整帧，消除了推理线程和 UI 同时访问单缓冲的竞争。
- VI/VPSS 让采集、缩放和格式转换由媒体硬件执行，减少 CPU 路径的负担和时序抖动。

因此，新链的主要收益是降低 CPU 预处理与复制开销、控制排队延迟并改善帧发布稳定性；它不会自动加速 YOLO 模型，当前总吞吐仍主要受 RKNN 推理和后处理限制。

## 8. 完成边界与未声明数据

| 项目 | 状态 |
| --- | --- |
| VI→VPSS→RKNN 主链实现 | 已完成 |
| VPSS MB/DMABUF 导入 RKNN | 已完成 |
| 有限队列、所有权和超时重建 | 已完成 |
| ARM 交叉构建与 OEM 部署 | 已完成 |
| 板端新链初始化、取帧、推理和显示冒烟 | 已完成 |
| 新链短时性能统计 | 已完成，约 70 秒 |
| OpenCV 与 RKMPI 同条件 A/B | 已取消，不声明前后提升值 |
| 新链 CPU/RSS 实测 | 未完成 |
| 30 分钟/2 小时/8 小时稳定性 | 未完成 |
| LED/高速相机物理端到端延迟 | 未完成 |

因此，当前不能写：

- “CPU 占用由 A% 降至 B%”。
- “端到端延迟由 A ms 降至 B ms”。
- “帧率由 A 提升至 B FPS”。
- “稳定运行 X 小时”。

可以写已经有证据支持的实现事实和新链绝对指标。

## 9. 当前简历表述

> 媒体与推理链路优化：将 OpenCV `VideoCapture`/CPU resize 预处理重构为 VI→VPSS→RKNN 链路，通过 RK MPI MB/DMABUF 将 VPSS 输出直接导入 RKNN；在 SC3336 864×480 输入、YOLOv5 640×640 模型下，实现约 9.1 FPS 的有效推理/发布帧率和 189 ms 端到端 p95，并通过容量为 2 的 latest-wins 有限队列、显式 Get/Release 所有权及连续超时重建控制帧积压和异常恢复。

若后续完成长稳，可在句末据实增加“实现 X 小时稳定运行”；若补齐同条件 CPU 采样，可增加 CPU 占用的实测变化。没有对应日志前不补数字。

## 10. 关键实现文件

| 文件 | 作用 |
| --- | --- |
| `Demo/yolov5_demo/cpp/rk_media_pipeline.h` | 媒体 pipeline 配置、frame 和统计接口 |
| `Demo/yolov5_demo/cpp/rk_media_pipeline.cc` | RKAIQ、VI、VPSS、MB、队列、所有权及恢复实现 |
| `Demo/yolov5_demo/cpp/AIcamera_c_interface.cc` | backend 编排、DMABUF 推理、绘图发布和性能统计 |
| `Demo/yolov5_demo/cpp/rknpu2/yolov5_rv1106_1103.cc` | RKNN tensor memory 与 DMABUF 输入适配 |
| `Demo/DeskBot_demo/gui_app/pages/ui_YOLOPage/ui_YOLOPage.c` | LVGL 完整帧读取、sequence 和页面生命周期 |
| `Demo/yolov5_demo/cpp/CMakeLists.txt` | RKMPI/RKNN/OpenCV 编译链接配置 |
| `Demo/DeskBot_demo/CMakeLists.txt` | DeskBot 媒体运行库部署与 RPATH |
| `docs/VI_VPSS_RKNN_VALIDATION.md` | 测量口径、运行参数和后续长稳协议 |
| `docs/tools/summarize_ai_camera_metrics.py` | 多轮性能日志汇总工具 |
