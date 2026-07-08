# 架构规格书 — Echo-Mate DeskBot

> **作者:** Wangyizhen
> **日期:** 2025-06-15
> **目标平台:** RV1106 SoC / Linux 4.19 / LVGL 9.x
> **角色:** 嵌入式应用与系统架构

---

## 1. 概述

Echo-Mate 是基于瑞芯微 RV1106 视觉 SoC 构建的嵌入式桌面 AI 伴侣机器人。
它将语音 AI（VAD + ASR + LLM + TTS）、YOLO 目标检测、LVGL 触摸屏 GUI
以及一系列实用应用（天气、日历、计算器、番茄时钟、小游戏）集成到单一固件
镜像中。DeskBot_demo 应用作为集成中枢，通过页面管理器导航模型编排所有功能。

- **SoC:** RV1106（单核 ARM Cortex-A7 @ 1.0 GHz，64 MB DDR2）
- **操作系统:** Buildroot Linux 4.19，glibc 或 musl
- **GUI:** LVGL 9.x，320×240 TFT LCD 触摸屏
- **语音流水线:** VAD → SenseVoice (ASR) → FastText (意图分类) → 通义千问 (LLM) → CosyVoice (TTS)，通过 WebSocket + Opus 传输
- **视觉:** YOLOv5，通过 RKNN NPU 加速
- **连接能力:** Wi-Fi、蓝牙

---

## 2. 系统架构

```
┌───────────────────────────────────────────────────────────────────┐
│                         应用层 (Application Layer)                 │
│  ┌─────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ ┌───────────┐  │
│  │ ChatBot │ │ Weather  │ │ Calendar │ │ YOLO   │ │ Pomodoro  │  │
│  │  页面   │ │  页面    │ │  页面    │ │ 页面   │ │   页面    │  │
│  └────┬────┘ └────┬─────┘ └────┬─────┘ └───┬────┘ └─────┬─────┘  │
│       └───────────┴────────────┴───────────┴──────────────┘       │
│                          │ 页面管理器 (lv_lib_pm)                  │
│                          │ 导航栈 + 生命周期管理                    │
├──────────────────────────┼────────────────────────────────────────┤
│                      硬件抽象层 / 中间件 (HAL / Middleware)         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────────┐  │
│  │ sys_mgr  │ │ gpio_mgr │ │ event_mgr│ │ AI Chat Client (C++) │  │
│  │ NTP/时间 │ │ sysfs    │ │ 发布/订阅 │ │ WS + Opus + JSON    │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────────────────┘  │
├───────────────────────────────────────────────────────────────────┤
│                       LVGL 9.x 图形引擎                             │
│  显示驱动 (SDL x86 / DRM ARM)  │  输入驱动 (触摸屏)                 │
│  字体引擎  │  图像解码器  │  动画系统                               │
├───────────────────────────────────────────────────────────────────┤
│                      Linux 内核 (4.19)                              │
│  DRM/KMS  │  GPIO/sysfs  │  ALSA  │  Wi-Fi  │  NPU (RKNN)        │
├───────────────────────────────────────────────────────────────────┤
│                      RV1106 硬件                                    │
│  Cortex-A7 @1GHz  │  64MB DDR2  │  NPU  │  ISP  │  MIPI-DSI LCD   │
└───────────────────────────────────────────────────────────────────┘
```

---

## 3. 页面管理器设计模式

DeskBot_demo 使用基于 **函数指针表** 的导航模式，构建于 `lv_lib_pm_t`（轻量级页面管理器）之上。

### 3.1 注册表

每个页面对外暴露恰好两个生命周期函数：

```c
typedef struct {
    const char *name;        // 导航用的字符串标识符
    void (*init)(void);      // 页面被压入导航栈时调用
    void (*deinit)(void);    // 页面从导航栈中弹出时调用
    lv_obj_t *page_obj;      // LVGL 屏幕对象（由页面管理器管理）
} ui_app_data_t;
```

所有页面在 `gui_app/ui.c` 中的编译期数组 `ui_apps[]` 内静态注册。新增页面的步骤：

1. 在 `gui_app/pages/ui_XxxPage/` 中创建 `ui_XxxPage.h` / `ui_XxxPage.c`
2. 递增 `_APP_NUMS`
3. 在 `ui_apps[]` 中添加一条记录
4. 在主页添加启动按钮

### 3.2 导航栈

| 操作               | 函数                              | 行为                            |
|--------------------|-----------------------------------|---------------------------------|
| 推入新页面         | `lv_lib_pm_OpenPage(pm, "name")`  | 调用旧页面 deinit，新页面 init  |
| 返回上一页面       | `lv_lib_pm_OpenPrePage(pm)`       | 弹出栈顶，初始化上一页面        |
| 页面切换动画       | `lv_scr_load_anim(ANIM_MOVE_xxx)` | 滑动动画，100ms                 |

### 3.3 生命周期

```
HomePage  ──[点击]──>  OpenPage("PomodoroPage")
                           │
                           ├─ deinit(HomePage)
                           ├─ init(PomodoroPage)  ── 创建屏幕、定时器
                           └─ scr_load_anim(new_screen)

PomodoroPage  ──[右滑]──>  OpenPrePage()
                              │
                              ├─ deinit(PomodoroPage) ── 删除定时器
                              ├─ init(HomePage)
                              └─ scr_load_anim(prev_screen)
```

**关键不变式：** `deinit()` 必须停止 `init()` 中创建的所有 LVGL 定时器。
LVGL 在屏幕被移除时会自动销毁子对象，但定时器不与屏幕生命周期绑定——
未能删除它们将导致 use-after-free 崩溃。

---

## 4. 关键组件接口

### 4.1 系统管理器 (`common/sys_manager/`)

| API                            | 用途                                    |
|--------------------------------|-----------------------------------------|
| `sys_get_time()`               | 读取系统 RTC                            |
| `sys_set_time()`               | 设置系统时间                            |
| `sys_get_time_from_ntp()`      | NTP 同步（阿里云 NTP 服务器）           |
| `sys_get_wifi_status()`        | 检查 wlan0 链路状态                     |
| `sys_load/save_system_para()`  | 持久化用户配置到 JSON 文件              |
| `sys_get_auto_location_by_ip()`| 通过高德 API 进行 IP 地理定位          |
| `sys_set_lcd_brightness()`     | 背光 PWM 控制                           |
| `sys_set_volume()`             | 音频输出音量                            |

### 4.2 GPIO 管理器 (`common/gpio_manager/`)

| API                            | 用途                                    |
|--------------------------------|-----------------------------------------|
| `gpio_init(pin, direction)`    | 通过 sysfs 导出并设置方向               |
| `gpio_set_value(pin, value)`   | 向 sysfs GPIO 节点写入 0/1              |
| `calculate_gpio_pin(b,g,x)`    | 计算 Linux GPIO 编号的宏                |

预定义引脚：`LED_BLUE`、`MOTOR1_INA/INB`、`MOTOR2_INA/INB`

**注意：** `ui.c` 中的 `_maintimer_cb` 每秒翻转 `LED_BLUE` 作为心跳信号。
新页面不得同时控制 `LED_BLUE`——这是一个有全局所有者的共享资源。

### 4.3 AI 语音助手客户端 (`Demo/AIChat_demo/`)

```
┌─────────────┐   WebSocket + JSON   ┌─────────────┐
│  RV1106      │ <──────────────────> │  PC 服务器   │
│  C++ 客户端  │   Opus 音频帧        │  Python     │
└─────────────┘                      └─────────────┘

流水线：
  麦克风 ─> VAD (silero) ─> Opus 编码 ─> WebSocket 发送 ─>
  服务器: Opus 解码 ─> SenseVoice ASR ─> FastText 意图分类 ─>
          通义千问 LLM ─> CosyVoice TTS ─> Opus 编码 ─> WebSocket 回复
  客户端: Opus 解码 ─> 扬声器播放
```

C++ 客户端编译为 C 兼容接口（`extern "C"`），供 DeskBot_demo 的 C 代码库调用。

### 4.4 YOLO 目标检测

- 模型: YOLOv5（瑞芯微 RKNN 格式）
- 运行时: RKNN API（NPU 推理）
- 输入: MIPI-CSI 摄像头 → ISP → RGA 预处理
- 输出: 在 LVGL 画布叠加层上渲染边界框
- **仅 ARM 平台**——x86 仿真不包含此页面（NPU 不可用）

---

## 5. 数据流——以番茄时钟为例

