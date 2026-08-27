# ST7789V DRM 阶段三板端验证摘要

日期：2026-08-27  
板型：RV1106 Echo-Mate  
内核：Linux 5.10.110  
显示：ST7789V、SPI0 CS0、320×240、RGB565  
应用 SHA-256：`f74590f30d4d2027b2a8f9b1ae7cfcdc401e3713922180747acdee1737c9cf8e`

## 功能与链路

- 板端 `/proc/<pid>/exe` 与主机交叉编译产物的 SHA-256 一致。
- Home 和 YOLO 采样中，进程 fd 3 均指向 `/dev/dri/card0`，并存在两个 card0 映射；没有发现进程持有 `/dev/fb0`。
- 用户确认颜色、方向、页面操作、offset 均正常，无明显撕裂、闪烁或花屏。
- 重复进入/退出 YOLO 正常，Home/YOLO 连续刷新约 30 分钟正常。
- Home、YOLO 两组采样的 `dmesg_before.txt` 与 `dmesg_after.txt` 分别逐字节一致，因此采样窗口内没有新增内核日志或 DRM/SPI 错误。

首次部署期间曾出现两类非最终状态问题：旧进程仍运行 FBDEV 产物；以及 OEM 内核模块目录/`insmod_ko.sh` 部署不完整。替换为正确 DRM 产物并恢复与当前内核匹配且可执行的模块加载脚本后，应用和重复 YOLO 生命周期恢复正常。它们属于应用/OEM 部署一致性问题，不是本轮最终 DRM 数据路径的运行错误。

## 同口径单轮 A/B

汇总命令：

```bash
python3 docs/tools/summarize_fb_display_baseline.py \
  docs/logs/st7789-drm/2026-08-27/home \
  docs/logs/st7789-drm/2026-08-27/yolo \
  docs/logs/st7789-drm/2026-08-27/drm/home \
  docs/logs/st7789-drm/2026-08-27/drm/yolo
```

| 场景 | 后端 | 采样时长 | 进程 CPU 均值/p95 | 系统 busy 均值 | RSS 均值/最大值 | HWM 最大值 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Home | FB/fbtft | 59 s | 3.00% / 4.00% | 7.64% | 7300 / 7300 KiB | 10952 KiB |
| Home | DRM/KMS | 59 s | 2.96% / 3.92% | 6.83% | 10636 / 10636 KiB | 13432 KiB |
| YOLO | FB/fbtft | 119 s | 35.71% / 37.76% | 51.19% | 13691 / 13828 KiB | 14360 KiB |
| YOLO | DRM/KMS | 119 s | 36.22% / 38.38% | 39.81% | 10872 / 10872 KiB | 13432 KiB |

单轮结果中，Home 进程 CPU 基本持平；YOLO 进程 CPU 增加 0.51 个百分点，属于基本持平。YOLO 系统 busy 下降 11.38 个百分点（约 22.2%），RSS 均值下降约 2819 KiB（约 20.6%）。但旧 FB 基线开启了 fbtft `debug=7`，内核打印会抬高系统开销；DRM Home 又是在此前进入 YOLO 后采集，RSS/HWM 起点不同。因此这些差值只作为趋势证据，不能单独归因于 DRM，也不作为最终性能提升百分比。

DRM 日志没有旧 fbtft 的 `Display update` debug 记录，故 DRM 的 SPI throughput 和“等效全屏传输 FPS”为 N/A；这不代表没有刷新。若要比较显示提交耗时，需要后续给 DRM flush/atomic commit 增加低扰动计时，或使用 tracepoint/逻辑分析仪。

## YOLO 应用指标

选取两组相同的 1200 个有效帧窗口：

| 后端 | FPS | e2e avg/p95 | infer avg/p95 | queue avg/p95 | failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| FB/fbtft | 8.889 | 174.010 / 193 ms | 89.000 / 94 ms | 60.057 / 76 ms | 0 |
| DRM/KMS | 8.807 | 174.041 / 191 ms | 91.174 / 95 ms | 58.846 / 76 ms | 0 |

YOLO 有效 FPS 与端到端 p95 基本持平，DRM 样本的 `timeouts/recoveries/failures` 为 `0/0/0`。退出页面时的最终记录为 1290 个有效帧、8.812 FPS、e2e p95 191 ms、failures 0。

## 内存与日志限制

- 采样时 `/proc/meminfo` 持续报告 `CmaTotal=67584 kB`、`CmaFree=0 kB`。当前 DRM dumb buffer 与 RKNN/VI/VPSS 能正常分配并运行，但这仍表示 CMA 余量不可见或已耗尽，是后续冷启动、重复生命周期和长稳需要持续观察的风险。
- `deskbot_log_tail.txt` 是滚动历史日志，包含恢复前的 RKNN `ENOMEM`、DRM dumb buffer 分配失败和 `No draw buffer` 记录，不能把它们误判为当前 A/B 窗口新增错误。当前窗口以 fd 证据、前后 dmesg 无变化及最终 `failures=0` 为准。
- 当前只有一轮正式同口径 A/B。阶段三的功能、30 分钟稳定性和基础性能验收可以收口；最终 Performance Report 应再做至少两轮冷启动同条件采样，并使用中位数与波动范围。

## 结论

DeskBot 已从 `/dev/fb0` 成功迁移为直接使用 `/dev/dri/card0`。画面、页面生命周期、30 分钟持续刷新和单轮基础性能均通过；没有观察到功能回退。现有数据支持“YOLO 应用吞吐基本不变、系统 busy 呈下降趋势”，但不足以给出排除 fbtft debug 与采样顺序影响后的最终 DRM 性能提升百分比。
