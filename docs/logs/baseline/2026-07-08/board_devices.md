# Board Devices Baseline

## 1. 采集目标

本节用于记录当前开发板 Linux 系统中已经枚举出的关键设备节点，包括：

* Video 节点：`/dev/video*`
* Media Controller 节点：`/dev/media*`
* V4L2 subdev 节点：`/dev/v4l-subdev*`
* Framebuffer 节点：`/dev/fb*`
* DRM 节点：`/dev/dri/*`
* Input 节点：`/dev/input/*`

当前已重点确认 camera / V4L2 / media pipeline。Framebuffer、DRM 和 input 节点仍需后续补充采集。

---

## 2. Video 节点枚举

### 2.1 执行命令

```bash
ls -l /dev/video*
```

### 2.2 枚举结果

当前系统共枚举出 `/dev/video0` 到 `/dev/video20`，说明 V4L2 视频子系统已经加载，并创建了多个视频采集、ISP 输出、ISP 统计和参数相关节点。

### 2.3 Video4Linux 节点名称

执行命令：

```bash
for v in /sys/class/video4linux/video*; do
    echo "---- $v ----"
    cat "$v/name" 2>/dev/null
done
```

解析结果如下：

| 设备节点           | 节点名称                           | 初步判断                |
| -------------- | ------------------------------ | ------------------- |
| `/dev/video0`  | `stream_cif_mipi_id0`          | rkcif MIPI 输入流 0    |
| `/dev/video1`  | `stream_cif_mipi_id1`          | rkcif MIPI 输入流 1    |
| `/dev/video2`  | `stream_cif_mipi_id2`          | rkcif MIPI 输入流 2    |
| `/dev/video3`  | `stream_cif_mipi_id3`          | rkcif MIPI 输入流 3    |
| `/dev/video4`  | `rkcif_scale_ch0`              | rkcif 缩放通道 0        |
| `/dev/video5`  | `rkcif_scale_ch1`              | rkcif 缩放通道 1        |
| `/dev/video6`  | `rkcif_scale_ch2`              | rkcif 缩放通道 2        |
| `/dev/video7`  | `rkcif_scale_ch3`              | rkcif 缩放通道 3        |
| `/dev/video8`  | `rkcif_tools_id0`              | rkcif 工具节点 0        |
| `/dev/video9`  | `rkcif_tools_id1`              | rkcif 工具节点 1        |
| `/dev/video10` | `rkcif_tools_id2`              | rkcif 工具节点 2        |
| `/dev/video11` | `rkisp_mainpath`               | ISP 主输出路径           |
| `/dev/video12` | `rkisp_selfpath`               | ISP selfpath 输出路径   |
| `/dev/video13` | `rkisp_bypasspath`             | ISP bypass 输出路径     |
| `/dev/video14` | `rkisp_mainpath_4x4sampling`   | ISP 主路径 4x4 采样输出    |
| `/dev/video15` | `rkisp_bypasspath_4x4sampling` | ISP bypass 4x4 采样输出 |
| `/dev/video16` | `rkisp_lumapath`               | ISP 亮度路径            |
| `/dev/video17` | `rkisp_rawrd0_m`               | ISP raw read 节点     |
| `/dev/video18` | `rkisp_rawrd2_s`               | ISP raw read 节点     |
| `/dev/video19` | `rkisp-statistics`             | ISP 统计信息节点          |
| `/dev/video20` | `rkisp-input-params`           | ISP 参数输入节点          |

当前 video 节点可以分为三组：

| 分组        | 节点范围                            | 作用                        |
| --------- | ------------------------------- | ------------------------- |
| rkcif 采集侧 | `/dev/video0` - `/dev/video10`  | MIPI CSI / CIF 采集、缩放和工具节点 |
| rkisp 输出侧 | `/dev/video11` - `/dev/video18` | ISP 处理后的图像输出路径            |
| rkisp 控制侧 | `/dev/video19` - `/dev/video20` | ISP 统计和参数输入节点             |