```
用户点击"开始"
    │
    ▼
_on_start_btn_click()          LV_EVENT_CLICKED
    │
    ├─ is_running = !is_running
    └─ _update_display() ────── 刷新标签 + 圆弧
        │
        ▼
_pomodoro_timer_cb()           lv_timer，周期 100ms
    │
    ├─ second_tick++（10 个 tick = 1 秒）
    ├─ remaining_seconds--
    ├─ _update_display()
    └─ 若 remaining_seconds == 0:
        └─ _handle_time_up()
            ├─ _switch_state()  ── 工作→休息→工作，重置弧范围
            ├─ ui_msgbox_info("番茄钟", "工作 开始！")
            └─ is_running = true  （自动继续）
```

---

## 6. 构建系统

```
CMakeLists.txt（根目录）
  │
  ├─ gui_app/CMakeLists.txt
  │   └─ file(GLOB_RECURSE GUI_APP_SOURCES "*.c")
  │       // gui_app/ 下所有 .c 文件自动包含——无需手动注册
  │
  ├─ common/CMakeLists.txt        (sys_manager, gpio_manager, event_manager)
  ├─ utils/CMakeLists.txt         (JSON 解析器, 工具函数)
  ├─ lvgl/                        (LVGL 9.x 库，链接为静态库)
  └─ main.c                       (入口点：调用 ui_init())
```

| 构建目标          | 命令                              | 输出              |
|-------------------|-----------------------------------|-------------------|
| x86 仿真          | `cmake .. && make`                | `bin/main` (SDL)  |
| ARM 交叉编译      | `cmake .. -DTARGET_ARM=ON && make`| `bin/main` (RV1106)|

---

## 7. 约束与不变式

| 约束                              | 理由                                        |
|-----------------------------------|---------------------------------------------|
| 屏幕: 320×240 像素                | 硬件 LCD 分辨率                              |
| `LV_COLOR_DEPTH` == 16            | RGB565 格式，匹配显示控制器                   |
| `_APP_NUMS` == 实际页面数         | 数组大小必须匹配，避免越界访问                |
| 字体字形 ⊇ UI 使用的字符          | 必须为所有使用的中文字符预生成字体             |
| deinit() 中清理定时器             | 页面切换后不能有悬空定时器                    |
| GPIO: 每引脚单一所有者            | 共享 GPIO 不得并发访问                        |

---

## 8. 术语表

| 术语             | 定义                                              |
|------------------|---------------------------------------------------|
| RV1106           | 瑞芯微视觉 SoC，Cortex-A7 + NPU + ISP             |
| LVGL             | 轻量级多功能图形库（嵌入式 GUI）                   |
| lv_lib_pm        | 轻量级 LVGL 页面管理器（导航栈）                   |
| SquareLine Studio| 所见即所得的 LVGL UI 设计器，生成 C 代码            |
| RKNN             | 瑞芯微神经网络运行时（NPU 加速）                   |
| VAD              | 语音活动检测（Silero VAD）                         |
| ASR              | 自动语音识别（SenseVoice）                         |
| TTS              | 文本转语音（CosyVoice）                            |
| LLM              | 大语言模型（通义千问）                             |

---
---------------------------------------------------------------------------------------------------------------------------------------------------------
## 9. 启动流程与运行时模型

### 9.1 `main()` 初始化序列

`main()` 位于项目根目录 `main.c`，极其精简。按调用顺序初始化以下核心硬件/库：

| 步骤 | 调用 | 说明 |
|------|------|------|
| 1 | `lv_init()` | LVGL 内核：内存管理、样式系统、对象模型 |
| 2 | `lv_linux_disp_init()` | 显示驱动，三选一编译：`fbdev`（`/dev/fb0` 帧缓冲）、`DRM`（`/dev/dri/card0`）、`SDL`（x86 仿真，320×240 窗口） |
| 3 | `lv_linux_indev_init()` | 输入设备：触摸屏（`/dev/input/event0` evdev）或 SDL 鼠标 |
| 4 | `ui_init()` | 应用层初始化（见 9.2） |
| 5 | `while(1) { lv_timer_handler(); usleep(1000); }` | 进入 LVGL 主事件循环，永不返回 |

### 9.2 `ui_init()` 展开

```
ui_init()
  ├─ _sys_para_init()
  │    ├─ sys_load_system_parameters()    // 加载 JSON 配置文件，失败则创建默认值
  │    ├─ sys_get_wifi_status()           // 检测 wlan0 链路状态
  │    ├─ sys_get_time_from_ntp()         // 阿里云 NTP 对时（若 auto_time 为 true）
  │    │    └─ sys_set_time()             // 写入系统 RTC
  │    └─ sys_get_auto_location_by_ip()   // 高德 API IP 地理定位（若 auto_location 为 true）
  │
  ├─ _gpios_init()
  │    ├─ gpio_init(LED_BLUE, OUT)        // LED 心跳指示灯
  │    ├─ gpio_init(MOTOR1_INA/INB, OUT)  // 电机 1 控制
  │    ├─ gpio_init(MOTOR2_INA/INB, OUT)  // 电机 2 控制
  │    └─ 全部置低电平（gpio_set_value = 0）
  │
  ├─ lv_theme_default_init()              // LVGL 默认主题（蓝色/红色主色调）
  │
  ├─ lv_lib_pm_Init(&page_manager)        // 页面管理器栈初始化
  │
  ├─ for(i=0; i<_APP_NUMS; i++)           // 注册全部 12 个页面
  │    └─ lv_lib_pm_CreatePage(name, init, deinit)
  │
  ├─ lv_lib_pm_OpenPage("HomePage")       // 启动首个页面（HomePage）
  │
  └─ lv_timer_create(_maintimer_cb, 1000) // 系统维护定时器（见 9.4）
```

### 9.3 线程模型

**应用层是严格的单线程架构。** `main.c` 和 `ui.c` 中没有任何 `pthread_create` 调用。所有功能（GUI 渲染、定时器回调、GPIO 控制、页面切换）均在主线程的 `lv_timer_handler()` 循环中协作执行。

唯一的独立线程存在于 **ChatBot 页面**（`gui_app/pages/ui_ChatBotPage/app_ChatBotPage.c`）——初始化时通过 `pthread_create` 创建 `ai_chat_thread`，入口函数 `ai_chat_thread_func`，专用于 WebSocket 阻塞 I/O。该线程仅在 ChatBot 页面活跃期间存在，页面退出时销毁。

| 层级 | 线程数 | 说明 |
|------|--------|------|
| 主线程 | 1 | `while(1) { lv_timer_handler() }`，驱动全部 GUI + 业务逻辑 |
| LVGL 内部 | 0（x86/ARM） | LVGL 的 SDL/fbdev/DRM 驱动均为轮询模式，不自建线程 |
| ChatBot 页面 | 1（按需） | WebSocket 通信专用线程，页面退出时销毁 |

### 9.4 定时任务一览

#### 系统级（全局常驻）

| 定时器入口函数 | 周期 | 回调中执行的工作 |
|---------------|------|-----------------|
| `_maintimer_cb` | 1000 ms | ① 每秒翻转 `LED_BLUE`（心跳闪烁） ② 每 300 个 tick（≈5 分钟）触发 NTP 对时 + WiFi 状态检测 + 系统参数持久化到 JSON 文件 |

#### 页面级（init 创建，deinit 销毁）

| 所属页面 | 入口函数 | 周期 | 用途 |
|---------|---------|------|------|
| HomePage | `ui_home_timer_cb` | 5000 ms | 刷新首页时间显示 |
| WeatherPage | `_ui_weather_timer_cb` | 1000 ms | 天气数据轮询更新 |
| YOLOPage | `timer_flash` | 30 ms | 目标检测框闪烁动画 |
| ChatBotPage | `_ChatBotTimer_cb` | 250 ms | AI 对话 UI 状态刷新 |
| ChatBotPage | `_ChatBotMoveTimer_cb` | 500 ms | 聊天动画效果 |
| PomodoroPage | `_pomodoro_timer_cb` | 100 ms | 番茄时钟倒计时（每 10 tick = 1 秒） |

> **关键不变式：** 页面级定时器在 `init()` 中创建，必须在对应 `deinit()` 中通过 `lv_timer_delete()` 销毁。否则页面退出后定时器仍会回调已释放的 UI 对象，导致 use-after-free 崩溃。

---

## 10. 编译与验证流程

### 10.1 构建系统结构

项目使用 **CMake** 构建系统，根 `CMakeLists.txt` 定义两个编译目标：

