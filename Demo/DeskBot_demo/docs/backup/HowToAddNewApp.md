# Echo-Mate DeskBot 新增 App 流程

## 概览

新增一个 App 需要修改 4 类文件（新建 2 个 + 修改 2 个），遵循页面管理器模式注册。

## ASCII 流程图

```
+==========================================================================+
|                   Echo-Mate 新增 App 完整流程                              |
+==========================================================================+
|                                                                          |
|  [1] 新建页面源文件                                                       |
|      Demo/DeskBot_demo/gui_app/pages/ui_XxxPage/                         |
|      |-- ui_XxxPage.h     (声明 init/deinit)                             |
|      |-- ui_XxxPage.c     (UI 布局 + 业务逻辑)                            |
|      |                                                                   |
|      +-- 核心要素:                                                        |
|          * init(): 创建 screen, 布局控件, 启动 timer                       |
|          * deinit(): 销毁 timer, 释放资源                                  |
|          * 手势回调: 右滑调用 lv_lib_pm_OpenPrePage() 返回                 |
|          * 跳转其他页: lv_lib_pm_OpenPage(&page_manager, NULL, "Xxx")     |
|                                                                          |
|          [推荐方式] SquareLine Studio 设计 UI, 导出 .c/.h                |
|          优点: 字体会自动包含 UI 中用到的所有汉字, 不会缺字                  |
|          手动写代码: 字体文件是旧的, 新文字可能显示为方框                    |
|                                                                          |
|  [2] 注册页面到页面管理器                                                  |
|      修改: gui_app/ui.c                                                  |
|      |-- #include "./pages/ui_XxxPage/ui_XxxPage.h"                      |
|      |-- #define _APP_NUMS  N -> N+1                                     |
|      |-- ui_apps[] 新增条目:                                              |
|          {                                                                |
|              .name = "XxxPage",                                           |
|              .init = ui_XxxPage_init,                                     |
|              .deinit = ui_XxxPage_deinit,                                 |
|              .page_obj = NULL                                             |
|          },                                                               |
|                                                                          |
|  [3] 添加首页入口按钮                                                      |
|      修改: gui_app/pages/ui_HomePage/ui_HomePage.c                        |
|      |-- 如果所有页面(每页 3x2=6 个按钮)已满:                              |
|      |   #define _APP_CONTAINER_MAX_PAGES  N -> N+1                      |
|      |-- 新增按钮代码:                                                     |
|          lv_obj_t * ui_XxxBtn = lv_button_create(ui_AppIconContainer);    |
|          lv_obj_set_width/ui_XxxBtn, 70);                                 |
|          lv_obj_set_height(ui_XxxBtn, 70);                                |
|          lv_obj_set_x(ui_XxxBtn, <计算X坐标>);  // 见下方坐标表            |
|          lv_obj_set_y(ui_XxxBtn, <计算Y坐标>);                             |
|          lv_obj_set_align(ui_XxxBtn, LV_ALIGN_LEFT_MID);                  |
|          // ... 样式设置 + 图标/文字 + 事件绑定 ...                         |
|          lv_obj_add_event_cb(ui_XxxBtn, ui_event_AppsBtn,                  |
|                              LV_EVENT_CLICKED, "XxxPage");                |
|                                                                          |
|          [图标方案, 按推荐程度]                                             |
|          A. SquareLine 导出 PNG -> images/ui_img_xxx64_png.c              |
|             + ui.h 添加 LV_IMG_DECLARE()                                   |
|             + lv_obj_set_style_bg_image_src(btn, &ui_img_xxx64_png, ...)  |
|          B. 使用 iconfont48 中的现成字符 ( 等)                        |
|             + lv_label_set_text(label, "")                          |
|             + lv_obj_set_style_text_font(label, &ui_font_iconfont48, ...) |
|          C. emoji 或自定义 Unicode -> 嵌入式 LVGL 不渲染, 不要用!          |
|                                                                          |
|  [4] 编译测试                                                              |
|      cd Demo/DeskBot_demo/build && cmake .. && make                       |
|      |-- cmake 自动 GLOB_RECURSE 扫描所有 .c, 无需手动改 CMakeLists.txt   |
|      |-- x86 仿真: cmake .. && make                                       |
|      |-- ARM 交叉编译: cmake .. -DTARGET_ARM=ON && make                    |
|                                                                          |
+==========================================================================+
|                                                                          |
|  首页按钮坐标参考 (屏幕 320x240, 按钮 70x70, 3列2行)                       |
|                                                                          |
|         col=0    col=1    col=2                                           |
|        (x=15)  (x=110)  (x=205)                                           |
|      +--------+--------+--------+                                         |
| 第N页 | row=0  | row=0  | row=0  |  y = -45                              |
|      |  15    | 110    | 205    |                                         |
|      +--------+--------+--------+                                         |
|      | row=1  | row=1  | row=1  |  y = +55                               |
|      |  15    | 110    | 205    |                                         |
|      +--------+--------+--------+                                         |
|                                                                          |
|  跨页坐标: 第N页 x = 基础值 + 320*(N-1)                                   |
|  例: 第3页 col=0 -> x = 15 + 320*2 = 655                                  |
|                                                                          |
+==========================================================================+
|                                                                          |
|  页面生命周期:                                                             |
|                                                                          |
|  HomePage 点击按钮                                                        |
|      |                                                                   |
|      v                                                                   |
|  lv_lib_pm_OpenPage("XxxPage")  -->  导航栈 push                         |
|      |                                                                   |
|      v                                                                   |
|  ui_XxxPage_init()                                                       |
|      |-- lv_obj_create(NULL)  创建 screen                                 |
|      |-- 布局 UI 控件                                                     |
|      |-- lv_timer_create()   启动定时器                                   |
|      |-- lv_scr_load_anim()  加载屏幕(带动画)                             |
|      |                                                                   |
|      v                                                                   |
|  [用户操作...]                                                            |
|      |                                                                   |
|      v                                                                   |
|  右滑手势 / 调用 OpenPrePage() --> 导航栈 pop                             |
|      |                                                                   |
|      v                                                                   |
|  ui_XxxPage_deinit()                                                     |
|      |-- lv_timer_delete()   停止定时器                                   |
|      |-- (LVGL 自动释放子对象, 无需手动 free)                              |
|                                                                          |
+==========================================================================+
|                                                                          |
|  常见踩坑:                                                                 |
|                                                                          |
|  [!] 中文字体缺字 -> 显示方框                                              |
|      原因: 手写代码用的旧字体, 没有新页面的汉字                              |
|      解决: 用 SquareLine Studio 设计页面, 导出时自动生成包含所需字的字体     |
|                                                                          |
|  [!] 控件超出屏幕                                                          |
|      屏幕 320x240, 控件 y 值基于 LV_ALIGN_CENTER (中心=120)               |
|      底部控件 y > 100 可能被截断                                           |
|                                                                          |
|  [!] LED/GPIO 冲突                                                        |
|      _maintimer_cb 每秒翻转 LED_BLUE, 新页面不要再控制 LED_BLUE           |
|                                                                          |
|  [!] 定时器未在 deinit 中释放                                              |
|      离开页面后 timer 仍在跑 -> 访问已释放的 UI 对象 -> 崩溃               |
|      务必在 deinit() 中 lv_timer_delete()                                 |
|                                                                          |
+==========================================================================+
```

## 文件变更清单

| # | 操作 | 文件路径 | 说明 |
|---|------|---------|------|
| 1 | 新建 | `gui_app/pages/ui_XxxPage/ui_XxxPage.h` | 声明 init/deinit |
| 2 | 新建 | `gui_app/pages/ui_XxxPage/ui_XxxPage.c` | UI 实现 + 业务逻辑 |
| 3 | 修改 | `gui_app/ui.c` | +include, +_APP_NUMS, +ui_apps[] 条目 |
| 4 | 修改 | `gui_app/pages/ui_HomePage/ui_HomePage.c` | +入口按钮 |
| 5* | 可选新建 | `gui_app/images/ui_img_xxx64_png.c` | 按钮图标 (从 SquareLine 导出) |
| 6* | 可选修改 | `gui_app/ui.h` | +LV_IMG_DECLARE (如果有新图片) |
