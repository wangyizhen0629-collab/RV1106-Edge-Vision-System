# ST7789V DRM 阶段三主机构建记录

日期：2026-08-27  
分支：`feat/st7789v-drm`  
起始 HEAD：`aad007f4a`

## Fresh ARM 配置与构建

```bash
cmake -S Demo/DeskBot_demo \
  -B /tmp/echo-mate-drm-app-build.n7cc49 \
  -DTARGET_ARM=ON \
  -DRV1106_SDK_PATH=/home/wangyizhen/Echo-Mate/SDK/rv1106-sdk \
  -DCMAKE_BUILD_TYPE=Release

cmake --build /tmp/echo-mate-drm-app-build.n7cc49 -j2
```

结果：配置成功；`lvgl/src/drivers/display/drm/lv_linux_drm.c.o` 编译成功；DeskBot `main` 构建到 100%。首次构建暴露 `drm.h` 未加入 `lvgl` target include path，修正 CMake target include 后在同一 fresh build 目录重建成功。

## 产物核对

```text
file: ELF 32-bit LSB executable, ARM, EABI5, dynamically linked
interpreter: /lib/ld-uClibc.so.0
size: 6504212 bytes
sha256: f74590f30d4d2027b2a8f9b1ae7cfcdc401e3713922180747acdee1737c9cf8e
dynamic dependency: libdrm.so.2
defined DRM symbols: lv_linux_drm_create, lv_linux_drm_set_file
default DRM device string: /dev/dri/card0
```

目标 sysroot 的 libdrm：

```text
usr/lib/libdrm.so -> libdrm.so.2
usr/lib/libdrm.so.2 -> libdrm.so.2.4.0
usr/lib/libdrm.so.2.4.0: 95740 bytes
```

SDK 应用打包入口 `project/app/deskbot/Makefile` 的 `DESKBOT_SOURCE` 指向根目录 `Demo/DeskBot_demo`，后续执行 app/OEM 构建会直接消费本轮修改；未在 SDK 目录维护第二份应用源码。

## 静态检查

```bash
git diff --check -- \
  Demo/DeskBot_demo/CMakeLists.txt \
  Demo/DeskBot_demo/conf/dev_conf.h \
  Demo/DeskBot_demo/lv_conf.h
```

结果：通过，无输出。

## 验证边界

本记录只证明目标架构配置、编译和链接成立。没有在主机运行 ARM 程序，也没有证明板端可以取得 DRM master、执行 atomic modeset/page flip 或正确显示；这些项目必须由实机日志和人工视觉验收补齐。