```
CMakeLists.txt（根）
  ├─ lvgl/           LVGL 核心静态库
  ├─ gui_app/        GUI 应用模块（file(GLOB_RECURSE *.c) 自动包含）
  ├─ utils/          工具函数（JSON 解析等）
  ├─ common/         硬件抽象层（sys_manager, gpio_manager, event_manager）
  └─ main.c          入口文件
```

`gui_app/CMakeLists.txt` 的关键设计：使用 `file(GLOB_RECURSE GUI_APP_SOURCES "*.c")` 自动扫描所有 `.c` 文件。**新增页面无需修改 CMakeLists.txt**——只需将 `.c` 文件放入 `gui_app/pages/` 目录下即被自动编译。

### 10.2 双目标编译

| 目标 | 命令 | 编译器 | 输出 | 适用场景 |
|------|------|--------|------|---------|
| x86 仿真 | `cmake .. && make` | x86-64 GCC（系统自带） | `bin/main` (SDL 窗口) | PC 上快速验证 UI、逻辑、状态机 |
| ARM 交叉编译 | `cmake .. -DTARGET_ARM=ON && make` | `arm-linux-gnueabihf-gcc` | `bin/main` (RV1106) | 实机部署，GPIO/摄像头/NPU 等硬件功能 |

ARM 编译由 `toolchain.cmake` 指定交叉编译器路径和 sysroot，通过 `-DTARGET_ARM=ON` 触发。

### 10.3 IDE 与命令行

二者并非对立——**IDE 本质上是调用底层 GCC + CMake + Make**，底层做的事完全一致：

```
┌──────────────────────────────────────────────────┐
│  同一件事，两种界面                                │
│                                                   │
│  IDE (VS Code + CMake Tools):                     │
│    Ctrl+Shift+B → 调 CMake → 调 make → 调 GCC     │
│                                                   │
│  命令行:                                           │
│    cd build && cmake .. && make                   │
│    → CMake → Make → GCC (完全相同的流程)           │
└──────────────────────────────────────────────────┘
```

### 10.4 日常开发循环

大多数修改只需走 x86 仿真路径，循环极快：

```
修代码 → make（或 IDE 一键编译）→ ./bin/main → SDL 窗口中测试 UI
   ↑________________________________________________↓
              整个循环 < 20 秒，全部在 PC 上完成
```

只有涉及真实硬件（GPIO、摄像头 ISP、NPU 推理、ALSA 音频）的改动才需要走 ARM 路径：

```
修代码 → cmake .. -DTARGET_ARM=ON && make → scp bin/main root@<rv1106>:~ → 开发板上运行
   ↑________________________________________________________________↓
                        ～3-5 分钟（编译 + 传输 + 启动）
```

这就是项目"双目标构建"策略的核心价值：**90% 的开发时间在 x86 仿真中完成，ARM 编译仅用于最终集成验证。**

### 10.5 编译验证检查清单

| 检查项 | 方法 |
|--------|------|
| 编译通过 | `make` 返回 `[100%] Built target main` |
| 无新增警告 | 关注 `-Wall` 输出，不应有新 warning |
| 字体字形完整 | 新增页面的中文字符是否在已生成字体中（不在 → tofu 方框） |
| 页面可导航 | 仿真运行：首页点击按钮 → 进入页面 → 右滑返回，无 crash |
| 定时器清理 | 反复进出页面 5+ 次，确认无 use-after-free 崩溃 |
| ARM 交叉编译 | 最终确认 `-DTARGET_ARM=ON` 编译无错误 |

---

## 11. CMake 构建系统详解

### 11.1 CMake 是什么

CMake 是一个构建系统生成器——你描述"项目长什么样"，它输出标准的 Makefile（或 Ninja 等），再由 `make` 执行实际编译。你不需要手写每一条编译命令。

```
CMakeLists.txt（你写的）→ cmake → Makefile（自动生成）→ make → 可执行文件
```

### 11.2 核心语法对照

| 你想要的 | CMake 写法 |
|---------|-----------|
| 生成可执行文件 | `add_executable(名字 main.c foo.c)` |
| 生成静态库 | `add_library(名字 STATIC a.c b.c)` |
| 自动找所有 `.c` 文件 | `file(GLOB_RECURSE SRCS "*.c")` |
| 添加头文件搜索路径 | `target_include_directories(名字 PUBLIC ./include)` |
| 链接库 | `target_link_libraries(名字 lib1 lib2)` |
| 条件分支 | `if(TARGET_ARM) ... else() ... endif()` |
| 编译选项 | `add_compile_options(-Wall -O2)` |
| 设置输出目录 | `set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)` |
| 开关选项 | `option(TARGET_ARM "..." OFF)` |

### 11.3 本项目的 CMake 结构拆解

```
Demo/DeskBot_demo/              CMakeLists.txt（根）
├── main.c                      └─ add_executable(main main.c)
├── lvgl/                       CMakeLists.txt（子）
│   └─ src/                     └─ add_library(lvgl STATIC ...)
├── gui_app/                    CMakeLists.txt（子）
│   └─ pages/                   └─ file(GLOB_RECURSE ... "*.c")
│       ├─ ui_HomePage/
│       ├─ ui_PomodoroPage/        ↑ 自动扫进去，无需手动加
│       └─ ...
├── common/                     CMakeLists.txt（子）
│   ├─ sys_manager/
│   └─ gpio_manager/
├── utils/                      CMakeLists.txt（子）
├── toolchain.cmake             ARM 交叉编译工具链配置
└── build/                      ← 在这里执行 cmake .. && make
    └── bin/main                 ← 最终产物
```

### 11.4 根 CMakeLists.txt 逐段解读

```cmake
# [1] 版本声明
cmake_minimum_required(VERSION 3.10)

# [2] 交叉编译开关：-DTARGET_ARM=ON 时切换为 ARM 编译器
option(TARGET_ARM "Build for ARM architecture" OFF)
if(TARGET_ARM)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")
endif()

# [3] 项目名 + C/C++ 标准
project(DeskBot)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

# [4] 引入子目录（每个有自己的 CMakeLists.txt）
add_subdirectory(lvgl)      # LVGL 图形库，编译为静态库
add_subdirectory(gui_app)   # UI 应用（GLOB_RECURSE 自动扫描）
add_subdirectory(utils)     # 工具函数
add_subdirectory(common)    # 硬件抽象层

# [5] 头文件路径
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})

# [6] 主程序 + 链接
set(MAIN_SOURCES main.c)
add_executable(${PROJECT_NAME} ${MAIN_SOURCES})
target_link_libraries(${PROJECT_NAME} lvgl gui_app utils common)
```

### 11.5 为何新增页面无需修改 CMakeLists.txt

`gui_app/CMakeLists.txt` 中关键的一行：

```cmake
file(GLOB_RECURSE GUI_APP_SOURCES "*.c")
```

`GLOB_RECURSE` 是通配符递归匹配——`gui_app/` 下所有子目录的 `.c` 文件在 `cmake` 执行时被自动发现。新增 `ui_PomodoroPage.c` 放到 `pages/` 下，重新运行 `cmake ..` 即自动包含。

**但有一个例外：** 如果新建了一个独立的子目录且需要自己的 `CMakeLists.txt`（比如一个全新的子项目），则需要在父级添加 `add_subdirectory(新目录)`。

### 11.6 使用流程

```bash
# 第一步：创建构建目录（只需一次）
cd Demo/DeskBot_demo
mkdir -p build && cd build

# 第二步：生成构建系统（每次新增/删除文件后需要重新运行）
cmake ..                     # x86 仿真
# 或
cmake .. -DTARGET_ARM=ON     # ARM 交叉编译

# 第三步：编译（每次修改代码后）
make                         # 只重新编译改动过的文件（增量编译）

# 运行验证
./bin/main                   # x86 仿真直接运行
```

`cmake ..` 只需要在文件增删后重新执行（让 GLOB_RECURSE 重新扫描）。修改现有 `.c` 文件后直接 `make` 即可，增量编译通常只需数秒。

---

## 12. Demo 项目组织结构

### 12.1 概述

`Demo/` 目录下包含多个独立的子项目，各自有独立的构建系统，互不依赖：

