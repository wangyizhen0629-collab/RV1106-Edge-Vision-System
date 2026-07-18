# Echo-Mate 二次开发路线图

## 目标与边界

本路线以已复现的开源 Echo-Mate / DeskBot 为基线，通过小步、可回退、可测量的改动学习并优化 RV1106 的设备树、摄像头媒体链路、VI/VPSS 和 DRM 显示链路。

路线图描述长期可能涉及的目录，但不代表默认授权修改。只有具体任务明确要求时，才修改 `SDK/rv1106-sdk/` 下的 kernel、uboot、buildroot、设备树或板级配置。每阶段开始前先运行 `git status --short`，并在 `docs/PROGRESS.md` 建立任务记录。

## 阶段 0：Baseline

### 目标

- 固化原项目已复现状态、软硬件环境和关键数据流。
- 建立后续优化可复用的测量方法与日志目录。
- 在不改变现有行为的前提下补采性能基线。

### 涉及目录

- `Demo/DeskBot_demo/`
- `Demo/yolov5_demo/`
- `Demo/AIChat_demo/`
- `Demo/rkmpi_demos/`
- `SDK/rv1106-sdk/`（只读核对）
- `docs/`

### 预期产出

- 更新后的 `docs/BASELINE.md`。
- `docs/logs/baseline/<YYYY-MM-DD>/` 下的板端命令输出和采样结果。
- 测试条件说明：板型、固件、分辨率、模型、输入场景、运行时长、采样工具。

### 建议验证命令

```bash
uname -a
cat /proc/cmdline
cat /proc/meminfo
cat /proc/cpuinfo
ps
top -b -n 1
dmesg
ls -l /dev/video* /dev/media* /dev/fb* /dev/dri/* 2>/dev/null
```

根据板端 BusyBox/工具可用性补充 `pidstat`、`time`、`media-ctl`、`v4l2-ctl`；不可用时记录替代方法。

### 完成标准

- 原项目 GUI、YOLO、AI Chat、显示和摄像头功能状态有明确记录。
- FPS、CPU、内存、启动时间、端到端延迟均有实测值，或有注明原因和采集方法的 TODO。
- 测量步骤可由另一人重复执行。

## 阶段 1：LED DTS

### 目标

- 找到 Echo-Mate 蓝色 LED 对应 GPIO、极性、pinctrl 和 `gpio-leds` 节点。
- 完成设备树使能配置，并理解亮灭/trigger 控制方式。
- 理解设备树节点到 LED class sysfs 的映射关系。

当前阶段范围已收口为蓝色 LED DTS 使能代码接入；不再单独制作禁用节点固件做反向对照。板端使能验证若随后续固件更新执行，再追加到进度记录，不虚写为已验证。

### 涉及目录

- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/`
- `SDK/rv1106-sdk/project/cfg/`（板级配置只读核对，必要时按明确任务修改）
- `docs/`

已确认候选板级文件包括：

- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/rv1106-echo-mate-ipc.dtsi`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/` 对应 RV1106 顶层 DTS（具体生效文件须由 BoardConfig/构建日志确认）

### 预期产出

- 最小 DTS/DTSI 改动。
- LED 节点、GPIO、有效电平和 pinctrl 的说明。
- 使能配置记录；若执行板端烧录，则补充 sysfs 日志和实物观察结果。

### 建议验证命令

```bash
cd SDK/rv1106-sdk
./build.sh lunch
./build.sh

dmesg | grep -i -E 'led|gpio|pinctrl'
find /sys/class/leds -maxdepth 2 -type f -o -type l
cat /sys/class/leds/<led-name>/trigger
echo none > /sys/class/leds/<led-name>/trigger
echo 1 > /sys/class/leds/<led-name>/brightness
echo 0 > /sys/class/leds/<led-name>/brightness
```

`<led-name>` 必须以板端实际枚举名称替换。

### 完成标准

- DTS/defconfig 使能改动明确，GPIO、有效电平、trigger 和资源占用关系均有记录。
- 若执行板端构建和烧录，LED 节点启用后的 sysfs 枚举、亮灭和 trigger 控制符合预期，且无新增关键内核错误。
- 专门的禁用节点对照验证不再纳入本阶段完成标准；未执行的板端步骤必须在进度记录中注明。

## 阶段 2：SC3336 DTS / Camera Pipeline

### 目标

- 对比原厂 SDK、Luckfox 参考配置与 Echo-Mate 配置，解释当前板子的 SC3336 适配差异。
- 梳理 `I2C → sensor → MIPI D-PHY/CSI2 → rkcif → rkisp → video/VI` 的设备树和 media graph。
- 明确 `compatible`、I2C 地址、`xvclk`、reset/pwdn GPIO、pinctrl、endpoint、`data-lanes` 和 remote-endpoint 的作用。

### 涉及目录

- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/`
- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/media/`
- `SDK/rv1106-sdk/media/isp/`
- `Demo/rkmpi_demos/`
- `docs/`

已确认可作为对照的文件包括 `rv1106-ipc.dtsi`、`rv1106-evb-cam.dtsi`、`rv1106-luckfox-pico-pro-max-ipc.dtsi` 和 `rv1106-echo-mate-ipc.dtsi`。

### 预期产出

- SC3336 DTS 对照表和媒体链路图。
- 当前板级每个关键属性的来源与理由。
- sensor probe、media graph、格式和取帧日志。

### 建议验证命令

```bash
dmesg | grep -i -E 'sc3336|rkisp|rkcif|csi|dphy|mipi'
i2cdetect -y <i2c-bus>
media-ctl -p -d /dev/media0
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --all
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=300 --stream-to=/tmp/sc3336.raw
```

设备号、总线号、像素格式和分辨率以板端 media graph 为准，不预先硬编码。

### 完成标准

- 能从 DTS 节点逐段解释到板端 video/VI 节点。
- SC3336 稳定 probe，media link 完整，能够持续取帧。
- 原厂、Luckfox 与 Echo-Mate 配置差异有证据和结论，未知项明确标注。

## 阶段 3：VI/VPSS Optimization

### 目标

- 以 RV1106 RKMPI VI/VPSS 替换当前 OpenCV-mobile / V4L2 / `cv::VideoCapture` 采图和缩放路径。
- 尽可能减少不必要的 CPU 拷贝、颜色转换和缩放。
- 保持 YOLO 输出到 LVGL 的现有接口行为，并提供旧链路回退开关。

### 涉及目录

- `Demo/yolov5_demo/cpp/`
- `Demo/rkmpi_demos/`
- `Demo/DeskBot_demo/`
- `SDK/rv1106-sdk/media/`（依赖和接口核对）
- `docs/`

已确认参考实现包括：

- `Demo/rkmpi_demos/example/luckfox_pico_rtsp_yolov5/`
- `Demo/rkmpi_demos/example/luckfox_pico_rtsp_opencv_capture/`

### 预期产出

- VI/VPSS 采图适配层及清晰的初始化/销毁生命周期。
- 旧链路与新链路的编译期或运行期选择方案。
- FPS、CPU、RSS/峰值内存、丢帧、超时和端到端延迟对比。
- 资源所有权、缓存一致性、线程同步和失败回收说明。

### 建议验证命令

```bash
cd Demo/DeskBot_demo/build
cmake .. -DTARGET_ARM=ON
make

