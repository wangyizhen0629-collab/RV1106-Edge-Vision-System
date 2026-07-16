## 启动参数与 RootFS Baseline

### 1. 内核信息

执行命令：

```bash
uname -a
```

输出结果：

```text
Linux root 5.10.110 #45 Wed Apr 9 17:33:31 CST 2025 armv7l GNU/Linux
```

解析结果：

| 项目         | 内容                          |
| ---------- | --------------------------- |
| Linux 内核版本 | 5.10.110                    |
| 内核编译版本     | #45                         |
| 内核编译时间     | Wed Apr 9 17:33:31 CST 2025 |
| CPU 架构     | armv7l                      |
| 系统类型       | GNU/Linux                   |
| 主机名        | root                        |

当前开发板运行的是基于 Linux 5.10.110 的 32 位 ARM 嵌入式 Linux 系统。

---

### 2. 启动参数

执行命令：

```bash
cat /proc/cmdline
```

输出结果：

```text
user_debug=31 storagemedia=sd androidboot.storagemedia=sd androidboot.mode=normal rootwait earlycon=uart8250,mmio32,0xff4c0000 console=ttyFIQ0 snd_soc_core.prealloc_buffer_size_kbytes=16 coherent_pool=0 root=/dev/mmcblk1p7 rootfstype=ext4 rk_dma_heap_cma=66M blkdevparts=mmcblk1:32K(env),512K@32K(idblock),256K(uboot),32M(boot),512M(oem),256M(userdata),6G(rootfs),-(media)
```

解析结果：

| 项目                    | 内容                           |
| --------------------- | ---------------------------- |
| 启动介质                  | SD 卡                         |
| 启动模式                  | normal                       |
| RootFS 设备             | /dev/mmcblk1p7               |
| RootFS 文件系统类型         | ext4                         |
| rootwait              | 已启用                          |
| 控制台设备                 | ttyFIQ0                      |
| early console         | uart8250, mmio32, 0xff4c0000 |
| Rockchip DMA heap CMA | 66M                          |
| RootFS 分区大小           | 6G                           |
| 存储设备                  | mmcblk1                      |

从启动参数可以看出，当前系统从 SD 卡启动，根文件系统指定为 `/dev/mmcblk1p7`，文件系统类型为 ext4。

其中，`rootwait` 表示内核会等待 rootfs 设备就绪后再继续挂载根文件系统，这对于 SD 卡启动的系统比较重要，因为 SD 卡初始化可能需要一定时间。

`console=ttyFIQ0` 表示内核主要控制台输出到 `ttyFIQ0`。该参数在 Rockchip 平台较常见，后续如果需要通过串口查看启动日志，可以重点关注该控制台。

`rk_dma_heap_cma=66M` 表示系统为 Rockchip DMA heap / CMA 预留了 66MB 连续内存区域。该配置可能与摄像头、V4L2 buffer、RGA、DRM 显示、NPU 输入输出 buffer 等媒体链路有关，后续在分析摄像头和 YOLO pipeline 时需要记录。

---

### 3. RootFS 实际挂载状态

执行命令：

```bash
mount | grep " on / "
```

输出结果：

```text
/dev/root on / type ext4 (rw,relatime)
```

执行命令：

```bash
cat /proc/mounts | grep " / "
```

输出结果：

```text
/dev/root / ext4 rw,relatime 0 0
```

解析结果：

| 项目                | 内容             |
| ----------------- | -------------- |
| 实际挂载设备            | /dev/root      |
| 启动参数指定的 RootFS 设备 | /dev/mmcblk1p7 |
| 挂载点               | /              |
| 文件系统类型            | ext4           |
| 挂载模式              | rw，可读写         |
| 挂载选项              | relatime       |

虽然系统实际挂载时显示为 `/dev/root`，但结合启动参数中的 `root=/dev/mmcblk1p7` 可以判断，当前根文件系统实际来自 `/dev/mmcblk1p7`。

RootFS 当前以 `rw` 模式挂载，说明根文件系统是可读写的，可以在系统中保存文件、修改配置、放置测试脚本和记录日志。

---