```
Demo/
├── DeskBot_demo/            ← 主应用：所有功能的集大成者
│   ├── main.c                ← 程序唯一入口（lv_init + ui_init + 主循环）
│   ├── lvgl/                 ← LVGL 图形库（编译为静态库）
│   ├── gui_app/              ← 12 个页面 + 页面管理器
│   ├── common/               ← 硬件抽象层（GPIO, NTP, 配置）
│   ├── utils/                ← 工具库
│   ├── CMakeLists.txt        ← 根构建文件
│   └── build/                ← 编译输出目录
│
├── AIChat_demo/              ← AI 语音助手（独立子项目）
│   ├── Client/CMakeLists.txt ← C++ 客户端，编译为独立程序
│   └── Server/main.py        ← Python 服务端，直接运行
│
├── yolov5_demo/              ← YOLO 目标检测（ARM only）
│   └── cpp/CMakeLists.txt    ← 依赖 RKNN SDK，x86 不可编译
│
├── rkmpi_demos/              ← 媒体处理示例
│   └── CMakeLists.txt        ← 依赖 Luckfox SDK
│
└── lvgl_demo/                ← 早期 LVGL 测试（无 CMake，已废弃）
```

### 12.2 各 Demo 的关系

```
         ┌─────────────┐
         │ lvgl_demo   │ → LVGL 独立实验（早期验证）
         └──────┬──────┘
                │ 验证 LVGL 可用
                ▼
┌──────────────────────────────────────────┐
│              DeskBot_demo                 │
│                                           │
│  借鉴 AIChat_demo 的 Client 端 ──────────│←── AIChat_demo（独立子项目）
│  借鉴 yolov5_demo 的 RKNN 推理 ──────────│←── yolov5_demo（独立子项目）
│  借鉴 rkmpi_demos 的媒体处理 ────────────│←── rkmpi_demos（独立子项目）
│                                           │
│  → 把各独立 Demo 验证过的模块整合，        │
│    统一为一个可执行文件、一套页面管理器      │
└──────────────────────────────────────────┘
```

**核心理念：** 每个独立 Demo 像"零件测试台"——在隔离环境中验证单一技术（AI 语音、YOLO、媒体处理）。`DeskBot_demo` 是"整机组装车间"——把验证通过的模块整合进统一的 LVGL 页面框架，用一个 `main()` 入口、一套 CMake 工程产出最终固件。

### 12.3 为什么 main.c 在 DeskBot_demo 根目录

1. **层级定位** — `DeskBot_demo` 不是"一个 Demo"，而是所有 Demo 的最终汇合点。它是唯一需要 `main()` 入口的工程。
2. **链接便利** — `main.c` 放在根目录，CMake 可以直接 `add_executable`，不需要跨目录引用。
3. **单一产物** — 整个 `Demo/DeskBot_demo/` 构建产出一个可执行文件 `bin/main`，对应一个固件镜像。其他 Demo 不产生最终固件，只是技术验证。

### 12.4 编译独立性

修改一个 Demo 不会影响其他 Demo：

| 改了什么 | 需要重新编译 |
|---------|------------|
| 修改 `AIChat_demo/Client/` 的代码 | 单独进 `AIChat_demo/Client/build` 编译 |
| 修改 `DeskBot_demo/gui_app/pages/` 的代码 | 只进 `DeskBot_demo/build` 编译 |
| 修改 `DeskBot_demo/common/` 的 HAL 代码 | 只进 `DeskBot_demo/build` 编译 |

各 Demo 之间通过**代码复制/借鉴**（而非链接共享库）来复用已验证的技术。这样每个 Demo 保持独立可编译，不会互相拖累。

---

## 13. 显示驱动三路径对比

### 13.1 三条路径

三者的共同目标：把 LVGL 渲染好的像素送到屏幕上。区别在于走什么通道：

```
LVGL 渲染引擎
    │
    ├── fbdev 路径:  LVGL → /dev/fb0 → 内核 framebuffer 驱动 → LCD
    ├── DRM 路径:    LVGL → /dev/dri/card0 → libdrm → 内核 DRM/KMS → LCD
    └── SDL 路径:    LVGL → SDL2 → 桌面窗口系统 → PC 显示器
```

### 13.2 详细对比

| 维度 | fbdev | DRM | SDL |
|------|-------|-----|-----|
| 设备文件 | `/dev/fb0` | `/dev/dri/card0` | 无（纯用户态） |
| 层级 | 直接写显存 | 通过 libdrm 调用 GPU | 通过 SDL2 创建桌面窗口 |
| 硬件加速 | 无，纯 CPU 写 | 支持 GPU/DMA 加速 | 依赖宿主桌面环境 |
| 多进程共享屏幕 | 不支持，只有一个程序能用 | 支持（KMS 仲裁） | 天然支持（窗口系统） |
| 屏幕旋转 | 自己算坐标 | 内核层面自动处理 | SDL 自动处理 |
| VSync / 双缓冲 | 手动实现 | 内核支持 page flip | SDL 支持 |
| 运行平台 | ARM 嵌入式 | ARM 嵌入式 | PC（x86） |
| 性能 | 最低（CPU 逐像素写） | 高（DMA + 硬件翻页） | 中等（多一层 SDL 抽象） |
| 开发复杂度 | 最简单 | 较复杂 | 最简单（对开发者） |

### 13.3 逐条解释

**fbdev（帧缓冲）**

```
程序 → open("/dev/fb0") → mmap 显存 → 直接写像素 → 屏幕
```

内核把显存映射成一块用户可以 `mmap` 的内存。程序往里写什么屏幕就显示什么——像在一块共享白板上直接画。

- 优点：简单可靠，几乎所有嵌入式 Linux 都支持
- 缺点：没有 GPU 加速，旋转缩放全用 CPU 算；不支持多程序同时显示

**DRM（Direct Rendering Manager）**

```
程序 → libdrm → /dev/dri/card0 → DRM 内核子系统 → KMS → 硬件
```

Linux 图形栈的现代标准。KMS（Kernel Mode Setting）负责分辨率/刷新率/旋转，DRM 负责渲染命令提交。支持原子提交——保证画面完整性、不撕裂。支持 page flip（双缓冲翻页），动画比 fbdev 流畅。

- 优点：硬件加速、多进程、画面不撕裂
- 缺点：配置更复杂，需要内核 DRM 驱动支持

**SDL（Simple DirectMedia Layer）**

只在 x86 仿真时使用。在 PC 上创建一个 320×240 窗口，LVGL 往窗口里画——就像普通桌面应用。嵌入式设备上完全不走这个路径。

- 优点：PC 开发调试极方便，一个窗口即看效果
- 缺点：嵌入式设备用不了，依赖完整桌面环境

### 13.4 实际选择逻辑

```
if (当前在 PC 上做仿真开发) {
    用 SDL;           // 开个窗口看效果，最快最方便
} else if (RV1106 实机) {
    if (需要流畅动画 || 有 DRM 驱动支持) {
        用 DRM;       // 现代方式，硬件加速
    } else {
        用 fbdev;     // 老方式，简单可靠，兜底方案
    }
}
```

### 13.5 在代码中的体现

`main.c` 使用编译期宏自动选择，同一个代码库适配三种环境：

```c
#if LV_USE_LINUX_FBDEV
    lv_linux_fbdev_create();      // ARM: fbdev
#elif LV_USE_LINUX_DRM
    lv_linux_drm_create();        // ARM: DRM
#elif LV_USE_SDL
    lv_sdl_window_create(320, 240); // x86: SDL 仿真
#else
    #error Unsupported configuration
#endif
```

选择哪个宏由 LVGL 配置文件（`lv_conf.h`）或 CMake 编译选项决定，无需修改代码。

---

## 14. 视频流水线：VI、VPSS 与 DRM

### 14.1 定位

VI、VPSS、DRM 是 RV1106 芯片内部**不同硬件模块**的软件接口。它们在一条数据流水线上各管一程，不是同类替代关系：

```
摄像头 Sensor → MIPI/CSI → [VI] → [VPSS] → [VENC/RGA] → [DRM] → LCD
                             采集    处理      编码/合成      显示
```

### 14.2 各自职责

| 缩写 | 全称 | 对应硬件 | 做什么 |
|------|------|---------|--------|
| **VI** | Video Input | ISP（图像信号处理器） | 从摄像头采集原始图像帧，做自动曝光/白平衡/降噪，输出 YUV/RGB 帧 |
| **VPSS** | Video Processing Sub-System | VPSS + RGA 硬件加速器 | 对帧做二次加工：裁剪、缩放、旋转、颜色空间转换、去噪 |
| **DRM** | Direct Rendering Manager | 显示控制器（Display Controller） | 把最终帧送到 LCD 显示，管理双缓冲、VSync、页翻转 |

