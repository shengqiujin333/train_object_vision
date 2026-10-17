# 时间基准与回放同步 — 实现交付（VideoSystemImplementation, ITEM-015）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-015「时间基准与回放同步」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §5.2（时钟/单位）、§8（IF-07 回放）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §5、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-PLY-02 + 时钟单调/UTC 判据）
- 基线 commit: `c288d04`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/replay_sync.h` | 新增 | `807e61095680ad8b` | 统一 UTC 回放同步 + seek 世代（纯逻辑） |
| `app/src/core/src/replay_sync.cpp` | 新增 | `c7ff863bf29916fe` | `compute_replay_sync`、`SeekController` |
| `app/src/core/include/railhmi/core/timebase.h` | 修改 | `6ce9bbecccdd65bc` | 新增 `try_to_wall_ms`/`can_write_utc`（未校准守卫） |
| `app/src/core/src/timebase.cpp` | 修改 | `66d961a2e793a90c` | 守卫实现 |
| `app/src/core/tools/timebase_sync_probe.cpp` | 新增 | `5890277c6d29227c` | 运行入口 `railhmi-timebase-sync`（配置驱动 + `--selftest`） |
| `app/tools/board_timebase_sync_check.py` | 新增 | `06804615e2621674` | 板端执行入口 |
| `app/src/core/CMakeLists.txt` | 修改 | `a8af494bacf2981e` | 编译 `replay_sync.cpp` + 注册探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `97a4433762f844a1` / `d0912c53dc88e34d` | 注册 `replay_sync` 测试组 |
| `app/tests/core/test_replay_sync.cpp` | 新增 | `f9a8332199bd09f3` | 校准/守卫/同步/seek 世代测试 |

## 2. 契约与行为

1. **启动校准**：`TimeBase::calibrate(wall_ms, mono_us)` 记录一次参照点；`to_wall_ms`（mono→UTC）、`to_mono_us`（UTC→mono）为**单次线性换算**（只做一次，无重复叠加）。
2. **未校准守卫**：`can_write_utc()`/`try_to_wall_ms()` 未校准时显式返回 false 且不写 `*wall_ms`——**未校准不得写 UTC 业务事件**；不静默用退化值冒充 UTC。
3. **统一 UTC 播放头同步**：`compute_replay_sync(locked_cameras, actual_utc_ms, calibrated, threshold)` 只消费**各路实际播放头（UTC）**（无固定 offsets 参数），求 max−min；超 `replay_sync_threshold_ms`（thresholds.yaml，默认 300）给出显式 `prompt`；缺任一路有效时钟 → `available=false`（`MISSING_CLOCK`）；时间基准未校准 → `available=false`（`UNCALIBRATED`）。
4. **seek 世代**：`SeekController::request_seek(target)` 递增 `generation`；`on_position(generation, actual)` 对**陈旧世代回调返回 false 并丢弃、不更新位置**；仅当实际位置进入目标容差才 `reached=true`，否则 `locating=true`（**定位未到达不提前显示已定位**）。
5. **实际播放头来源**：视频实际解码 position 由 media provider（ITEM-013 `LocalMediaProvider::play_segment`）提供；本项在其上建立统一 UTC 时间轴与同步/seek 语义。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
build-core/tools/railhmi-timebase-sync --selftest
build-core/tools/railhmi-timebase-sync --config config --out sync.log --json sync.json
```

板端一键：
```bash
python app/tools/board_timebase_sync_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-015 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item015-r2`）

- `railhmi-core-tests`：**879 passed / 0 failed**（新增 `replay_sync` 组）。
- `railhmi-timebase-sync --selftest`：**9/9 PASS**。
- 板端运行（真实墙钟/单调时钟）：
  - `timebase calibrated=true can_write_utc=true wall_ms=1792249580171 mono_us=378476823109 roundtrip=true`。
  - 未校准：`can_write_utc=false try_to_wall_ms=false utc_event_written=false`。
  - 同步（三路实际播放头，真实时钟读）：`available in_sync=true max_deviation_ms=10`。
  - 注入时间差：`in_sync=false max_deviation_ms=800 prompt=同步偏差 800 ms（阈值 300 ms）`。
  - 缺时钟：`available=false reason=MISSING_CLOCK missing=CAM-03`。
  - seek：`gen1=1 gen2=2 stale0_discarded=true stale1_discarded=true stale_pos_unchanged=true premature_located=false reached=true`。
  - acceptance：`timebase_calibrated=true conversion_roundtrip=true uncalibrated_no_utc=true sync_real_in_sync=true sync_prompt_over_threshold=true sync_missing_clock_unavailable=true seek_stale_discarded=true seek_not_premature=true seek_reached=true threshold_from_config=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-015/ITEM-015-ENV-A0-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 core 1 项失败 `stale does not update` | `request_seek` 曾把 `position_ms` 归零，陈旧回调“不更新”无从体现 | `request_seek` 保留最近一次已接受 position（仅置 `locating`），陈旧回调不改变它（r2 通过） |
| r1 selftest `roundtrip` 失败 | 探针期望值算错（ms→µs 需 ×1000；`(1001000-1000000)×1000` 是 1,000,000µs 而非 1000µs） | 改用与单测一致的 1.7e12 量级参照点，期望值修正（r2 通过） |

## 6. 边界与移交

- 本项交付**时间基准守卫与回放同步/seek 模型**；生产 UI 的 P3 时间轴/游标渲染归桌面 UI 职责（`core/replay.h` 已提供时间轴/四跳转模型）。
- 视频实际解码播放头由 ITEM-013 provider 提供并已独立验证；本项验证统一 UTC 时间轴与同步/seek 语义，不重复相机取流。
- 未改 core/wire 契约、SQLite DDL、生产 UI；`TimeBase` 既有 API 保留（新增守卫为增量）。