### 4. RootFS 空间使用情况

执行命令：

```bash
df -h /
```

输出结果：

```text
Filesystem                Size      Used Available Use% Mounted on
/dev/root                 5.8G    283.6M      5.3G   5% /
```

解析结果：

| 项目         | 内容     |
| ---------- | ------ |
| RootFS 总容量 | 5.8G   |
| 已使用空间      | 283.6M |
| 可用空间       | 5.3G   |
| 使用率        | 5%     |
| 挂载点        | /      |

当前根文件系统剩余空间约为 5.3G，使用率仅为 5%。因此，当前不存在 rootfs 空间不足的问题，可以正常进行 baseline 脚本存放、日志记录和轻量级测试数据保存。

---

### 5. MMC / SD 分区节点

执行命令：

```bash
ls -l /dev/mmcblk1*
```

输出结果：

```text
brw-rw----    1 root     disk      179,   0 Jan  1 12:00 /dev/mmcblk1
brw-rw----    1 root     disk      179,   1 Jan  1 12:00 /dev/mmcblk1p1
brw-rw----    1 root     disk      179,   2 Jan  1 12:00 /dev/mmcblk1p2
brw-rw----    1 root     disk      179,   3 Jan  1 12:00 /dev/mmcblk1p3
brw-rw----    1 root     disk      179,   4 Jan  1 12:00 /dev/mmcblk1p4
brw-rw----    1 root     disk      179,   5 Jan  1 12:00 /dev/mmcblk1p5
brw-rw----    1 root     disk      179,   6 Jan  1 12:00 /dev/mmcblk1p6
brw-rw----    1 root     disk      179,   7 Jan  1 12:00 /dev/mmcblk1p7
brw-rw----    1 root     disk      179,   8 Jan  1 12:00 /dev/mmcblk1p8
```

根据启动参数中的分区信息：

```text
blkdevparts=mmcblk1:32K(env),512K@32K(idblock),256K(uboot),32M(boot),512M(oem),256M(userdata),6G(rootfs),-(media)
```

可以解析出如下分区布局：

| 分区节点           | 分区名称     |   大小 | 作用                                |
| -------------- | -------- | ---: | --------------------------------- |
| /dev/mmcblk1p1 | env      |  32K | U-Boot 环境变量                       |
| /dev/mmcblk1p2 | idblock  | 512K | 启动识别块 / 低级启动数据                    |
| /dev/mmcblk1p3 | uboot    | 256K | U-Boot                            |
| /dev/mmcblk1p4 | boot     |  32M | Kernel / DTB / boot image 等启动相关内容 |
| /dev/mmcblk1p5 | oem      | 512M | 厂商资源或应用资源                         |
| /dev/mmcblk1p6 | userdata | 256M | 用户数据                              |
| /dev/mmcblk1p7 | rootfs   |   6G | 根文件系统                             |
| /dev/mmcblk1p8 | media    | 剩余空间 | 媒体或数据分区                           |

当前系统中 `/dev/mmcblk1p1` 到 `/dev/mmcblk1p8` 均存在，说明实际检测到的分区节点与启动参数中的分区布局一致。其中 `/dev/mmcblk1p7` 对应 rootfs 分区，`/dev/mmcblk1p8` 对应 media 分区。

---

### 6. Baseline 结论

当前开发板运行 Linux 5.10.110 内核，平台架构为 32 位 ARM，即 `armv7l`。系统从 SD 卡启动，启动参数指定根文件系统为 `/dev/mmcblk1p7`，文件系统类型为 ext4。

系统实际运行时，根文件系统显示为 `/dev/root`，挂载到 `/`，文件系统类型为 ext4，挂载模式为 `rw`，即当前 rootfs 可读写。根文件系统总容量约为 5.8G，已使用 283.6M，可用空间约为 5.3G，使用率仅为 5%。

因此，当前 rootfs 状态正常，不存在空间不足或只读挂载问题。后续可以在该系统中放置 baseline 采集脚本、保存日志，并继续采集 BoardConfig、实际 DTB、媒体节点、摄像头节点、YOLO FPS、CPU、内存和延迟等信息。
