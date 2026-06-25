# Echo-Mate 二次开发进度

## 当前状态

- [x] 原开源 Echo-Mate / DeskBot 项目已在 RV1106 板子上完整复现全部功能。
- [x] 初始化二次开发路线、基线和进度文档。
- [ ] TODO：补采并归档 Baseline 性能数据。
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
