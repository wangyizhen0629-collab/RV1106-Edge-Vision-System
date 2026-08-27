# VI→VPSS→RKNN 优化与验证

## 当前结论

主机侧实现、ARM 交叉构建、OEM 部署和 RKMPI 新链板端功能冒烟已经完成。一次约 70 秒的累计记录得到有效推理/发布帧率 9.091 FPS、采集 PTS 到 RGB565 front buffer 发布的 p95 189 ms，且 timeout/recovery/failure 均为 0；完整数据与链路总结见 `docs/VI_VPSS_RKNN_SUMMARY.md`。

旧 OpenCV backend 的当前强制测试出现花屏，已决定不再修复或作为性能基线，因此不执行 OpenCV/RKMPI A/B，也不声明延迟或 FPS 的前后提升值。CPU/RSS 和 2 小时/8 小时长稳仍待验证，不能填写 CPU 降幅或稳定运行小时数。

默认测试条件与现有板端证据对齐：SC3336 运行在 2304×1296 RAW10@25 FPS mode，应用侧请求 864×480，YOLOv5 RKNN 输入为 640×640×3 UINT8 NHWC，模型 SHA-256 为 `7e72c8caa060ea15d69109e4af6c1f80005cbd1be14bbe1f6f2af9175861c44a`。

## 实现链路与复制边界

```text
SC3336 → RKISP
  → VI channel：864×480 NV12，DMABUF，绑定模式 depth=0
  → VI→VPSS bind
  → VPSS channel：640×640 RGB888，MB pool
  → 有界 latest-wins 队列（默认 2，最大 4）
  → MB handle → DMABUF fd
  → rknn_create_mem_from_fd + rknn_set_io_mem
  → rknn_run + YOLO 后处理
  → CPU 绘框/缩放/转 RGB565
  → 320×240 双缓冲发布 → LVGL
```

- VI 到 VPSS 通过 RKMPI bind 传递媒体缓冲，不做应用层整帧复制。
- VPSS 负责模型输入尺寸和 RGB888 格式转换；RKNN 直接导入 VPSS MB 对应的 DMABUF，去掉旧链路的 CPU `cv::resize` 和向 RKNN 输入缓冲写整帧数据。
- 当前随 SDK 提供的 `librknnmrt.so` 导出了 `rknn_create_mem_from_fd`，但没有导出头文件声明的 `rknn_create_mem_from_mb_blk`，所以实现采用 `RK_MPI_MB_Handle2Fd` 后按 fd 导入。这是共享像素缓冲，不是像素复制；每帧仍有导入描述符的创建/销毁开销。
- 推理完成后，为 CPU 绘框和显示转换执行 cache 同步。RGB888→RGB565、推理线程→发布双缓冲、发布缓冲→LVGL image data 仍有必要的 CPU 访问/复制，因此不能表述为“Camera 到屏幕全链零拷贝”。
- OpenCV 仍用于后处理绘图、显示缩放及旧 backend 回退；优化结论应写“移除主采集/预处理路径中的 OpenCV `VideoCapture`/CPU resize”，而不是“彻底移除 OpenCV”。

## 缓冲区所有权

| 阶段 | 所有者 | 交接与释放规则 |
| --- | --- | --- |
| VI 内部帧 | VI | bind 模式不由应用 `Get/Release`，VI channel depth 为 0 |
| VPSS 输出 MB | VPSS → producer | `RK_MPI_VPSS_GetChnFrame` 成功后计入 outstanding |
| 有界队列 | producer → inference | 队列满时释放最旧帧，保留最新帧，避免无界延迟 |
| RKNN 输入 | inference 临时借用 | 导入 DMABUF、推理、恢复 RKNN 内部输入绑定、销毁导入描述符，然后才 Release VPSS frame |
| 页面输出 | inference/LVGL | inference 写 back buffer 后在 mutex 临界区切换 front；LVGL 在同一 mutex 下复制完整 front frame，并用 sequence 跳过重复帧 |