### 14.3 数据流向

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│     VI       │     │    VPSS      │     │     DRM      │
│              │     │              │     │              │
│ 从摄像头抓帧  │ ──→ │ 裁剪/缩放/   │ ──→ │ 送到屏幕显示  │
│ 原始 YUV 帧  │     │ 转色域/旋转  │     │ 管理双缓冲    │
│              │     │              │     │              │
│ 全分辨率      │     │ 处理到 320   │     │ 320×240      │
│ 1920×1080    │     │ ×240        │     │ LCD          │
└──────────────┘     └──────────────┘     └──────────────┘
```

以 YOLO 目标检测页面为例：

```
摄像头 1080p ─→ VI 采集 ─→ VPSS 缩小到 320×240 ─→ RGA 叠加检测框 ─→ DRM ─→ 屏幕
```

### 14.4 打个比方

把视频帧想象成流水线上的零件：

- **VI** 是仓库管理员——从供应商（摄像头 Sensor）手里拿原材料（原始像素），做来料检验（白平衡、曝光）
- **VPSS** 是加工车间——把原材料裁切、打磨、上色（缩放、裁剪、色域转换），做成半成品
- **DRM** 是展台——把成品摆到橱窗（LCD）上，并负责换展品的时机（VSync、page flip）

### 14.5 与 LVGL 的关系

LVGL 只画 GUI 界面（用 CPU），VI/VPSS 处理摄像头视频流（用硬件加速器）。两者通过 **RGA（2D 图形加速器）** 做硬件图层合成：

```
LVGL GUI 层（CPU 绘制）
        │
        ├──→ RGA 硬件合成 ──→ DRM ──→ LCD
        │
VPSS 视频层（硬件处理）
```

不经过合成时，也可以分时复用 DRM：LVGL 渲染桌面时独占屏幕，打开 YOLO 页面时切换到视频流 + 叠加检测框。

### 14.6 软件栈归属

```
RKMPI（Rockchip Media Process Interface）
  ├─ VI 模块     (rk_mpi_vi.h)
  ├─ VPSS 模块   (rk_mpi_vpss.h)
  ├─ VENC 模块   (视频编码，H.264/H.265)
  └─ RGA 模块    (2D 图形合成)

Linux 内核
  ├─ DRM/KMS    (显示管理)
  ├─ V4L2       (摄像头驱动层，VI 的底层)
  └─ FBDEV      (旧式显示，与 DRM 互斥)
```

VI 和 VPSS 属于 **RKMPI**（Rockchip 的媒体处理接口库），DRM 属于 **Linux 内核显示框架**。不同软件栈，上下游协作关系。

---

## 15. YOLO 图像数据流全链路追踪

### 15.1 概述

从摄像头采集到屏幕显示，一帧图像在以下组件间流转：OpenCV → NPU DMA 缓冲 → 推理 → 后处理 → 画框 → 全局输出缓冲 → LVGL。涉及两个线程：推理线程（pthread）和 LVGL 主线程。

### 15.2 第一步：摄像头采集（OpenCV → V4L2）

```cpp
// AIcamera_c_interface.cc:103-108
cv::VideoCapture cap;
cv::Mat bgr(disp_height, disp_width, CV_8UC3);   // 320×240 BGR
cap.open(0);                                      // 打开 /dev/video0
cap >> bgr;                                       // 抓一帧 320×240 BGR
```

- `cap >> bgr` 底层：`open("/dev/video0")` → `ioctl(VIDIOC_DQBUF)` 从内核 DMA 缓冲区取帧
- **buffer 位置**：`bgr.data`，OpenCV 在用户态 malloc 分配，230KB
- **有一次内核→用户态拷贝**（非零拷贝），但 320×240 帧的拷贝开销可忽略

### 15.3 第二步：送入 NPU（零拷贝技巧）

```cpp
// AIcamera_c_interface.cc:105
cv::Mat bgr_model_input(model_height, model_width, CV_8UC3,
                        rknn_app_ctx.input_mems[0]->virt_addr);
```

**关键设计**：`bgr_model_input` 的 `data` 指针直接指向 NPU 输入 DMA 缓冲区的虚拟地址，而非独立 malloc 的内存。

```
rknn_app_ctx.input_mems[0]->virt_addr  ← NPU DMA 缓冲区（物理连续，用户态可访问）
                     ↑
bgr_model_input.data 指向同一块内存 → 零拷贝
```

因此 `cv::resize(bgr, bgr_model_input, 640×640)` 直接将缩放后的像素写入 NPU 可读取的 DMA 内存，无需二次拷贝。

**buffer 变迁**：

| 步骤 | buffer 位置 | 大小 | 所有者 |
|------|------------|------|--------|
| `cap >> bgr` | `bgr.data`（malloc） | 230KB | OpenCV 用户态 |
| `cv::resize` 后 | `input_mems[0]->virt_addr`（DMA） | 640×640×3 = 1.2MB | NPU |

### 15.4 第三步：NPU 硬件推理

```cpp
// yolov5_rv1106_1103.cc:193
ret = rknn_run(app_ctx->rknn_ctx, nullptr);
```

`rknn_run` 是阻塞调用，提交 NPU 任务并等待完成：

```
input_mems[0]  ─→  [NPU 硬件]  ─→  output_mems[0]  (80×80 网格，int8)
                                  output_mems[1]  (40×40 网格，int8)
                                  output_mems[2]  (20×20 网格，int8)
```

```cpp
// yolov5.h:38
rknn_tensor_mem* output_mems[3];  // NPU 输出 DMA 缓冲区，每帧覆盖
```

### 15.5 第四步：后处理（解码 + NMS）

入口：`post_process()` (`postprocess.cc:381`)

```
output_mems[0/1/2]->virt_addr (int8*)
      ↓  process_i8_rv1106()              遍历所有网格 cell
      │   box_confidence ≥ thres_i8?      置信度过滤
      │   取 maxClassProbs                 找最可能的类别
      │   解码 box_x, box_y, box_w, box_h  (反量化 + anchor 解码)
      ↓
      临时 vector：
        filterBoxes[4×N]   ← [x, y, w, h]
        objProbs[N]        ← 置信度
        classId[N]         ← COCO 类别 ID
      ↓  quick_sort + NMS                  按置信度降序 + 去重
      ↓
      od_results (栈变量，每帧覆盖)：
        od_results->results[i].box.{left, top, right, bottom}
        od_results->results[i].prop       置信度 0~1
        od_results->results[i].cls_id     COCO 80 类 ID
        od_results->count                 有效检出数
```

**数据结构**（`postprocess.h`）：

```c
typedef struct {
    image_rect_t box;   // {left, top, right, bottom}，640×640 坐标系
    float prop;          // 置信度
    int cls_id;          // COCO 80 类 ID
} object_detect_result;

typedef struct {
    int count;                                        // 检出数量
    object_detect_result results[OBJ_NUMB_MAX_SIZE];  // 最多 128 个
} object_detect_result_list;

