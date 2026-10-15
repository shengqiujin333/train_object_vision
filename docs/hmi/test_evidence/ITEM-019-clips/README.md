# ITEM-019 证据 — 本地报警保护片段与缺口记录

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-019`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §10（保护片段前 30s/后 60s，可配置；先写临时文件再原子重命名）、§7 持久化（`recording_segments`/`recording_gaps`）；`docs/hmi/INTERACTION_DESIGN.md` v2（PLY-002/PLY-008：录像缺口显式标记不静默）；`docs/hmi/VISUAL_DESIGN.md` v2（录像不可用黄条）
- 源码: `app/src/core/include/railhmi/core/clip.h`、`app/src/core/src/clip.cpp`、`app/src/core/tools/clip_probe.cpp`、`app/tests/core/test_clip.cpp`、`app/src/core/CMakeLists.txt`、`app/tests/core/{CMakeLists.txt,main.cpp}`、`app/tools/board_build.py`
- 依赖复用: `railhmi-core::apply_retention`（ITEM-007，保护片段优先淘汰）、`add_gap`（缺口合并）、`railhmi-platform::FileStore::write_atomic`（临时文件→重命名）

## 1. 交付内容

1. **有界滚动缓冲** `RollingBuffer`：按时间戳有界入队（满丢最旧），`drop_before` 维护报警前窗口；`max_frames_per_channel` 缺省由 `fps*(pre+post)+裕量` 推导。
2. **保护片段固化** `ClipRecorder::on_alarm`：报警时生成 `[触发-pre, 触发+post]` 保护片段（时长 = (pre+post) 秒，配置化）；`finalize(now)` 在 post 窗口结束后标记为 **complete**，并从滚动缓冲统计片段帧数。
3. **原子写盘**：`clip_manifest_json` 产出清单，经 `FileStore::write_atomic`（先写 `<path>.tmp.<pid>` 再 `rename`）落盘；`railhmi-clip-probe` 在板端实际写入并回读校验。
4. **容量/期限保留**：`ClipRecorder::segments()` 把保护片段作为 `Critical` 片段，`apply_retention_now` 复用 `apply_retention` —— **普通片段先淘汰，保护片段仅在仍超容量时淘汰（最旧优先）**；淘汰后从记录中移除。
5. **缺口记录**：`on_recording_interrupt` 调用 `add_gap` 写入并合并重叠/相邻缺口（`RecordingGapRow` → 事件/时间轴“录像不可用”标记的数据来源）。
6. **`railhmi-clip-probe`**：无 Qt，串联上述流程并输出结构化证据与验收判定。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun` → `TESTRUN_EXIT=0` `CLIP_EXIT=0`。核心单测 **537 passed / 0 failed**（含 ITEM-019 新增片段/缺口/原子写盘断言）：
```
[PASS] buffer bounded to capacity / oldest dropped / drop_before trims / count_in window
[PASS] clip range = trigger-pre .. trigger+post / clip duration = (pre+post) seconds
[PASS] clip pending before post window / not finalized before t1 / finalized at t1 / clip complete after post window / frame_count from rolling buffer
[PASS] manifest has duration / manifest has camera / manifest complete
[PASS] overlapping gaps merged / merged range / gap reason kept / different camera separate gap
[PASS] normal evicted first, protected retained / protected clip id not evicted / protected evicted only when still over capacity
[PASS] ensure clip dir / atomic write clip / clip content round trip / clip file exists
```

`railhmi-clip-probe`（板端原文，r2 权威轮）：
```
type=config pre_s=30 post_s=60 buffer=2275 capacity_gb=12
type=buffer cam=CAM-01 frames=2251 capacity=2275
type=clip id=1 cam=CAM-01 trigger_ms=60000 t0_ms=30000 t1_ms=120000 duration_ms=90000 state=recording
type=finalize before=0 at=1
type=clip id=1 state=finalized complete=true frames=2001 duration_ms=90000
type=clip-store dir_ok=true wrote=true exists=true path=/tmp/railhmi_clips/clip-1.json
type=clip-manifest {"id":1,"cam":"CAM-01","level":"L1","trigger_ms":60000,"t0_ms":30000,"t1_ms":120000,"duration_ms":90000,"state":"finalized","complete":true,"frame_count":2001}
type=retention evicted=1 continuous=1 critical=0 protected_remaining=1
type=gap cam=CAM-01 count=1 t0=200000 t1=210000 reason=recording_interrupted
type=acceptance duration_ms=90000 clip_complete=true clip_file_exists=true protected_priority=true gap_merged=true result=PASS
```

判据核对（用户可观察点）：
- 触发报警后保护片段存在，**时长 90000ms = (30+60)s 与配置一致**，post 后 `complete=true` ✅
- 片段**先写临时文件再原子重命名**（`FileStore::write_atomic`，`wrote=true/exists=true`，回读一致）✅
- 容量/期限来自配置，**保护片段优先保留**（`evicted=1 continuous=1 critical=0 protected_remaining=1`）✅
- 录像中断写入缺口记录并合并（`gap count=1 t0=200000 t1=210000`），事件/时间轴可据此显示“录像不可用”而非静默 ✅

## 3. 边界

- **实际视频编码/封包**复用媒体层（GStreamer `mppvideodec`/编码链，属媒体/视频职责）；本项交付**保护片段的逻辑、时间范围、原子清单落盘、保留与缺口记录**。清单 JSON 记录了片段时间范围与帧数，视频文件接入点已就绪（`ProtectedClip::path`）。
- **段行入库**：`RecordingSegmentRow`/`RecordingGapRow` 的 SQLite 写入复用 ITEM-007 `SqliteRepository`；应用进程尚未打开数据库，本项在 core 层完成模型与文件落盘，数据库接线归后续集成。
- 探针写入 `/tmp/railhmi_clips/`（临时目录，不作为产品数据路径）；真实数据目录来自配置，属运行包项。

## 4. 证据文件

- `build-TestRun-...-ITEM019-r2.core-evidence_clip-probe-...-r2.log`（权威轮：片段时长/原子写盘/保护优先/缺口合并 PASS）
- `...-ITEM019-r2.core-evidence_core-tests-...-r2.log` / `.xml`（537 passed / 0 failed）
- `...-ITEM019-r2.transcript.json` / `.log`（构建 + 单测 + 探针 + 取回）
- r1 留痕（探针 config 行打印本地未推导容量 0，r2 已修正为 `rec.config()`；不影响判定）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-clip-probe --dir /tmp/railhmi_clips
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-019-clips --stamp <tag>
```
