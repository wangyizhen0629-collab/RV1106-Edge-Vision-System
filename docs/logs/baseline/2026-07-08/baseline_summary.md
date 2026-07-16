# Baseline board summary

日志目录：`docs/logs/baseline/2026-07-08/`

整理日期：2026-07-16

## 已确认事实

- 系统内核：Linux 5.10.110，编译版本 `#45 Wed Apr 9 17:33:31 CST 2025`，架构 `armv7l`。
- 启动介质：SD 卡，启动参数中 `storagemedia=sd`，RootFS 指向 `/dev/mmcblk1p7`。
- RootFS：ext4，可读写挂载；`df -h /` 显示约 5.8G，总使用率约 5%。
- 显示节点：存在 `/dev/fb0`，`fb0` 名称为 `fb_st7789v`，分辨率 `320,240`，位深 `16`。
- DRM：`board_display.txt` 未枚举到 `/dev/dri/*` 输出，当前 Baseline 显示链路按 fbdev / fbtft 记录。
- 摄像头 media graph：
  - `/dev/media0`：`rkcif-mipi-lvds`
  - sensor：`m00_b_sc3336 4-0030`
  - sensor 格式：`SBGGR10_1X10/2304x1296@10000/250000`
  - 链路：`SC3336 -> rockchip-csi2-dphy0 -> rockchip-mipi-csi2 -> rkcif`
  - rkcif video 节点：`/dev/video0` 到 `/dev/video10`
  - rkisp mainpath：`/dev/video11` 到 `/dev/video18`，`/dev/media1`
  - rkisp statistics：`/dev/video19`、`/dev/video20`
- DeskBot 进程样本：
  - 进程名：`main`
  - PID：`596`
  - `VmRSS`：12540 kB
  - `VmHWM`：13060 kB
  - `Threads`：1
- YOLO/DeskBot 运行时 `top` 采样：
  - 样本数：60
  - `./main` CPU：平均约 46.6%，最小 38%，最大 50%
  - `./main` VSZ：69208 kB，采样期间保持稳定

## 已归档原始日志

- `host_git_state.md`：主机侧 Git 状态。
- `board_system_info.md`：板端内核、启动参数、RootFS 和分区信息。
- `board_display.txt`：fbdev/ST7789V 显示节点信息。
- `board_camera_media.txt`：`media-ctl`、`v4l2-ctl` 输出。
- `board_dmesg.txt`：本次保存的 dmesg 片段。
- `board_key_dmesg.txt`：本次保存的关键 dmesg 片段。
- `deskbot_process.txt`：`/proc/<pid>/status`、`stat`、`cmdline` 输出。
- `deskbot_yolo_top_60s.txt`：YOLO 页面运行期间 60 秒 `top` 采样。

## 当前缺口

- `function_check.md` 未记录人工功能检查结果，GUI、触摸、YOLO 画面、AI Chat、显示颜色/方向仍需补充。
- `board_devices.md` 原始直接设备枚举为空；当前只能从 display/media 日志间接确认部分节点。
- `board_dmesg.txt` 内容以 `fb_st7789v` 刷屏日志为主，未覆盖完整启动 dmesg；后续 LED、SC3336、DRM 阶段仍建议重新保存完整启动日志。
- 尚未记录 BoardConfig、实际顶层 DTS/DTB、固件镜像名和烧录方式。
- 尚未采集应用内 YOLO 有效 FPS、分段耗时、端到端延迟和长稳运行数据。