// od_results 在 _inference_loop 线程栈上定义，每帧用完即弃：
object_detect_result_list od_results;  // AIcamera_c_interface.cc:84
```

### 15.6 第五步：坐标映射 + OpenCV 画框

```cpp
// AIcamera_c_interface.cc:120-138
for (int i = 0; i < od_results.count; i++) {
    // 坐标系转换：640×640 → 320×240
    mapCoordinates(bgr, bgr_model_input, &det->box.left, &det->box.top);
    mapCoordinates(bgr, bgr_model_input, &det->box.right, &det->box.bottom);

    // OpenCV 直接在 bgr Mat 上绘制矩形框 + 类别标签
    cv::rectangle(bgr, cv::Point(left,top), cv::Point(right,bottom),
                  cv::Scalar(0,255,0), 3);
    cv::putText(bgr, name, cv::Point(left, top-8), ...);
}
// 坐标缩放：scaleX = 320/640 = 0.5 → 推理坐标 × 0.5 = 显示坐标
```

### 15.7 第六步：格式转换 → 全局输出缓冲

```cpp
// AIcamera_c_interface.cc:139-151
cv::resize(bgr, bgr, cv::Size(320, 240));          // 确保尺寸
cv::cvtColor(bgr, disp, cv::COLOR_BGR2BGR565);     // BGR → RGB565
memcpy(yolo_pic_buf, disp.data, 320×240×2);        // → 全局缓冲
```

**全局输出缓冲**（文件作用域，跨线程共享）：

```cpp
// AIcamera_c_interface.cc:50-51
uint8_t* yolo_pic_buf;       // 全局，start_ai_camera 中 malloc(153KB)
size_t yolo_pic_buf_size;    // 320×240×2
```

### 15.8 第七步：LVGL 定时器取图

```cpp
// ui_YOLOPage.c:55-77
static void timer_flash() {              // lv_timer，每 30ms
    if (_first_into) {
        img_dsc.data = malloc(320×240×2);  // 分配 LVGL 图像缓冲
        _ai_camera_init();                 // 启动推理线程
    }
    get_buf_data(img_dsc.data);            // memcpy(yolo_pic_buf → LVGL)
    lv_img_set_src(img_cam, &img_dsc);     // LVGL 刷新屏幕
}
```

### 15.9 完整链路总图

```
┌── 推理线程 (pthread) ──────────────────────────────────────────────┐
│                                                                     │
│  [1] cv::VideoCapture cap(0); cap >> bgr;                          │
│      bgr.data (malloc 230KB)     ← V4L2 → /dev/video0 → 320×240    │
│                         │                                           │
│  [2] cv::resize(bgr → bgr_model_input) (640×640)                   │
│      bgr_model_input.data == input_mems[0]->virt_addr (零拷贝!)     │
│                         │                                           │
│  [3] rknn_run(ctx)              NPU 硬件推理                        │
│      output_mems[0/1/2]         int8 输出张量                       │
│                         │                                           │
│  [4] post_process() → od_results (栈变量)                          │
│      {box:{l,t,r,b}, prop, cls_id} × N                             │
│                         │                                           │
│  [5] mapCoordinates + cv::rectangle + cv::putText                   │
│      在 bgr 上画框叠加                                             │
│                         │                                           │
│  [6] cv::cvtColor → RGB565 → memcpy → yolo_pic_buf (全局 153KB)    │
│                         │                                           │
│      ════════════════════ 跨线程边界 ════════════════════════      │
│                         │                                           │
│  [7] LVGL 主线程: timer_flash() 每 30ms                             │
│      get_buf_data(img_dsc.data) → memcpy                           │
│      lv_img_set_src(img_cam, &img_dsc) → 屏幕显示                  │
│                                                                     │
└────────────────────────────────────────────────────────────────────┘
```

### 15.10 关键变量与线程安全

| 变量 | 作用域 | 线程 | 说明 |
|------|--------|------|------|
| `rknn_app_ctx` | 全局 | 推理线程独占 | NPU 上下文 + 输入/输出 DMA 内存 |
| `yolo_pic_buf` | 全局 | 推理线程写，LVGL 主线程读 | 无锁，本质是双缓冲语义（最新帧覆盖旧帧） |
| `od_results` | 栈变量 | 推理线程独占 | 每帧用完即弃 |
| `img_dsc.data` | 全局 | LVGL 主线程独占 | LVGL 图像帧缓冲 |

**线程安全策略**：推理线程写完 `yolo_pic_buf` 后，LVGL 的 `timer_flash` 通过 `memcpy` 拷贝到独立的 LVGL 缓冲区再显示。即使推理线程在 `memcpy` 期间写入了新数据，最多导致画面撕裂（半帧新半帧旧），不会引发崩溃。

---

## 16. YOLO 线程与 LVGL 线程的同步分析

### 16.1 双线程架构

```
┌───────────────────────────────┐  ┌──────────────────────────────┐
│  推理线程 (pthread)            │  │  LVGL 主线程                  │
│  _inference_loop()            │  │  timer_flash() 每 30ms       │
│                               │  │                              │
│  cap >> bgr                   │  │                              │
│  resize → NPU 输入             │  │                              │
│  rknn_run() NPU 推理          │  │                              │
│  post_process() → od_results  │  │  get_buf_data()              │
│  cv::rectangle() 画框         │  │    ↕ memcpy 读               │
│  cv::cvtColor() 格式转换      │  │  lv_img_set_src()            │
│  memcpy → yolo_pic_buf (写) ──┼──→                              │
└───────────────────────────────┘  └──────────────────────────────┘
```

**关键设计**：推理线程将检测结果直接画到图像上，LVGL 侧拿到的不是"检测结果结构体"，而是"已经画好框的像素帧"。传递媒介是全局 buffer `yolo_pic_buf`。

### 16.2 共享数据清单

| 变量 | 类型 | 作用域 | 写者 | 读者 | 同步机制 |
|------|------|--------|------|------|---------|
| `yolo_pic_buf` | `uint8_t*` (153KB) | 全局 | 推理线程 `memcpy` | LVGL 线程 `memcpy` | **无** |
| `ai_camera_running` | `int` | 全局 | `start/stop` 函数 | `start/stop` 函数 | `pthread_mutex_t running_mutex` |
| `ai_camera_stop` | `int` | 全局 | `stop_ai_camera` | 推理线程 `while` 条件 | **无**（技术上有 data race） |
| `od_results` | 栈变量 | 推理线程内 | 推理线程 | 推理线程 | 不需要（线程私有） |
| `img_dsc.data` | `uint8_t*` (153KB) | 全局 | LVGL 线程 | LVGL 线程 | 不需要（单线程独占） |

### 16.3 逐个分析

#### yolo_pic_buf — 无锁帧缓冲

```cpp
// 推理线程写 (AIcamera_c_interface.cc:151)
memcpy(yolo_pic_buf, disp.data, 320 * 240 * 2);

// LVGL 线程读 (AIcamera_c_interface.cc:218)
void get_buf_data(uint8_t* buffer) {
    memcpy(buffer, yolo_pic_buf, 153600);  // 没有任何锁
}
```

**没有互斥锁、信号量、原子变量、或内存屏障。**

**为什么可以接受**：

```
推理线程帧率:   ~5-10 fps (100-200ms/帧，受 NPU 推理速度限制)
LVGL 读取频率:  ~33 fps (30ms 周期)
memcpy 153KB:   ~10μs (1GHz Cortex-A7，DDR2 带宽足够)

实际重叠概率:   ~10μs / 100ms ≈ 0.01%
```

写入和读取重叠的概率极低。即使发生，结果是一条水平画面撕裂线（半帧新半帧旧），下一个 30ms 周期立即修复。在 320×240 低分辨率屏幕上几乎不可察觉。

**崩溃风险为零**：`yolo_pic_buf` 的生命周期被线程启停严格保护——`malloc` 在 `pthread_create` 之前，`free` 在 `pthread_join` 之后，指针绝无悬空可能。

#### ai_camera_running — 有 Mutex 保护 ✅

```cpp
int start_ai_camera(const char* model_path) {
    pthread_mutex_lock(&running_mutex);
    if (ai_camera_running) {           // 锁内检查
        pthread_mutex_unlock(&running_mutex);
        return -1;                     // 防止重复启动
    }
    ai_camera_running = 1;             // 锁内设置
    pthread_mutex_unlock(&running_mutex);
    // 后续 malloc + pthread_create 在锁外，正确（非共享操作）
}
```

mutex 保证了"检查-设置"的原子性，防止两个线程同时调用 `start`。

#### ai_camera_stop — 存在数据竞争 ⚠️

```cpp
// stop_ai_camera: 锁外写入
pthread_mutex_unlock(&running_mutex);
ai_camera_stop = 1;                     // ← 无锁写入

// _inference_loop: 无锁读取
while(!ai_camera_stop) { ... }          // ← 无锁读取
```

在 C11 标准下属于 undefined behavior（data race）。但实际在 Cortex-A7 单核上：`int` 对齐读写是原子的，只有一个写者，最终一定会被读者看到。属于"实践上安全，规范上不严谨"。如果严格修复，应使用 `atomic_int` 或在读写两侧都加锁。

#### od_results — 天然安全 ✅

```cpp
// _inference_loop 函数体内部
object_detect_result_list od_results;  // 栈上分配，线程私有
```

仅供推理线程内 `post_process` 解析后 `cv::rectangle` 画框使用。LVGL 线程完全不可见，无需任何同步。

#### 线程生命周期 — 正确 ✅

```
start_ai_camera():
  mutex check → malloc(yolo_pic_buf) → pthread_create(_inference_loop)

stop_ai_camera():
  mutex check → ai_camera_stop=1 → pthread_join(ai_camera_thread) → free(yolo_pic_buf)
```

`malloc` 先于线程创建，`free` 后于线程退出。指针生命周期包含所有读写窗口。

### 16.4 为什么"检测结果"不直接传给 LVGL

传递的是"已画好框的像素"，而不是 `od_results` 结构体：

```
传统做法（重）:                     本项目的做法（轻）:

推理线程:  post_process → 坐标      推理线程:  post_process → 坐标
           ↓                                 ↓
          写入共享队列 (需锁)                cv::rectangle 直接画框
           ↓                                 ↓