页面退出、队列丢帧、异常帧和正常推理都只有一个 VPSS Release 路径。恢复前先清空队列，再等待 outstanding 归零，避免 VPSS pool 尚被消费者持有时销毁 group。

## 超时与恢复

默认 `GetChnFrame` 超时为 200 ms。连续 5 次超时后执行：

1. 停止向消费者交付新帧并释放队列内旧帧。
2. 等待推理线程归还正在使用的 MB。
3. 依次 UnBind、Disable VPSS channel、Stop/Destroy VPSS group、Disable VI channel/device、退出 MPI 和 RKAIQ。
4. 最多重建 3 次，每次间隔 100 ms；统计 timeout、recovery 和 failure。

该机制处理媒体取帧停滞；RKNN 驱动内部永久阻塞不属于这一路超时能够恢复的范围，长稳测试仍需结合 `dmesg`、温度和进程存活状态判断。

## 构建与部署

```bash
cmake -S Demo/DeskBot_demo -B /tmp/echo-mate-vivpss-build \
  -DTARGET_ARM=ON \
  -DRV1106_SDK_PATH="$PWD/SDK/rv1106-sdk" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/echo-mate-vivpss-build --target main -j1
```

DeskBot 构建会把 `librockit.so`、`librockchip_mpp.so.1`、`librkaiq.so`、`librga.so` 和 `librknnmrt.so` 复制到 `bin/lib`。SDK OEM 打包沿用现有 app 入口：

```bash
cd SDK/rv1106-sdk
./build.sh app
```

## 运行时选择

同一个 ARM 二进制支持以下 backend：

```bash
# 强制旧链，用于基线
export DESKBOT_CAMERA_BACKEND=opencv

# 强制新链；初始化失败时不静默回退，正式 A/B 必须使用此模式
export DESKBOT_CAMERA_BACKEND=rkmpi

# 默认：优先 RKMPI，初始化失败时回退 OpenCV
export DESKBOT_CAMERA_BACKEND=auto
```

通过 OEM 启动脚本运行时，变量必须进入新进程环境，例如：

```bash
DESKBOT_CAMERA_BACKEND=rkmpi /oem/usr/bin/RkLunch.sh restart
```

可调参数：

| 环境变量 | 默认值 | 含义 |
| --- | ---: | --- |
| `DESKBOT_CAMERA_SOURCE_WIDTH` | 864 | VI/OpenCV 请求宽度 |
| `DESKBOT_CAMERA_SOURCE_HEIGHT` | 480 | VI/OpenCV 请求高度 |
| `DESKBOT_CAMERA_QUEUE` | 2 | 用户态队列容量，范围 1–4 |
| `DESKBOT_CAMERA_TIMEOUT_MS` | 200 | 单次 VPSS 取帧超时 |
| `DESKBOT_CAMERA_RECOVERY_THRESHOLD` | 5 | 连续超时恢复阈值 |
| `DESKBOT_CAMERA_MANAGE_ISP` | 1 | 是否由 backend 启停 RKAIQ |
| `DESKBOT_IQ_DIR` | `/oem/usr/share/iqfiles` | RKAIQ IQ 目录 |
| `DESKBOT_CAMERA_WARMUP_FRAMES` | 30 | 不计入 FPS/延迟的预热成功帧 |

若系统已有独立 RKAIQ 管理进程，应设置 `DESKBOT_CAMERA_MANAGE_ISP=0`，避免重复占用 ISP。正式测试必须在日志中确认 `backend=opencv` 或 `backend=rkmpi`，不能使用 `auto` 的结果填简历。

## 可选的同条件 A/B 协议

当前已取消旧 OpenCV A/B。本节仅保留为未来重新建立可用旧链基线时的协议，不能用于补写本阶段已经不存在的对比数据。若重新执行，OpenCV 和 RKMPI 应各运行至少 3 轮，每轮建议 10 分钟；模型、sensor mode、864×480 输入请求、640×640 模型、场景、照明、CPU/NPU 频率策略、显示页面和队列配置保持不变。

每轮执行：