---

## 3. v4l2-ctl 设备列表

### 3.1 执行命令

```bash
v4l2-ctl --list-devices
```

### 3.2 解析结果

| 设备组                | 节点                                             | 说明                                  |
| ------------------ | ---------------------------------------------- | ----------------------------------- |
| `rkcif-mipi-lvds`  | `/dev/media0`                                  | rkcif / MIPI CSI 侧 media controller |
| `rkcif`            | `/dev/video0` - `/dev/video10`                 | rkcif 采集、缩放与工具节点                    |
| `rkisp_mainpath`   | `/dev/video11` - `/dev/video18`, `/dev/media1` | ISP 输出相关节点                          |
| `rkisp-statistics` | `/dev/video19`, `/dev/video20`                 | ISP 统计和参数节点                         |

当前系统存在两个主要 media controller：

```text
/dev/media0: rkcif / MIPI CSI 侧
/dev/media1: rkisp / ISP 侧
```

这说明摄像头链路不是单一 `/dev/video0` 节点，而是 Rockchip 平台典型的：

```text
sensor → MIPI CSI-2 → rkcif → rkisp → video output
```

---

## 4. /dev/video0 节点分析

### 4.1 执行命令

```bash
v4l2-ctl -d /dev/video0 --all
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### 4.2 关键结果

| 项目          | 内容                                                          |
| ----------- | ----------------------------------------------------------- |
| 节点          | `/dev/video0`                                               |
| Driver      | `rkcif`                                                     |
| Card type   | `rkcif`                                                     |
| Bus info    | `platform:rkcif-mipi-lvds`                                  |
| 能力          | Video Capture Multiplanar / Streaming / Extended Pix Format |
| Entity name | `stream_cif_mipi_id0`                                       |
| 上游连接        | `rockchip-mipi-csi2`                                        |
| 链路状态        | Enabled                                                     |
| 当前分辨率       | `2304x1296`                                                 |
| 当前像素格式      | `BG10`                                                      |
| 当前数据类型      | 10-bit Bayer raw                                            |

`/dev/video0` 当前位于 MIPI CSI-2 到 rkcif 的底层采集链路上。它当前默认输出格式为：

```text
2304x1296 BG10
```

也就是 10-bit Bayer raw 数据。该节点更接近 sensor 原始输出，不一定适合直接作为 YOLO 的应用层输入。

### 4.3 /dev/video0 支持格式

`/dev/video0` 支持多种格式，包括：

* NV16
* NV61
* NV12
* NV21
* YUYV
* YVYU
* UYVY
* VYUY
* RGB3
* BGR3
* RGB565
* 多种 Bayer raw 格式
* GREY / Y10 / Y12 / Y16
* Embedded data / shield pixel data

支持尺寸范围：

```text
64x64 - 2304x1296，步进 8x8
```

虽然 `/dev/video0` 支持 YUV/RGB 等格式，但当前默认格式是 `BG10` raw Bayer。后续如需使用该节点，需要实际测试其是否能稳定输出 NV12、NV21、RGB3 或 BGR3。

---

## 5. /dev/video11 节点分析

### 5.1 执行命令

```bash
v4l2-ctl -d /dev/video11 --all
v4l2-ctl -d /dev/video11 --list-formats-ext
```

### 5.2 关键结果

| 项目             | 内容                                                          |
| -------------- | ----------------------------------------------------------- |
| 节点             | `/dev/video11`                                              |
| Driver         | `rkisp_v7`                                                  |
| Card type      | `rkisp_mainpath`                                            |
| Bus info       | `platform:rkisp-vir0`                                       |
| Driver version | `2.0.0`                                                     |
| Media driver   | `rkisp-vir0`                                                |
| Media model    | `rkisp0`                                                    |
| Media version  | `5.10.110`                                                  |
| Entity name    | `rkisp_mainpath`                                            |
| 上游实体           | `rkisp-isp-subdev`                                          |
| 链路状态           | Enabled                                                     |
| 能力             | Video Capture Multiplanar / Streaming / Extended Pix Format |
| 当前分辨率          | `864x480`                                                   |
| 当前像素格式         | `NV21`                                                      |
| Quantization   | Full Range                                                  |
| Bytes per line | `864`                                                       |
| Size image     | `622080`                                                    |
| Crop bounds    | `2304x1296`                                                 |

`/dev/video11` 是 Rockchip ISP 的 mainpath 主输出节点。它当前已经输出：

```text
864x480 NV21
```

相比 `/dev/video0` 的 raw Bayer 输出，`/dev/video11` 的 NV21 格式更适合作为 YOLO 前处理输入。

### 5.3 /dev/video11 支持格式

`/dev/video11` 支持以下格式：

| 格式   | 含义                | 尺寸范围              |
| ---- | ----------------- | ----------------- |
| UYVY | YUV 4:2:2 packed  | 32x16 - 2304x1296 |
| NV16 | Y/CbCr 4:2:2      | 32x16 - 2304x1296 |
| NV61 | Y/CrCb 4:2:2      | 32x16 - 2304x1296 |
| NV21 | Y/CrCb 4:2:0      | 32x16 - 2304x1296 |
| NV12 | Y/CbCr 4:2:0      | 32x16 - 2304x1296 |
| NM21 | Y/CrCb 4:2:0, N-C | 32x16 - 2304x1296 |
| NM12 | Y/CbCr 4:2:0, N-C | 32x16 - 2304x1296 |

支持尺寸范围：

```text
32x16 - 2304x1296，宽高步进为 8x8
```

当前默认格式为：

```text
864x480 NV21
```

这对 YOLO baseline 来说是一个比较合理的默认输入格式，因为 NV21/NV12 都是嵌入式视频 pipeline 中常见的 YUV420 格式，便于后续进行硬件加速或轻量前处理。

---

## 6. /dev/media0：rkcif / MIPI CSI 侧拓扑

### 6.1 执行命令

```bash
media-ctl -p -d /dev/media0
```

### 6.2 Media device 信息

| 项目                | 内容                |
| ----------------- | ----------------- |
| Driver            | `rkcif`           |
| Model             | `rkcif-mipi-lvds` |
| Media API version | `5.10.110`        |
| Driver version    | `5.10.110`        |

### 6.3 关键实体

| Entity                | 节点                 | 作用                    |
| --------------------- | ------------------ | --------------------- |
| `stream_cif_mipi_id0` | `/dev/video0`      | rkcif MIPI stream 0   |
| `stream_cif_mipi_id1` | `/dev/video1`      | rkcif MIPI stream 1   |
| `stream_cif_mipi_id2` | `/dev/video2`      | rkcif MIPI stream 2   |
| `stream_cif_mipi_id3` | `/dev/video3`      | rkcif MIPI stream 3   |
| `rkcif_scale_ch0`     | `/dev/video4`      | rkcif scale channel 0 |
| `rkcif_scale_ch1`     | `/dev/video5`      | rkcif scale channel 1 |
| `rkcif_scale_ch2`     | `/dev/video6`      | rkcif scale channel 2 |
| `rkcif_scale_ch3`     | `/dev/video7`      | rkcif scale channel 3 |
| `rkcif_tools_id0`     | `/dev/video8`      | rkcif tools node 0    |
| `rkcif_tools_id1`     | `/dev/video9`      | rkcif tools node 1    |
| `rkcif_tools_id2`     | `/dev/video10`     | rkcif tools node 2    |
| `rockchip-mipi-csi2`  | `/dev/v4l-subdev0` | MIPI CSI-2 控制器        |
| `rockchip-csi2-dphy0` | `/dev/v4l-subdev1` | CSI-2 DPHY            |
| `m00_b_sc3336 4-0030` | `/dev/v4l-subdev2` | SC3336 摄像头 sensor     |

最关键的 sensor 实体为：

```text
m00_b_sc3336 4-0030
```

对应 V4L2 subdev 节点为：

```text
/dev/v4l-subdev2
```

这说明当前系统已经成功识别 SC3336 摄像头。

### 6.4 Sensor 输出格式

`media-ctl` 显示 SC3336 sensor 输出格式为：

```text
SBGGR10_1X10 / 2304x1296
```

这表示 sensor 输出的是 10-bit Bayer raw 数据，分辨率为 `2304x1296`。

### 6.5 media0 摄像头底层链路

`/dev/media0` 确认的底层链路如下：

```text
m00_b_sc3336 4-0030
    /dev/v4l-subdev2
    SBGGR10_1X10 / 2304x1296
        ↓