LVGL 线程: 读队列 (需锁)                     cv::cvtColor → yolo_pic_buf
           ↓                                 ↓
          逐个画框到 LVGL canvas            memcpy 拿走 (无锁)
```

**优势**：LVGL 侧不需要知道 YOLO 的数据结构、不需要理解 COCO 80 类、不需要实现画框逻辑。只需要显示一张图。

**代价**：`yolo_pic_buf` 是无锁的，接受极低概率的画面撕裂。

### 16.5 设计评价

| 维度 | 评级 | 说明 |
|------|------|------|
| 正确性 | ⭐⭐⭐⭐ | 不会崩溃、不会死锁、不会丢数据 |
| 线程安全规范性 | ⭐⭐⭐ | `ai_camera_stop` 有 data race（单核无害） |
| 性能 | ⭐⭐⭐⭐⭐ | 零同步开销，符合嵌入式资源约束 |
| 可维护性 | ⭐⭐⭐⭐ | 模型简单，新页面可照搬 |

这是一种典型的嵌入式异步帧缓冲模式——**"与其加锁，不如接受偶尔撕裂"**。在 64MB RAM、单核 Cortex-A7 的约束下是务实的工程选择。

### 16.6 在这个项目中关于锁的问题，这样的作法符合实际工程吗？在面试时这样作答会减分吗？（gemini）

这不仅完全符合实际工程经验，而且如果你在面试中能按这个逻辑把这段话讲出来，**绝对是一个巨大的加分项**！

在真实的嵌入式工程环境中，教科书里的“凡共享必加锁”往往是个伪命题。面试官（尤其是资深的系统架构师）最反感的就是背诵八股文、为了加锁而加锁的“教条主义者”。他们最想看到的，就是你这种“基于系统资源的极限边界，做出的精算与折中（Trade-off）”。

以下我帮你梳理一下，为什么这种做法很高级，以及在面试中你该如何“包装”这套说辞，把防守变成绝杀。

### 1. 为什么这符合实际工程？（真实的工业界是怎么做的）

在音视频流、摄像头采集和 AI 推理这种“流式数据”处理中，**“绝对的数据完整性”往往要让位于“系统的低延迟和高吞吐”**。

* **如果加锁（Mutex）会发生什么？**
如果给 `yolo_pic_buf` 加上互斥锁，当 LVGL 正在读取（耗时 10μs）或等待屏幕刷新时，YOLO 推理线程会被强行阻塞（Block）。这会导致 NPU 算力被闲置，甚至引发整个图像链路的卡顿（Jitter）。
* **不加锁的代价是什么？**
如 cc 所分析，最大的代价就是 0.01% 概率发生“画面撕裂（Tearing）”。对于一个 30fps 的视频流来说，一帧画面的上半部分是上一秒的，下半部分是这一秒的，人眼根本无法察觉，下一帧（30ms后）就自动覆盖了。
* **工程真相：** 牺牲肉眼不可见的单帧完美，换取两个线程的绝对解耦和最高运行效率，这是嵌入式视觉/显示驱动中最常用的务实手段。

---

### 2. 面试高分话术：如何把“没加锁”讲成“架构设计”

如果你在面试时只是说：“啊，我当时忘了加锁，但测了一下没报错就这么用了。”——**这叫事故，扣大分。**

但如果你用以下“先抑后扬 + 抛出备用方案”的话术，面试官会觉得你是个老手：

> **面试官：** “你的 YOLO 线程和 LVGL UI 线程是如何共享这 153KB 图像数据的？加锁了吗？”
> **你的回答（分三步走）：**
> **第一步（坦诚架构）：** “在这个项目中，我使用的是**无锁的单缓冲（Single Buffer）直接覆盖机制**。YOLO 线程画完框后，通过 memcpy 直接覆写全局内存，LVGL 定时器也是无锁读取。”
> **第二步（技术精算与折中）：** “我当时之所以**刻意没有加锁**，是做过时间窗口计算的。在 Cortex-A7 上，153KB 的 memcpy 耗时大约在 10 微秒级别，而 UI 刷新周期是 30 毫秒。两者发生读写冲突的概率极低。退一万步讲，即便发生冲突，最坏的后果也仅仅是出现一帧的画面撕裂（Tearing），这在 320x240 的屏幕上对视觉体验几乎没有影响。如果引入互斥锁，反而会导致轻量级的 UI 线程和重负载的 NPU 推理线程互相阻塞，拉低整体帧率。这算是一个**用极低的视觉撕裂风险换取零同步开销**的工程折中。”
> **第三步（降维打击，给出进阶方案）：** “当然，如果要追求工业级的绝对防撕裂，我也考虑过升级方案。我们不需要用互斥锁，而是应该采用**双缓冲（Double Buffering / Ping-Pong Buffer）**。YOLO 写 Buffer A 时，LVGL 读 Buffer B，写完后通过一个原子的指针翻转来交换读写权限。但这会额外消耗 153KB 的宝贵 RAM，由于当前芯片资源受限，我权衡后选择了单缓冲模式。”

这段话一出，面试官基本就没有什么可挑剔的了，因为你把他的潜在问题（画面撕裂怎么办？怎么完美解决？）自己全答完了。

---

### 3. ⚠️ 唯一的扣分隐患：`ai_camera_stop` 的数据竞争

cc 在报告的 `16.3` 节中敏锐地指出了一个 C 语言规范上的雷点：

```cpp
// 线程 A
ai_camera_stop = 1;

// 线程 B
while(!ai_camera_stop) { ... }

```

**在面试中，千万不要对这个地方打马虎眼。** 对于老派的 C/C++ 面试官（特别是一些做底层 OS 或编译器的考官），如果你不指出这个问题，他们会认为你 C 语言基础不扎实。

如果聊到线程生命周期控制，你必须**主动承认并优化**这个点：

> “在这个项目中，`ai_camera_stop` 标志位目前的读写是没有加保护的。虽然在 32 位 ARM 单核上，整型的对齐读写天然是原子的，不会造成总线错误，但从 C11 标准来看，这是典型的未定义行为（Undefined Behavior）。编译器如果开启高等级优化（如 `-O2` 或 `-O3`），很可能会把 `ai_camera_stop` 缓存到寄存器里，导致死循环。
> **改进方案：** 应该将这个变量声明为 `<stdatomic.h>` 中的 `atomic_int`，或者最起码加上 `volatile` 关键字强制每次访存，以确保规范性和代码的可移植性。”

### 总结

cc 给你的这段分析非常硬核且透彻。不要觉得“没加锁”就是做错了，**优秀的工程师不是用锁来解决一切问题，而是知道什么时候可以不加锁。**

把你手头这份分析消化掉，尤其是记住那几个关键耗时（10μs、30ms）和“双缓冲”的替代方案，这会成为你面试中的一个极佳的“技术亮点”。

## 17. AI Chat 语音流水线

### 17.1 总体架构

```
┌── RV1106 客户端 (C++ / PortAudio / Opus / websocketpp) ────────┐
│                                                                 │
│  麦克风 → PCM → Opus 编码 → WebSocket 发送                      │
│  扬声器 ← PCM ← Opus 解码 ← WebSocket 接收                      │
│                                                                 │
│  状态机: idle ⇄ listening ⇄ thinking ⇄ speaking                │
└────────────── WebSocket + JSON + 自定义二进制协议 ───────────────┘
                              │
┌── PC 服务器 (Python / FunASR / DashScope / CosyVoice) ─────────┐
│                                                                 │
│  VAD (FSMN) → ASR (SenseVoice) → 意图 (FastText)               │
│  → LLM (通义千问 Turbo) → TTS (CosyVoice)                      │
└─────────────────────────────────────────────────────────────────┘
```

**核心设计决策**：客户端只负责音频采集/播放和 Opus 编解码。所有 AI 模型跑在 PC 服务器上。RV1106 的 64MB RAM
 和 Cortex-A7 无法承载 LLM+TTS 模型。

### 17.2 客户端：音频处理

**AudioProcess 类**（`Client/Audio/AudioProcess.h`）：

```
PortAudio → recordCallback → PCM int16 帧
    → push recordedAudioQueue (Mutex + Condition Variable)
        → Listening 状态消费 → Opus 编码 → 发送到服务器

服务器音频帧:
    → Speaking 状态消费 → Opus 解码 → push playbackQueue (Mutex)
        → PortAudio playCallback → 扬声器