```bash
log_file=/userdata/deskbot/deskbot.log
log_offset=$(wc -c < "$log_file")
pid=$(cat /var/run/deskbot.pid)
cat /proc/"$pid"/status > run_status_start.txt
top -b -d 1 -n 600 -p "$pid" > run_top.txt
cat /proc/"$pid"/status > run_status_end.txt
tail -c +"$((log_offset + 1))" "$log_file" | \
  grep '\[ai_camera_metrics\]' > run_metrics.txt
dmesg | tail -n 300 > run_dmesg_tail.txt
```

`log_offset` 应在本轮进入 YOLO 页面前记录。若进程非正常终止、没有 `final=1`，汇总脚本会使用文件中最后一条周期累计记录，并应在报告中注明结束方式。

应用默认丢弃前 30 个成功帧，再按单调时钟统计：

- `fps`：预热后成功发布帧数/首末发布时间差。
- `infer_p95_ms`：`rknn_run` 加 YOLO 后处理。
- `queue_p95_ms`：VPSS Get 成功到 inference pop。
- `e2e_p95_ms`：媒体 PTS（可确认与 monotonic 同时基时）或软件采集起点到 RGB565 front buffer 发布，不包含 LVGL timer、fbdev/SPI scanout 和面板光学响应。
- `queue_drop`：latest-wins 主动丢弃的旧帧，不等同于驱动丢帧；必须与队列容量一起报告。

RKMPI 结果应检查 `media_pts == frames`。如果出现 `software_start > 0`，说明部分帧的 PTS 无法与单调时钟对应；这些数据只能作为应用内处理时延，不能直接填成严格的 capture→publish p95。需要物理端到端延迟时，应另做 LED/画面事件加高速相机测试。

把每轮日志拉回主机后汇总：

```bash
python3 docs/tools/summarize_ai_camera_metrics.py \
  opencv-run-{1,2,3}.txt rkmpi-run-{1,2,3}.txt
```

## 稳定性验收

- 冒烟：YOLO 页面连续 30 分钟，进入/退出 20 次。
- 回归：新 backend 连续 2 小时，确认进程存活、画面更新、RSS 无持续增长。
- 简历长稳：目标 8 小时或更长；只有日志覆盖的实际时长才能填写 `[X]`。
- 通过条件：`failures=0`、无未恢复 timeout、无 `release_failures`/media error、页面可正常退出；队列 drop 允许存在，但必须解释为有限队列的延迟控制策略。

建议日志目录：`docs/logs/vi-vpss/<YYYY-MM-DD>/`。大体积原始视频不提交，只记录外部位置与校验值。

## 简历表述

当前已有证据支持的非对比表述：

> 媒体与推理链路优化：将 OpenCV `VideoCapture`/CPU resize 预处理重构为 VI→VPSS→RKNN 链路，通过 RK MPI MB/DMABUF 将 VPSS 输出直接导入 RKNN；在 SC3336 864×480 输入、YOLOv5 640×640 模型下，实现约 9.1 FPS 的有效推理/发布帧率和 189 ms 端到端 p95，并通过容量为 2 的 latest-wins 有限队列、显式 Get/Release 所有权及连续超时重建控制帧积压和异常恢复。

只有未来重新建立有效旧链并完成同口径三轮 A/B 后，才能填写对比版本：

> 媒体与推理链路优化：将 OpenCV `VideoCapture`/CPU resize 预处理重构为 VI→VPSS→RKNN 链路，以 RK MPI MB 对应的 DMABUF 直接导入 RKNN；在 SC3336 864×480 输入、YOLOv5 640×640 模型下，将采集时间戳至可显示帧发布的 p95 延迟由 **[A] ms** 降至 **[B] ms**、有效帧率由 **[C]** 提升至 **[D] FPS**，并通过容量为 **[Q]** 的 latest-wins 队列、显式 Get/Release 所有权及连续超时重建实现 **[X] 小时**稳定运行。

如果数据只证明预处理时延下降、而 FPS 被 NPU 或 SPI 显示限制，则应据实删除“帧率提升”，改写为 CPU/预处理时延的实测收益。
