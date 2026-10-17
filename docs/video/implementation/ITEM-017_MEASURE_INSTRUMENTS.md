# 预算测量装置与三类源分层留证 — 实现交付（VideoSystemImplementation, ITEM-017）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-017「预算测量装置与三类源分层留证」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §5（时钟/单位）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §8/§12、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-INSTR-01..02、三类源分层）
- 基线 commit: `ead8f74`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/metrics.h` | 新增 | `7ad0b34868aca293` | 延迟直方图/吞吐/存储/恢复/同步样本 + 三类源记录 + 归档 JSON（纯逻辑） |
| `app/src/core/src/metrics.cpp` | 新增 | `1cf1483b6ba59b5a` | `LatencyHistogram`（nearest-rank 分位）、`measurement_json`、`measurements_archive_json` |
| `app/src/media/tools/measure_probe.cpp` | 新增 | `5bc2170b0c78cae4` | 运行入口 `railhmi-measure`（配置驱动 + `--selftest`） |
| `app/tools/board_measure_check.py` | 新增 | `29a575d851df2115` | 板端执行入口 |
| `app/src/core/CMakeLists.txt` | 修改 | `3875141af43ba7b1` | 编译 `metrics.cpp` |
| `app/src/media/CMakeLists.txt` | 修改 | `f1bd8d6eb4e47a31` | 注册 `railhmi-measure` 探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `0f848c4d9c3596b9` / `db99fe08b242ad67` | 注册 `metrics` 测试组 |
| `app/tests/core/test_metrics.cpp` | 新增 | `7a363a87072afdfe` | 直方图/分位/源类/状态/JSON 测试 |

## 2. 契约与行为

1. **吞吐**：`ThroughputSample{source_fps, display_fps, cpu_pct, rss_kb, npu_pct, temp_c, npu_note}`；CPU 由 `/proc/self/stat` 差分、RSS 由 `/proc/self/status`、温度由 thermal zone、NPU 由 rknpu load（不可用时 `npu_pct=-1` + 显式 `npu_note`，不伪造）。
2. **端到端延迟分位**：`LatencyHistogram`（nearest-rank p50/p90/p99 + min/max/mean/n），四段：`到达->解码完成`、`解码完成->转换完成`、`转换完成->呈现`、`端到端`（微秒）。
3. **存储**：`StorageSample{measured_bitrate_bps, clip_bytes, free_bytes, total_bytes}`；压缩包直通封装为真实片段实测字节，bitrate=字节×8/窗口；free/total 由 `statvfs`。
4. **断流恢复耗时**：`RecoverySample{attempts, last_recovery_ms}`；真实 `CameraWorker` cut → `RecoveryController` 重建并以真帧确认，测恢复耗时。
5. **同步偏差**：`SyncSample`；复用 `core::compute_replay_sync`（各路实际播放头 UTC 求 max−min）。
6. **三类源分层留证**：`SourceClass{Rtsp, File, Simulation}` 各自一条 `MeasurementRecord`；`MeasurementStatus{Recorded, ToBeExecuted, NotApplicable}`。缺授权/未接设备如实记 `to_be_executed` + reason，**不当作通过**；不互相替代。
7. **可归档**：`measurement_json`（单条）+ `measurements_archive_json`（`schema=railhmi.measure.v1`，含三类状态）。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-measure --selftest
build-media/tools/railhmi-measure --config config --duration 6 \
    --outdir core-evidence/measure --out measure.log --json measure.json
```

板端一键：
```bash
python app/tools/board_measure_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-017 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item017-r3`）

- `railhmi-core-tests`：**918 passed / 0 failed**（新增 `metrics` 组）。
- `railhmi-measure --selftest`：**5/5 PASS**。
- 三类源归档记录（`schema=railhmi.measure.v1`）：
  - **模拟输入（recorded）**：`source_fps=24.99 display_fps=24.83 cpu_pct=82.8 rss_kb=67452 temp_c=42.5 npu=unavailable:needs_privileged_rknpu_load`；端到端 `p50=10860us p90=18945us p99=21415us`（到达→解码→转换→呈现）；`clip_bytes=1668500 bitrate≈2.22Mbps free=33.1GB`；`sync available/in_sync max_deviation=6ms`；`recovery attempts=1 last=663ms`。
  - **文件回放（recorded）**：`source_fps=74.99`（快于实时）；端到端 `p50=81428us`；存储同片段 `1668500`。
  - **真实 RTSP（to_be_executed）**：`reason=CREDENTIAL_MISSING`（板端无相机凭据；未接设备/缺授权如实记为待执行，不当作通过）。
  - acceptance：`simulation_recorded=true file_recorded=true rtsp_to_be_executed=true three_source_classes=true latency_percentiles=true storage_measured=true recovery_measured=true sync_instrument=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-017/ITEM-017-ENV-A0-C-20261017-r1.md` 与同目录原始日志/JSON/实测片段。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 core/selftest `p50/p90/p99` 失败 | 分位用线性插值，p90/p99 期望与插值定义不符；后续 `p/100*n` 浮点舍入 `0.9*100=90.0000001` 令 ceil 多 1 | 改为 **nearest-rank**（`ceil(p*n/100 − ε)`）（r2/r3 通过） |
| r1 media 编译失败 `nv12_to_bgr_raw` 未声明 | 探针匿名命名空间内未限定 `media::` | 限定 `media::nv12_to_bgr_raw` / `media::BgrImage`（r3 通过） |
| r2 模拟记录先于 sync/recovery 赋值输出（日志与归档不一致） | emit 顺序 | 将模拟记录 emit 移到 sync/recovery 赋值之后（r3 一致） |

## 6. 边界与移交

- 本项交付**可复用测量装置与三类源分层归档**；真实 RTSP（ENV-D）需现场授权/接设备后另行执行，当前如实 `to_be_executed`，不冒充通过。
- 生产仪表盘/图表渲染归桌面 UI 职责；本项提供结构化测量数据与归档 JSON。
- 延迟含管线排队（端到端诚实口径）；NPU 负载需特权接口，不可用时显式标注不伪造。
- 未改 core/wire 契约、SQLite DDL、生产 UI。
