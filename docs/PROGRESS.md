# Echo-Mate 二次开发进度

## 当前状态

- [x] 原开源 Echo-Mate / DeskBot 项目已在 RV1106 板子上完整复现全部功能。
- [x] 初始化二次开发路线、基线和进度文档。
- [x] 创建 Baseline 记录分支并完成主机侧 Git 状态记录。
- [x] 初步归档 Baseline 板端系统、显示、摄像头 media graph 和 DeskBot 进程采样日志。
- [ ] TODO：补齐 Baseline 的应用内 FPS、分段耗时、端到端延迟、完整 dmesg 和人工功能检查。
- [ ] TODO：完成蓝色 LED DTS 使能/禁用与板端验证。
- [ ] TODO：完成 SC3336 原厂/参考/Echo-Mate DTS 对比和摄像头链路验证。
- [ ] TODO：实现 VI/VPSS 采图链路并完成前后性能对比。
- [ ] TODO：完成 ST7789V DRM/tinyDRM/mipi-dbi 调研、适配和 LVGL 接入。
- [ ] TODO：形成最终 Performance Report。

## 记录约定

每个后续任务复制下方模板。未执行的验证必须写“未执行”及原因；日志建议保存到 `docs/logs/<stage>/<YYYY-MM-DD>/`。大体积原始视频、固件和二进制文件不提交 Git，只记录存放位置、文件名和校验信息。

### 任务记录模板

#### YYYY-MM-DD：任务名称

- 状态：TODO / 进行中 / 已完成 / 部分完成 / 阻塞
- 目标：
- 分支：
- 基线版本/commit：
- 板型与硬件：
- 修改文件：
- 验证命令：
- 日志路径：
- 结果：
- 性能数据：
- 遗留问题：
- 下一步：

## 历史记录

### 2026-07-16：Baseline 板端日志检查与归档

- 状态：部分完成
- 目标：检查并归档 RV1106 板端 Baseline 日志，提取系统、显示、摄像头 media graph 和 DeskBot/YOLO 外部采样结论。
- 分支：`baseline/board-metrics`
- 基线版本/commit：`f972384a7`（主机侧 Baseline 记录提交）
- 板型与硬件：RV1106 Echo-Mate；SC3336 与 ST7789V 已由日志间接确认，具体板卡修订、屏幕模组参数和供电方式仍待补充。
- 修改文件：
  - `docs/BASELINE.md`
  - `docs/PROGRESS.md`
  - `docs/logs/baseline/2026-07-08/baseline_summary.md`
  - `docs/logs/baseline/2026-07-08/function_check.md`
  - `docs/logs/baseline/2026-07-08/board_devices.md`
  - `docs/logs/baseline/2026-07-08/board_camera_media.txt`
  - `docs/logs/baseline/2026-07-08/board_display.txt`
  - `docs/logs/baseline/2026-07-08/board_dmesg.txt`
  - `docs/logs/baseline/2026-07-08/board_key_dmesg.txt`
  - `docs/logs/baseline/2026-07-08/board_system_info.md`
  - `docs/logs/baseline/2026-07-08/deskbot_process.txt`
  - `docs/logs/baseline/2026-07-08/deskbot_yolo_top_60s.txt`
- 验证命令：
  - `git status --short`
  - `find docs/logs/baseline/2026-07-08 -maxdepth 2 -type f | sort`
  - `du -h docs/logs/baseline/2026-07-08/* | sort -h`
  - `file docs/logs/baseline/2026-07-08/*`
  - `wc -l docs/logs/baseline/2026-07-08/*`
  - `rg -n "sc3336|rkcif|rkisp|csi|dphy|mipi|video|/dev/video|fb_st7789v|/dev/fb0|drm|DRM|main|VmRSS|VmHWM" docs/logs/baseline/2026-07-08`
  - `git diff --check`