top -b -n 1
cat /proc/<pid>/status
cat /proc/<pid>/stat
dmesg | tail -n 200
```

应用内应使用单调时钟记录采集、预处理、推理、后处理和显示各段时间；所有对比使用相同模型、分辨率、场景和运行时长。

### 完成标准

- DeskBot YOLO 页面可稳定使用 VI/VPSS 获取帧，退出和重复进入无资源泄漏或死锁。
- 旧链路可回退，检测结果和画面方向/颜色正确。
- 至少完成三轮可重复对比，并给出数据支持的收益或无收益结论。

## 阶段 4：ST7789V DRM

### 目标

- 明确当前 ST7789V 的 SPI、DC、reset、背光、方向、像素格式和频率配置。
- 调研当前内核中 `sitronix,st7789v`、tinyDRM、DRM mipi-dbi 的可用实现与版本约束。
- 打通 `ST7789V → DRM/KMS → /dev/dri/card* → LVGL linux_drm`，必要时保留 fbdev 兜底。

### 涉及目录

- `SDK/rv1106-sdk/sysdrv/source/kernel/drivers/gpu/drm/`
- `SDK/rv1106-sdk/sysdrv/source/kernel/arch/arm/boot/dts/`
- `SDK/rv1106-sdk/sysdrv/source/kernel/` 的 Kconfig/defconfig
- `Demo/DeskBot_demo/main.c`
- `Demo/DeskBot_demo/conf/`
- `Demo/DeskBot_demo/lvgl/src/drivers/display/drm/`
- `docs/`

已确认 `rv1106-echo-mate-ipc.dtsi` 中存在 `compatible = "sitronix,st7789v"`，最终驱动绑定和生效配置仍须通过内核源码、config 与板端日志验证。

### 预期产出

- 驱动方案选择记录：复用、回移植或最小适配。
- DTS、Kconfig/defconfig、驱动和 LVGL 接入的分层改动。
- DRM 枚举、测试图、LVGL 画面和稳定性日志。

### 建议验证命令

```bash
dmesg | grep -i -E 'drm|st7789|mipi|dbi|spi'
zcat /proc/config.gz | grep -E 'DRM|TINYDRM|MIPI_DBI|ST7789'
ls -l /dev/dri /sys/class/drm
modetest -c -p -e
```

若镜像未提供 `modetest` 或 `/proc/config.gz`，记录内核 `.config`、sysfs 和替代测试程序的结果。

### 完成标准

- 驱动稳定 probe，DRM connector/mode 状态符合预期。
- 测试图与 LVGL 均可显示，颜色、方向、偏移和刷新区域正确。
- 连续运行无明显 SPI/DRM 错误、花屏或资源泄漏，并有 fbdev/旧方案回退说明。

## 阶段 5：Performance Report

### 目标

- 汇总各阶段的可复现证据，形成面试可讲解的技术报告。
- 区分事实、测量结果、设计权衡和待验证假设。

### 涉及目录

- `docs/`
- 各阶段 `docs/logs/` 数据
- 相关源码/DTS 路径（只读引用）

### 预期产出

- 前后链路图、关键改动摘要和问题定位过程。
- 性能对比表：平均 FPS、帧时间分布、CPU、RSS/峰值内存、启动时间、端到端延迟、稳定性。
- 测量方法、原始日志索引、局限性与后续工作。

### 建议验证命令

```bash
git status --short
git diff --check
find docs/logs -type f -maxdepth 4 | sort
```

性能数据使用阶段 0 和阶段 3 定义的同一套板端采集命令与应用内时间戳。

### 完成标准

- 所有表格可追溯到原始日志和对应 Git 版本。
- 结论能够说明“为什么改、如何验证、数据如何、有什么代价、如何回退”。
- 文档中没有把未执行测试写成已完成。
