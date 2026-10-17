# 视频诊断、能力探测与结构化日志 — 实现交付（VideoSystemImplementation, ITEM-016）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-016「视频诊断、能力探测与结构化日志」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §8（IF-07 诊断）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §12、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-LOG-01..03）
- 基线 commit: `372200d`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/video_diagnostics.h` | 新增 | `38576f560ffa2f93` | 每路结构化指标 + 三态 + 整条脱敏（纯逻辑） |
| `app/src/core/src/video_diagnostics.cpp` | 新增 | `364cbe34008b6390` | `classify_presence`、`video_metric_json_line`、`redact_error` |
| `app/src/media/tools/diagnostics_probe.cpp` | 新增 | `481585db64bbabb9` | 运行入口 `railhmi-diagnostics`（配置驱动 + `--selftest`） |
| `app/tools/board_diagnostics_check.py` | 新增 | `c6d31cc5e837b719` | 板端执行入口 |
| `app/src/core/CMakeLists.txt` | 修改 | `57adb05c9c1d74d7` | 编译 `video_diagnostics.cpp` |
| `app/src/media/CMakeLists.txt` | 修改 | `65632d47ce7a231a` | 注册 `railhmi-diagnostics` 探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `de503dde2596f3cc` / `3caeda1a272a23c4` | 注册 `video_diagnostics` 测试组 |
| `app/tests/core/test_video_diagnostics.cpp` | 新增 | `b40f6f3aa8b0cd90` | 三态/脱敏/结构化字段测试 |

## 2. 契约与行为

1. **每路结构化指标**：`VideoChannelMetrics` 含 `state/fps/last_frame_mono_ms/last_frame_wall_ms/reconnect_count/quality_flags/display_dropped/ai_dropped/recording_overflow/gap_count/attempt_id/last_error`。
2. **结构化单行 JSON**：`video_metric_json_line` → `LogRecord{source="video", message="channel_metric", fields=...}` 经 `log_record_json`（ITEM-024）序列化，可按日滚动（`log_day_key`/`roll_health_logs_by_day`）。
3. **诊断三态**：`classify_presence` → 在线(Online)/未核验(Unverified)/缺口(Gap)。已知缺口（`gap_count>0` 或 `frozen`）优先暴露；有新鲜帧且 live/degraded → 在线；其余（未连接/未观测）→ 未核验。
4. **整条脱敏**：`redact_error` 移除 RTSP userinfo、`password/passwd/pwd/user-pw/token/secret/user-id/user/location/url` 值；若仍含 pipeline/bus 原文（`rtspsrc`/`GstPipeline`/`GST_MESSAGE`/`pipeline0`）则整条替换为 `<redacted>`。凭据/鉴权 URL/管线原文绝不进入日志/DB/工件/命令行。
5. **能力探测与版本独立读取**：复用 ITEM-001 `video_capability`（直接 `gst-inspect-1.0 <element>`，非 `--exists`），逐元素 status+Long-name、GStreamer/librga/librknnrt 版本独立可读；凭据只判存在性。
6. **实际 attempt_id**：`make_attempt_id(camera, generation)`（无 → `none`），与恢复控制器一致。
7. **按日滚动文件**：`<logdir>/video-YYYY-MM-DD.log` 写结构化 JSON 行；目录自动创建。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-diagnostics --selftest
build-media/tools/railhmi-diagnostics --config config --duration 2 \
    --logdir core-evidence/logs --out diag.log --json diag.json
```

板端一键：
```bash
python app/tools/board_diagnostics_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-016 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item016-r3`）

- `railhmi-core-tests`：**898 passed / 0 failed**（新增 `video_diagnostics` 组）。
- `railhmi-diagnostics --selftest`：**7/7 PASS**。
- 能力/版本（独立读取）：`gstreamer=1.20.3`、`librga=rga_api version 1.10.1_[0]`、`librknnrt=2.3.2 (…@2025-04-09T…)`；11 个必需元素全 present（含 Long-name，如 `mppvideodec = Rockchip's MPP video decoder`）。
- 三路结构化指标：
  - `CAM-01 presence=online (在线) state=live fps=25 …_wall_ms=1792252020023 attempt_id=CAM-01#g1 display_dropped=49 ai_dropped=48 recording_overflow=20 gap_count=0`。
  - `CAM-02 presence=gap (缺口) gap_count=1`。
  - `CAM-03 presence=unverified (未核验) state=offline`。
- 结构化按日文件 `video-2026-10-17.log`：3 行单行 JSON，字段齐全（presence/state/drops/overflow/gap/attempt_id/last_error）。
- 脱敏：`rtsp://admin:S3CRET@… password=… rtspsrc location=…` → `<redacted>`（无凭据/无 userinfo/无管线原文）。
- acceptance：`capability_versions=true elements_required_present=true tristate_online_unverified_gap=true redaction_clean=true daily_roll_ok=true daily_file_written=true structured_fields=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-016/ITEM-016-ENV-A0-C-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 media 编译失败 `'core' does not name a type` | 探针匿名命名空间内的自由函数未 `using namespace railhmi` | 匿名命名空间加 `using namespace railhmi;`（r2 通过） |
| r2 未产出按日文件、`last_frame_wall_ms=-1` | `ofstream` 不创建父目录；探针未校准 TimeBase 且 StreamStats 墙钟为 -1 | 写前 `FileStore::ensure_parent_dir`；校准 TimeBase 并由 mono→wall 补墙钟字段（r3 通过） |

## 6. 边界与移交

- 本项交付**视频诊断/能力探测/结构化日志**；生产 UI 诊断面板渲染归桌面 UI 职责。
- 日志落盘按日文件与 `roll_health_logs_by_day` 逻辑复用 ITEM-024（core health）；SQLite 侧写入归事件/持久化层（平台）。
- 真实相机 ENV-D 的断流/鉴权脱敏现场取证另档；本项以板端真实元素探测 + test 源指标 + 显式注入证明三态/脱敏/日志语义。
- 未改 core/wire 契约、SQLite DDL、生产 UI。
