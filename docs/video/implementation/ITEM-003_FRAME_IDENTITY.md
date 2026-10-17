# 帧身份与跨重连代际 — 实现交付（VideoSystemImplementation, ITEM-003）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-003「帧身份与跨重连代际」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（§4/§5 身份与时钟）、`docs/video/VIDEO_TEST_DESIGN.md`（G2 / TC-V-SEQ-01..02）
- 基线 commit: `fc1fce2`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/frame_identity.h` | 新增 | `a6b8e2214a1a1edd` | `FrameId`/`FrameMeta`/`FrameSequencer`/`FrameGate`/`make_session_id`；纯标准库 |
| `app/src/core/src/frame_identity.cpp` | 新增 | `3e5d0b22b92c01e4` | 身份/序号/代际生成与消费者接纳判定实现 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `c99bafbdd9f8adc7` | 配置加 `timebase`/`session_id`；`StreamStats` 加 session/generation/seq/wall/pts；新增 `last_frame_meta/generation/frames_emitted` |
| `app/src/media/src/camera_stream.cpp` | 修改 | `0dac4c2eea6b172f` | 每帧生成身份+时间；每次真实重建 `begin_generation()` |
| `app/src/media/tools/stream_probe.cpp` | 修改 | `985c1aa785a3ed72` | 时间基准校准、session/generation/seq/wall/pts 上屏、FrameGate 接纳计数、`--rebuild-after`、身份 selftest |
| `app/tests/core/test_frame_identity.cpp` | 新增 | `09874c1cf8d1e4ab` | core 单测：seq 递增、代际重建、接纳拒绝（旧 session/generation/重复/乱序） |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | — | 注册 frame_identity 测试组 |
| `app/src/core/CMakeLists.txt` | 修改 | — | 编入 `frame_identity.cpp` |
| `app/tools/board_frame_identity_check.py` | 新增 | `dfdd57505ce881d5` | 板端执行入口（CoreTests + 身份 selftest + `--rebuild-after` 运行） |

## 2. 契约与行为

1. **帧身份**：`FrameId{session_id, camera_id, generation, seq}`。`session_id` 由 `make_session_id()` 在应用启动生成、跨相机共享；`camera_id` 来自注册表；`generation` 只在**真正重建**时递增；`seq` 在同 generation 内从 1 起严格递增。
2. **时间字段**：`capture_mono_us` = `TimeBase::steady_now_us()`（接收时刻，单调微秒）；`capture_wall_ms` = 已校准 `TimeBase` 映射的 UTC epoch 毫秒，**未校准时为 -1**；`pts_ns` = `GstBuffer` PTS（`GST_CLOCK_TIME_IS_VALID` 判定），无效为 -1。单调接收时刻**不冒充** UTC；`pts_ns` 不当作主机时钟。
3. **重建语义**：`CameraStream::start()` 每次真正构建/重建管线时调用 `FrameSequencer::begin_generation()`（generation+1、seq 归零）；`stop()` 不改变 generation。首次构建 generation=1。
4. **消费者接纳判定**（`FrameGate`，供 AI/录像/显示复用）：
   - 首个 session 被采纳；不同 session → `stale_session`；
   - `generation` 小于已接纳 → `stale_generation`；大于则推进代际并重置 seq；
   - 同代内 `seq` 重复 → `duplicate_seq`；`seq` 倒退 → `out_of_order_seq`；否则 `accepted`。
5. **可观察**：`StreamStats` 暴露 `session_id/generation/last_seq/capture_wall_ms/pts_ns`；`stream_probe` 逐拍打印这些字段与接纳结果；`--rebuild-after <s>` 触发一次真实 stop/start 重建并演示旧代际帧被拒。
6. 与 `presentable` 的关系：非 LIVE 不得当实时（既有判定）+ 帧须属于当前 generation（`FrameGate::is_current`）。本项提供判定原语，具体消费接线在 ITEM-004/006/009/010。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests     # 含 frame_identity 组
app/build.sh MediaCheck
build-media/tools/railhmi-stream-probe --selftest                 # 含身份纯逻辑核对（23 项）
build-media/tools/railhmi-stream-probe --source test --duration 6 --rebuild-after 2 \
    --out id.log --json id.json
```

板端一键：
```bash
python app/tools/board_frame_identity_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-003 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `build.sh CoreTests` + `railhmi-core-tests`：**698 passed, 0 failed**（含 frame_identity 组「seq strictly increases / rebuild resets seq / stale generation rejected / duplicate & out-of-order rejected / same_stream」等）。
- `build.sh MediaCheck` + `railhmi-stream-probe --selftest`：**23/23 PASS**（含 `identity_seq_increasing / identity_rebuild_resets_seq / gate_accept / gate_duplicate_seq / gate_stale_generation / gate_out_of_order_seq / gate_stale_session`）。
- `--source test --rebuild-after 2`：`generation` 1→2（t≈2017ms 重建），重建后 `seq` 重置为 1 并递增；`type=identity_gate stale_old_generation=stale_generation old_gen=1 new_gen=2`；summary `generation=2 frames_emitted=151 gate_admitted=151 gate_rejected=0`；每拍 `wall_ms`（UTC epoch）与 `pts_ns`（生成源 PTS）齐备。
- 证据：`docs/video/test_evidence/ITEM-003/ITEM-003-ENV-A0-B-20261017-r1.md` 与同目录原始日志。

## 5. 边界与移交

- 本项交付**身份 + 时间 + 接纳判定**与最小运行时接入；**自动重建控制器**（退避到期真实重建、attempt 计数）属 ITEM-014，本项只在其调用 `start()` 时推进 generation。
- 帧租约/像素所有权（`FrameLease`）属 ITEM-004；本项只承载元数据（`FrameMeta`），不持有像素。
- `capture_wall_ms` 依赖应用启动校准 `TimeBase`；未校准时显式 -1，不退化冒充 UTC。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名。