- 日志路径：`docs/logs/baseline/2026-07-08/`
- 结果：
  - 板端内核为 Linux 5.10.110，RootFS 从 SD 卡 `/dev/mmcblk1p7` 启动并以 ext4 rw 挂载。
  - 当前显示链路确认为 `/dev/fb0` + `fb_st7789v`，320×240，16 bpp；本次日志未枚举到 DRM 节点。
  - media graph 确认 `m00_b_sc3336 4-0030` 连接到 `rockchip-csi2-dphy0`、`rockchip-mipi-csi2` 和 rkcif。
  - rkcif 暴露 `/dev/video0` 到 `/dev/video10`，rkisp mainpath 暴露 `/dev/video11` 到 `/dev/video18` 和 `/dev/media1`。
- 性能数据：
  - DeskBot 进程样本：PID `596`，`VmRSS` 12540 kB，`VmHWM` 13060 kB。
  - 60 秒 `top` 采样中 `./main` CPU 平均约 46.6%，最小 38%，最大 50%；VSZ 稳定为 69208 kB。
- 遗留问题：
  - `function_check.md` 尚未记录人工功能检查结果。
  - `board_devices.md` 原始直接设备枚举为空。
  - `board_dmesg.txt` 以 `fb_st7789v` 刷屏日志为主，尚不能替代完整启动 dmesg。
  - BoardConfig、实际 DTB、固件镜像名、YOLO 有效 FPS、分段耗时、端到端延迟和长稳运行数据仍待补。
- 下一步：补采缺失的直接设备枚举和人工功能检查；随后进入 LED DTS 阶段。

### 2026-07-08：Baseline 主机侧记录初始化

- 状态：部分完成
- 目标：创建 Baseline 记录分支，并记录后续板端采样前的主机 Git/仓库状态。
- 分支：`baseline/board-metrics`
- 基线版本/commit：`198322396726c401c2cd2e1d69000ca395cc0974`
- 板型与硬件：RV1106 Echo-Mate；本次未连接板端采集，硬件修订、SC3336 模组和 ST7789V 屏幕参数仍待补充。
- 修改文件：
  - `docs/BASELINE.md`
  - `docs/PROGRESS.md`
  - `docs/logs/baseline/2026-07-08/host_git_state.md`
- 验证命令：
  - `git status --short`
  - `git branch --show-current`
  - `git rev-parse HEAD`
  - `git log --oneline --decorate --max-count=8`
  - `git remote -v`
- 日志路径：`docs/logs/baseline/2026-07-08/host_git_state.md`
- 结果：已从 `docs/embedded-roadmap` 创建 `baseline/board-metrics`，并确认主机侧记录前工作区干净。
- 性能数据：未采集；本次仅记录主机侧 Git 状态。
- 遗留问题：仍需在 RV1106 板端补采系统信息、设备枚举、media graph、YOLO 性能、AI Chat 状态和显示链路状态。
- 下一步：在板端执行 Baseline 采集命令，并将输出保存到 `docs/logs/baseline/2026-07-08/` 后回填 `docs/BASELINE.md`。

### 2026-06-26：二次开发文档初始化

- 状态：已完成
- 目标：建立长期路线、进度模板和原项目基线说明，不修改源码或底层系统。
- 分支：未创建；建议 `docs/embedded-roadmap`
- 基线版本/commit：待首次板端数据采集时补充。
- 板型与硬件：RV1106 Echo-Mate；具体板卡修订、SC3336 模组和 ST7789V 屏幕参数待补充。
- 修改文件：
  - `AGENTS.md`
  - `docs/ROADMAP.md`
  - `docs/PROGRESS.md`
  - `docs/BASELINE.md`
- 验证命令：
  - `git status --short`
  - 仓库路径与关键实现的 `rg`/`find` 只读核对
  - `git diff --check`
- 日志路径：本次仅初始化文档，未生成板端日志。
- 结果：已建立 Baseline、LED DTS、SC3336、VI/VPSS、ST7789V DRM 和 Performance Report 六阶段路线。
- 性能数据：未采集，见 `docs/BASELINE.md` 的 TODO 表。
- 遗留问题：需确认当前烧录固件对应的 BoardConfig、顶层 DTS/DTB、内核配置和准确硬件版本。
- 下一步：执行 Baseline 信息与性能数据采集，避免后续优化缺少统一对照。
