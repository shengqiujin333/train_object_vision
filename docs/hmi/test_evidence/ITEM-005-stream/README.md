# ITEM-005 证据 — railhmi-core 流状态机与质量判定

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-005`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §6.2（流状态机）/§7.1；`docs/hmi/INTERACTION_DESIGN.md` v2 §7.1（六态与硬约束）；`docs/hmi/TEST_DESIGN.md` TC-CORE-02、TC-CORE-03
- 源码: `app/src/core/include/railhmi/core/stream_state.h`、`app/src/core/src/stream_state.cpp`
- 测试: `app/tests/core/test_stream.cpp`；运行器 `app/tools/board_build.py`（板端 CoreTests）

## 1. 交付内容

1. **六态状态机** `StreamStateMachine`（显式单调时钟 `now_ms`，可用模拟时钟驱动）：
   `CONNECTING / LIVE / FROZEN / OFFLINE / RECONNECTING / DEGRADED`。
2. **时间相关转换**：
   - `CONNECTING` 超 `first_frame_timeout_ms` 未出首帧 → 重连尝试；
   - `LIVE/DEGRADED` 超 `freeze_timeout_ms` 无新帧 → `FROZEN`；
   - `FROZEN` 再超时 → `RECONNECTING`；`RECONNECTING` 退避 `reconnect_backoff_ms` → 再次 `CONNECTING`；
   - 重连计数达 `reconnect_threshold` → `OFFLINE` 并发 **升级设备事件** `Escalated`；
   - 恢复出帧 → `LIVE/DEGRADED` 且重连计数清零（`Recovered`/`FirstFrame` 事件）。
3. **质量标志与判定**：`QualityFlags{too_dark, too_bright, occluded, blurred}` 与
   `classify_quality(FrameStats, QualityThresholds)`（按均值/标准差/拉普拉斯方差判定；无效统计不产生标志）；
   有质量标志的出帧 → `DEGRADED`（降级可用）。
4. **非 LIVE 禁止旧帧实时的判定接口**：`may_present_as_live(now_ms)`（仅 `LIVE/DEGRADED` 且帧新鲜时 true）；
   `is_streaming()`；调用方渲染前必须检查，false 时渲染状态占位 + 最近帧时间戳。
5. **事件缓冲**：`StreamEvent{camera_id, kind, t_ms, reconnect_count, detail}`，供事件/持久化层 drain。
6. 阈值 schema 扩展：`thresholds.yaml` 增加 `first_frame_timeout_ms`、`reconnect_backoff_ms`（配置层校验）。

## 2. 实测（板端本机，经登记 SSH 入口）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `./build-core/tests/railhmi-core-tests` | **79 passed / 0 failed**（平台 10 + 配置 31 + 流 38） | `...-ITEM005-r1.transcript.json` → `tests` |
| `railhmi-config-check ./config` | `config OK: 6 cameras, 4 roi, 1 integrations`，退出 0 | 同上 → `config-check-good` |
| 坏配置（删 `confirm_frames`） | 报 `thresholds.confirm_frames: required field missing`，退出 1 | 同上 → `config-check-bad` |

### 2.1 六态与升级序列（r1 原文摘录）

`initial state = offline` → `start -> connecting` → `first frame -> live` → `no new frame -> frozen` →
`frozen -> reconnecting`（`reconnect count = 1`）→ `reconnecting -> connecting (retry)` →
`retry timeout -> reconnecting count 2` → `retry again -> connecting` →
**`threshold reached -> offline`** + **`Escalated device event emitted at threshold`** →
`recover from offline -> live` + `reconnect count reset on recovery` + `Recovered event emitted`。
另覆盖 `source error -> reconnecting/offline` + `SourceError`/`Escalated` 事件。

### 2.2 非 LIVE 接口与质量判定（r1 原文摘录）

`offline/connecting/frozen cannot present as live`、`live presents fresh frame as live`、
`degraded counts as streaming (degraded available)`、`degraded may present fresh frame`；
`classify: low mean -> too_dark`、`high mean -> too_bright`、`near-uniform -> occluded`、
`low lapvar -> blurred`、`normal frame -> no flags`、`invalid stats -> no flags`。

## 3. 边界与后续

- 本层只做**流状态与质量判定**；实际取流/解码/冻结检测在 media 层（ITEM-013）驱动这些接口。
- 质量标志的帧统计（`FrameStats`）由 media/quality 层从解码帧计算；本层只做阈值判定（可独立单测）。
- 升级设备事件到报警级别的映射（前方主摄离线/冻结→二级、侧视离线→三级）归规则/事件层（ITEM-015）。
- 运行时状态（fps/bitrate/last_frame_ts 等）发布由 ITEM-... 状态模型接入；本层提供 `state/reconnect_count/last_frame_ms`。

## 4. 证据文件

- `build-CoreTests-192_168_0_103-20261015-ITEM005-r1.{transcript.json,log}`（权威：79/79 + config-check）

## 5. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-005-stream --stamp <tag> --run-tests
# 板上直接: ./build-core/tests/railhmi-core-tests
```