rockchip-csi2-dphy0
    /dev/v4l-subdev1
        ↓
rockchip-mipi-csi2
    /dev/v4l-subdev0
        ↓
rkcif
    /dev/video0 - /dev/video10
```

该链路中的关键连接均为 `ENABLED`，说明 sensor 到 MIPI CSI-2，再到 rkcif 的底层采集链路已经启用。

---

## 7. /dev/media1：rkisp / ISP 侧拓扑

### 7.1 执行命令

```bash
media-ctl -p -d /dev/media1
```

### 7.2 Media device 信息

| 项目                | 内容           |
| ----------------- | ------------ |
| Driver            | `rkisp-vir0` |
| Model             | `rkisp0`     |
| Media API version | `5.10.110`   |
| Driver version    | `5.10.110`   |

### 7.3 关键实体

| Entity                         | 节点                 | 作用                   |
| ------------------------------ | ------------------ | -------------------- |
| `rkisp-isp-subdev`             | `/dev/v4l-subdev3` | ISP 子设备              |
| `rkisp_mainpath`               | `/dev/video11`     | ISP 主输出路径            |
| `rkisp_selfpath`               | `/dev/video12`     | ISP selfpath 输出路径    |
| `rkisp_bypasspath`             | `/dev/video13`     | ISP bypass 输出路径      |
| `rkisp_mainpath_4x4sampling`   | `/dev/video14`     | ISP 主路径 4x4 采样输出     |
| `rkisp_bypasspath_4x4sampling` | `/dev/video15`     | ISP bypass 4x4 采样输出  |
| `rkisp_lumapath`               | `/dev/video16`     | ISP 亮度路径             |
| `rkisp_rawrd0_m`               | `/dev/video17`     | ISP raw read 节点      |
| `rkisp_rawrd2_s`               | `/dev/video18`     | ISP raw read 节点      |
| `rkisp-statistics`             | `/dev/video19`     | ISP 统计信息节点           |
| `rkisp-input-params`           | `/dev/video20`     | ISP 参数输入节点           |
| `rkcif-mipi-lvds`              | `/dev/v4l-subdev4` | rkcif 到 rkisp 的桥接子设备 |

### 7.4 ISP 输入链路

`/dev/media1` 显示 ISP 的输入来自：

```text
rkcif-mipi-lvds → rkisp-isp-subdev
```

链路状态为：

```text
ENABLED
```

ISP 输入格式为：

```text
SBGGR10_1X10 / 2304x1296
```

这说明来自 rkcif 的 raw Bayer 数据已经进入 rkisp。

### 7.5 ISP 输出链路

`rkisp-isp-subdev` 的输出连接到多个节点：

```text
rkisp_mainpath
rkisp_selfpath
rkisp_bypasspath
rkisp_mainpath_4x4sampling
rkisp_bypasspath_4x4sampling
rkisp_lumapath
rkisp-statistics
```

其中最重要的是：

```text
rkisp_mainpath → /dev/video11
```

`/dev/video11` 是当前应用层和 YOLO pipeline 最值得优先测试的摄像头输入节点。

---

## 8. V4L2 Subdev 节点映射

根据 media topology，当前关键 subdev 节点如下：

| Subdev 节点          | Entity                | 作用                   |
| ------------------ | --------------------- | -------------------- |
| `/dev/v4l-subdev0` | `rockchip-mipi-csi2`  | MIPI CSI-2 控制器       |
| `/dev/v4l-subdev1` | `rockchip-csi2-dphy0` | CSI-2 DPHY           |
| `/dev/v4l-subdev2` | `m00_b_sc3336 4-0030` | SC3336 摄像头 sensor    |
| `/dev/v4l-subdev3` | `rkisp-isp-subdev`    | ISP 子设备              |
| `/dev/v4l-subdev4` | `rkcif-mipi-lvds`     | rkcif 到 rkisp 的桥接子设备 |

当前最重要的 sensor 节点是：

```text
/dev/v4l-subdev2
```

对应：

```text
m00_b_sc3336 4-0030
```

这可以作为后续 SC3336 设备树、驱动 probe、sensor format 和 MIPI lane 配置分析的基准信息。

---

## 9. 完整摄像头链路总结

根据 `/dev/video*`、`/dev/media0` 和 `/dev/media1`，当前开发板摄像头链路可以总结为：

```text
SC3336 sensor
    entity: m00_b_sc3336 4-0030
    node: /dev/v4l-subdev2
    format: SBGGR10_1X10 / 2304x1296
        ↓
