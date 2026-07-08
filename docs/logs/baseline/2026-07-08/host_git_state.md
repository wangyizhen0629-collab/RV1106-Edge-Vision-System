# Baseline host Git state

采集时间：2026-07-08 09:31:45 CST +0800

本文件只记录主机侧 Git/仓库状态，用于后续板端 Baseline 数据采集前的版本锚点。当前未包含 RV1106 板端命令输出、性能数据或功能验证日志。

## Commands and outputs

### `git status --short`

```text

```

说明：无输出，表示采集前工作区干净。

### `git branch --show-current`

```text
baseline/board-metrics
```

### `git rev-parse HEAD`

```text
198322396726c401c2cd2e1d69000ca395cc0974
```

### `git log --oneline --decorate --max-count=8`

```text
198322396 (HEAD -> baseline/board-metrics, origin/docs/embedded-roadmap, docs/embedded-roadmap) chore: add Codex embedded workflow skills
630dfa7e3 docs: organize DeskBot architecture documentation
0e0a8cf93 docs: initialize embedded development roadmap and baseline
d9d02410d (origin/main, main) Initial commit: Echo-Mate v1
```

### `git remote -v`

```text
origin	https://github.com/wangyizhen0629-collab/Echo-mate-v1.git (fetch)
origin	https://github.com/wangyizhen0629-collab/Echo-mate-v1.git (push)
```

## Notes

- 当前 Baseline 记录分支：`baseline/board-metrics`
- 当前记录锚点：`198322396726c401c2cd2e1d69000ca395cc0974`
- 后续板端日志建议继续放在本目录下，例如：
  - `board_system_info.txt`
  - `board_dmesg.txt`
  - `board_devices.txt`
  - `board_media_ctl.txt`
  - `deskbot_yolo_top_60s.txt`
  - `deskbot_yolo_proc_status.txt`
