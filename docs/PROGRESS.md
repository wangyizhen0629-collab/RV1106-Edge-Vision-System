# Echo-Mate 二次开发进度

## 当前状态

- [x] 原开源 Echo-Mate / DeskBot 项目已在 RV1106 板子上完整复现全部功能。
- [x] 初始化二次开发路线、基线和进度文档。
- [x] 创建 Baseline 记录分支并完成主机侧 Git 状态记录。
- [x] 初步归档 Baseline 板端系统、显示、摄像头 media graph 和 DeskBot 进程采样日志。
- [x] 补充 Baseline 人工功能检查和 video/media 设备枚举分析。
- [x] 完成 Baseline 初始归档；尚未采集的应用内 FPS、延迟和设备枚举项保留在 `docs/BASELINE.md`，后续按需补采。
- [x] 完成蓝色 LED DTS 使能、SDK 构建与固件打包；禁用节点对照验证不再纳入本阶段，未执行的板端验证已在历史记录中注明。
- [ ] TODO：完成 SC3336 原厂/参考/Echo-Mate DTS 对比和摄像头链路验证。
- [ ] TODO：实现 VI/VPSS 采图链路并完成前后性能对比。
- [x] ST7789V DRM 阶段一完成：Home/YOLO 实机 FB 基线、运行时链路、驱动迁移参数和人工视觉结果均已固化；实测 SCLK/reset/DC 波形未采集，作为非阻塞证据限制保留。
- [x] ST7789V DRM 阶段二完成：mipi-dbi TinyDRM 驱动、fbtft 解绑和 16-bit SPI 分片修复已通过实机验证；DRM connector connected、mode 320×240，颜色/方向/offset/刷新正常，Home/YOLO 持续运行约 30 分钟。
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

### 2026-07-17～2026-07-18：LED DTS 蓝灯使能接入

- 状态：已完成（蓝色 LED DTS 使能、SDK 构建与固件打包范围）
- 目标：根据原理图确认 `GPIO0_A4` 为蓝色 LED 后，将蓝灯接入 `gpio-leds`，并配置为 timer trigger 默认 1 秒周期闪烁。
- 分支：`baseline/board-metrics`（建议后续切到 `feat/led-dts` 或在当前分支提交前确认分支策略）
- 基线版本/commit：`aca4be603`（Baseline 文档冻结提交）
- 板型与硬件：RV1106 Echo-Mate；红色电源 LED 已由 `work` 节点控制，蓝色 LED 确认为 `GPIO0_A4`，当前实物默认已亮。
- 修改文件：
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
  - `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig`
  - `docs/PROGRESS.md`
  - `docs/ROADMAP.md`
- 验证命令：
  - `git status --short`
  - `rg -n "LEDS_TRIGGER_TIMER|LEDS_GPIO|LEDS_TRIGGER_ACTIVITY" SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/configs/echo_rv1106_linux_defconfig SDK/rv1106-sdk/sysdrv/source/kernel/.config`
  - `dtc -I dtb -O dts SDK/rv1106-sdk/output/out/sysdrv_out/board_uclibc_rv1106/rv1106g-echo-mate.dtb`，反编译结果中已确认蓝灯节点及 timer 配置。
  - `rg -n "rv1106g-echo-mate|echo_rv1106_linux_defconfig|BoardConfig|kernel" SDK/rv1106-sdk/IMAGE/SD_CARD_RV1106G-ECHO-MATE.DTS_20260717.1510_RELEASE_TEST/build_info.txt`
  - `sha256sum SDK/rv1106-sdk/output/image/update.img SDK/rv1106-sdk/IMAGE/SD_CARD_RV1106G-ECHO-MATE.DTS_20260717.1510_RELEASE_TEST/IMAGES/update.img`
  - `git diff --check`
- 日志路径：构建信息见 `SDK/rv1106-sdk/IMAGE/SD_CARD_RV1106G-ECHO-MATE.DTS_20260717.1510_RELEASE_TEST/build_info.txt`；未生成独立的 LED 板端日志。
- 结果：
  - 在 `gpio-leds` 下新增 `blue_led: blue`，label 为 `echo-mate:blue:status`。
  - 蓝灯 GPIO 配置为 `<&gpio0 RK_PA4 GPIO_ACTIVE_HIGH>`，默认 trigger 为 `timer`，`led-pattern = <500 500>`。
  - `echo_rv1106_linux_defconfig` 已开启 `CONFIG_LEDS_TRIGGER_TIMER=y`，用于支持 timer trigger 和 `delay_on`/`delay_off` sysfs 属性。
  - 原 `gpio0pa4` fixed regulator 节点保留但标记为 `status = "disabled"`，避免同一 GPIO 同时被表达为常开 regulator 与 LED。
  - SDK 已使用 `rv1106g-echo-mate.dts` 和 `echo_rv1106_linux_defconfig` 完成构建；生成 DTB 的时间晚于 DTSI 修改时间，反编译 DTB 已确认包含蓝灯节点。
  - 已生成并归档 `update.img`；输出镜像与 `IMAGE/SD_CARD_RV1106G-ECHO-MATE.DTS_20260717.1510_RELEASE_TEST/IMAGES/update.img` 的 SHA-256 均为 `a1acc02ca8abe6480f1a7f55bec9b9bf93f55ce1925bca0829b91008ab4fe1cc`。
  - 2026-07-18 按当前任务范围收口：完成使能配置，不再单独制作禁用节点固件做反向对照。
- 性能数据：不适用。
- 遗留问题：
  - 非阻塞待验证：后续若随固件更新烧录，可补充 `/sys/class/leds/echo-mate:blue:status`、trigger、亮灭和极性观察；当前不能写作已完成板端验证。
  - 禁用节点对照验证已取消，不再作为本任务完成条件。
- 下一步：蓝色 LED DTS 使能任务收口，进入 SC3336 DTS / Camera Pipeline 阶段；若后续有烧录后的板端验证结果，再追加日志记录。

### 2026-07-16：Baseline 板端日志检查与归档

- 状态：已完成（Baseline 初始归档范围）
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
  - `board_devices.md` 已补充 video/media/V4L2 subdev 节点映射，推荐 YOLO baseline 输入节点为 `/dev/video11`，当前格式 `864x480 NV21`。
  - 人工功能检查显示 GUI 首页、触摸/按键、摄像头画面、AI Chat、显示颜色/方向均正常。
  - YOLO 页面整体可用，但进入时会有一瞬间花屏；检测框有时会过大，显示到屏幕外。
- 性能数据：
  - DeskBot 进程样本：PID `596`，`VmRSS` 12540 kB，`VmHWM` 13060 kB。
  - 60 秒 `top` 采样中 `./main` CPU 平均约 46.6%，最小 38%，最大 50%；VSZ 稳定为 69208 kB。
- 遗留问题：
  - framebuffer、DRM 和 input 的直接枚举仍需补采。
  - `board_dmesg.txt` 以 `fb_st7789v` 刷屏日志为主，尚不能替代完整启动 dmesg。
  - BoardConfig、实际 DTB、固件镜像名、YOLO 有效 FPS、分段耗时、端到端延迟和长稳运行数据仍待补。
- 下一步：Baseline 初始归档已收口；尚缺的 framebuffer/DRM/input、完整启动 dmesg、应用内 FPS 和延迟数据继续保留在 `docs/BASELINE.md`，在相关优化阶段按统一方法补采。

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