rockchip-csi2-dphy0
    node: /dev/v4l-subdev1
        ↓
rockchip-mipi-csi2
    node: /dev/v4l-subdev0
        ↓
rkcif-mipi-lvds
    media: /dev/media0
    video nodes: /dev/video0 - /dev/video10
        ↓
rkisp-isp-subdev
    node: /dev/v4l-subdev3
    media: /dev/media1
        ↓
rkisp_mainpath
    node: /dev/video11
    current output: 864x480 NV21
```

该链路表明：

```text
SC3336 → CSI-2 DPHY → MIPI CSI-2 → rkcif → rkisp → /dev/video11
```

已经被系统成功识别并启用。

---

## 10. YOLO 输入节点判断

根据当前 board devices baseline，YOLO 摄像头输入节点优先级建议如下：

| 优先级 | 节点             | 判断                                                            |
| --- | -------------- | ------------------------------------------------------------- |
| 1   | `/dev/video11` | `rkisp_mainpath`，当前输出 `864x480 NV21`，最适合作为 YOLO baseline 输入节点 |
| 2   | `/dev/video12` | `rkisp_selfpath`，可作为备用 ISP 输出节点                               |
| 3   | `/dev/video0`  | rkcif 底层采集节点，当前默认 `2304x1296 BG10` raw Bayer，不建议作为 YOLO 首选    |
| 不建议 | `/dev/video19` | ISP statistics 节点，不是普通图像帧输入                                   |
| 不建议 | `/dev/video20` | ISP input params 节点，不是普通图像帧输入                                 |

当前推荐 YOLO baseline 使用：

```text
Input node: /dev/video11
Format: 864x480 NV21
```

后续性能测试应优先围绕 `/dev/video11` 采集 FPS、CPU、内存和端到端延迟。

---

## 11. Framebuffer / DRM / Input 节点状态

当前尚未采集以下节点信息：

| 类型          | 节点                                        | 当前状态 |
| ----------- | ----------------------------------------- | ---- |
| Framebuffer | `/dev/fb*`                                | 待采集  |
| DRM/KMS     | `/dev/dri/*`, `/sys/class/drm/`           | 待采集  |
| Input       | `/dev/input/*`, `/proc/bus/input/devices` | 待采集  |

后续需要继续执行：

```bash
echo "========== framebuffer =========="
ls -l /dev/fb* 2>/dev/null || echo "no /dev/fb*"
for f in /sys/class/graphics/fb*; do
    [ -e "$f" ] || continue
    echo "---- $f ----"
    cat "$f/name" 2>/dev/null
    cat "$f/modes" 2>/dev/null
    cat "$f/virtual_size" 2>/dev/null
done

echo "========== drm =========="
ls -l /dev/dri/* 2>/dev/null || echo "no /dev/dri/*"
ls -l /sys/class/drm/ 2>/dev/null || echo "no /sys/class/drm"

echo "========== input =========="
ls -l /dev/input/* 2>/dev/null || echo "no /dev/input/*"
cat /proc/bus/input/devices 2>/dev/null || echo "no /proc/bus/input/devices"
```

Framebuffer / DRM 信息将用于判断当前屏幕显示链路是传统 framebuffer 方式，还是 DRM/KMS 方式。

Input 信息将用于判断系统是否识别了触摸屏、按键、键盘、鼠标或其他输入设备。

---

## 12. Board Devices Baseline 结论

当前开发板的 video/media 设备节点状态正常。系统已经成功枚举出 Rockchip 平台的 rkcif、rkisp、MIPI CSI-2、CSI-2 DPHY 和 SC3336 sensor 相关节点。

已经确认的关键信息如下：

| 项目                      | 结论                                        |
| ----------------------- | ----------------------------------------- |
| Sensor                  | SC3336                                    |
| Sensor entity           | `m00_b_sc3336 4-0030`                     |
| Sensor subdev           | `/dev/v4l-subdev2`                        |
| Sensor 输出格式             | `SBGGR10_1X10 / 2304x1296`                |
| MIPI CSI-2 控制器          | `rockchip-mipi-csi2`, `/dev/v4l-subdev0`  |
| CSI-2 DPHY              | `rockchip-csi2-dphy0`, `/dev/v4l-subdev1` |
| rkcif media controller  | `/dev/media0`                             |
| rkisp media controller  | `/dev/media1`                             |
| rkcif video nodes       | `/dev/video0` - `/dev/video10`            |
| rkisp output nodes      | `/dev/video11` - `/dev/video18`           |
| ISP statistics / params | `/dev/video19`, `/dev/video20`            |
| 推荐 YOLO 输入节点            | `/dev/video11`                            |
| 推荐 YOLO baseline 格式     | `864x480 NV21`                            |

当前摄像头链路可以认为已经打通：

```text
SC3336 sensor → CSI-2 DPHY → MIPI CSI-2 → rkcif → rkisp → /dev/video11
```

因此，当前 board devices baseline 的 video/media 部分状态良好。下一步应继续补充 framebuffer、DRM 和 input 节点枚举结果，并基于 `/dev/video11` 进行实际出帧测试和 YOLO 性能 baseline 采集。
# Board device enumeration

状态：待补采直接枚举

原计划保存以下命令的直接输出：

```bash
ls -l /dev/video* 2>/dev/null
ls -l /dev/media* 2>/dev/null
ls -l /dev/fb* 2>/dev/null
ls -l /dev/dri/* 2>/dev/null
ls -l /dev/input/* 2>/dev/null
ls -l /sys/class/drm 2>/dev/null
ls -l /sys/class/graphics 2>/dev/null
ls -l /sys/class/video4linux 2>/dev/null
```

本次该文件为空，暂未获得上述直接枚举结果。

可从其它日志间接确认的节点：

- `board_display.txt`：
  - `/dev/fb0`
  - `fb_st7789v`
  - `320,240`
  - `16` bpp
- `board_camera_media.txt`：
  - `/dev/media0`
  - `/dev/media1`
  - `/dev/video0` 到 `/dev/video20`
  - `/dev/v4l-subdev0`
  - `/dev/v4l-subdev1`
  - `/dev/v4l-subdev2`

后续建议补采直接枚举输出，特别是 `/dev/input/*`、`/dev/dri/*` 和 `/sys/class/drm`。