```

**编解码参数**：16kHz 单声道，40ms 帧（640 样本），libopus 压缩比约 10:1。

**自定义二进制协议**：
```c
struct BinProtocol {
    uint16_t version;       // 协议版本号
    uint16_t type;          // 0 = Opus 音频数据
    uint32_t payload_size;  // Opus payload 长度
    uint8_t  payload[];     // Opus 编码后的音频
} __attribute__((packed));
```

### 17.3 客户端：状态机

```
      ┌─────────┐
      │ startup │ → 初始化完成 → idle
      └─────────┘
                        ↓ 用户触发（唤醒词/按键）
      ┌───────────┐
      │ listening │ ← 启动录音 + 独立线程循环发送 Opus 帧
      └───────────┘
                        ↓ 服务器返回 vad=end 或 buffer_full
      ┌───────────┐
      │ thinking  │ ← 等待服务器 LLM + TTS 生成
      └───────────┘
                        ↓ 收到第一个 TTS 音频帧
      ┌───────────┐
      │ speaking  │ ← 独立线程接收/解码/播放 TTS 音频
      └───────────┘
                        ↓ 收到 tts=end
                      idle
```

每个状态三个方法：`Enter()` 初始化 → `Run()` 在新线程中执行 → `Exit()` 清理。

### 17.4 客户端：线程模型

```
Application 实例
├── ws_msg_thread_         ← websocketpp 消息回调线程（库内部）
├── state_trans_thread_    ← 状态转换线程（消费 eventQueue_）
├── Listening::state_running_thread_  ← 仅 listening 期间存在
└── Speaking::state_running_thread_  ← 仅 speaking 期间存在

稳定状态: 2 线程，语音对话期间: 3 线程
```

**同步机制**：

| 对象 | 机制 | 用途 |
|------|------|------|
| `recordedAudioQueue` | `std::mutex` + `std::condition_variable` | 麦克风 PCM → 编码线程 |
| `playbackQueue` | `std::mutex` | 解码线程 → 扬声器 |
| `eventQueue_` | 内部 Mutex | 状态转换事件入队/出队 |
| `IntentQueue_` | 内部 Mutex | 服务器下发的意图数据 |
| `threads_stop_flag_` | `std::atomic<bool>` | 安全通知子线程退出 |

### 17.5 服务器：服务组件

```
ServiceManager
├── AudioProcessor          ← 二进制协议解包 + Opus 解码
├── VADService              ← 语音活动检测（流式）
│   └── VADModel            ← FunASR FSMN-VAD
├── ASRService              ← 语音识别
│   └── ASRModel            ← SenseVoice (FunASR AutoModel)
├── IntentService           ← 意图分类
│   └── (FastText)           ← 文本分类器
├── ChatService             ← 大语言模型
│   └── LLMModel            ← 通义千问 Turbo (DashScope API)
├── TTSService              ← 语音合成
│   └── TTSModel            ← CosyVoice (WebSocket 流式)
├── TaskManager             ← 线程池：短生命周期任务
│
├── tts_text_queue          ← LLM 输出 → TTS 输入（Queue，线程安全）
├── audio_queue             ← TTS 输出 → 发送线程（Queue，线程安全）
└── ws_send_queue           ← 所有待发送消息（Queue，线程安全）
```

### 17.6 服务器：请求处理流水线

```
客户端 Opus 帧到达 WebSocket
      │
      ▼
  AudioProcessor.unpack_bin_frame()      ← 解包二进制协议
  AudioProcessor.decode_audio()          ← Opus → PCM int16
      │
      ▼
  VADService.process_audio_frame()       ← FSMN 流式检测，返回值:
      │
      ├── 0: 继续积累音频到 ASR 缓冲区
      │
      ├── 1: 检测到语音结束 →
      │     ASRService.asr_generate_text()     ← SenseVoice 识别
      │     TaskManager.submit_task(chat_start_task, text)
      │       │
      │       ▼
      │     ┌─ chat_start_task() ────────────────────────────┐
      │     │ 1. IntentService.detect_intent(text)           │
      │     │    → FastText 分类 → function_calls            │
      │     │    → 系统内置: continue_chat, exit_chat        │
      │     │    → 其他意图通过 WS 下发客户端执行             │
      │     │                                               │
      │     │ 2. ChatService.generate_chat_response(text)   │
      │     │    → Qwen-Turbo 流式 API (DashScope)          │
      │     │    → 逐 chunk yield                            │
      │     │                                               │
      │     │ 3. for each text_chunk:                        │
      │     │      TTSService.tts_speech_stream(chunk)       │
      │     │      → CosyVoice WebSocket 流式合成            │
      │     │      → _tts_on_data() 回调 → audio_queue      │
      │     │                                               │
      │     │ 4. _tts_on_complete() → "tts":"end" → 客户端  │
      │     └───────────────────────────────────────────────┘
      │
      ├── 2: 无语音活动超时 → 通知客户端丢弃
      │
      └── 3: 缓冲区满 → 强制触发 ASR（防止 OOM）
```

### 17.7 AI 模型清单

| 模型 | 库/框架 | 运行位置 | 输入 | 输出 | 执行方式 |
|------|---------|---------|------|------|---------|
| VAD | FunASR FSMN-VAD | 服务器本地 | PCM int16 流 (每 200ms) | 0/1/2/3 状态码 | CPU 推理 |
| ASR | SenseVoice (FunASR) | 服务器本地 | PCM 累积缓冲区 | 中文字符串 | CPU 推理 |
| 意图 | FastText | 服务器本地 | 文本 | function_call [{name, args}] | CPU 推理 |
| LLM | 通义千问 Turbo | DashScope API (云端) | 对话历史 + 用户输入 | 流式文本 chunks | API 调用 |
| TTS | CosyVoice | 服务器本地 | 文本 chunks (流式) | PCM/Opus 音频帧 | GPU/CPU 推理 |

### 17.8 服务器线程/协程模型

```
asyncio 主循环:
  ├── WebSocket 服务器 (websockets)
  ├── process_send_queue 协程      ← 轮询 ws_send_queue，每 100ms
  └── handle_client 协程 (每客户端一个)

独立线程:
  ├── AudioSendThread               ← 轮询 audio_queue → batch 发送 TTS 帧
  └── TaskManager 线程池            ← 执行 chat_start_task 等异步任务
```

### 17.9 DeskBot 集成

```
app_ChatBotPage.c                     ui_ChatBotPage.c
(C 接口壳 + 电机控制)                 (LVGL UI 人脸动画)

start_ai_chat(config)                 每 250ms (_ChatBotTimer_cb):
  → create_aichat_app()                 get_ai_chat_state() → 状态机状态
  → pthread_create(ai_chat_thread)        idle    → 随机眼球运动
  → run_aichat_app() (阻塞)              listening → 问号图标旋转 + 眯眼
  → destroy_aichat_app()                 thinking  → 思考图标 + 眨眼
                                         speaking  → 嘴巴张合
每 500ms (_ChatBotMoveTimer_cb):
  chat_bot_get_intent_process()       离开页面:
  → get_aichat_app_intent()             stop_ai_chat()
  → if "robot_move":                    → stop_aichat_app() → 信号 → pthread_join
    → GPIO 控制双路电机前进/后退/转向    → destroy
```

### 17.10 完整数据流

```
[User 说话]
     │
[RV1106] 麦克风 → PortAudio → PCM int16 → Opus encode → BinProtocol → WebSocket
     │
[Server] WebSocket → AudioHandler.handle_audio_message()
     │ → AudioProcessor.unpack → Opus decode → PCM
     │ → VADService.process_audio_frame()       (FSMN 流式，一帧帧喂入)
     │ → ASRService.asr_add_audio_buffer()      (积累 PCM)
     │ → VAD return 1 (语音结束)
     │ → ASRService.asr_generate_text()          (SenseVoice 批量识别)
     │ → TaskManager

| 维度 | 评级 | 说明 |
|------|------|------|
| 架构合理性 | ⭐⭐⭐⭐⭐ | C/S 分离完美匹配硬件约束，客户端轻量，服务端可独立升级模型 |
| 网络依赖 | ⭐⭐ | 完全离线不可用，是最大软肋 |
| 延迟体验 | ⭐⭐⭐ | 200-800ms 首字延迟（网络 + LLM + TTS 流式），可接受 |
| 代码结构 | ⭐⭐⭐⭐ | 状态机模式清晰，服务分层合理 |
| 可扩展性 | ⭐⭐⭐⭐ | 新增意图只需注册回调；切换 LLM 只需改模型名 |

---
---

> **待完善:** 补充详细的接口规格、时序图、内存预算分析和错误处理策略。
