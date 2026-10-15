# VERIFY-ITEM-005 — railhmi-core 流状态机与质量判定（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-005`（commit **ad27e8b**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/include/railhmi/core/stream_state.h`、`app/src/core/src/stream_state.cpp`、`app/tests/core/test_stream.cpp`、`app/src/core/src/config.cpp`/`config.h`（流时序阈值字段与校验）、`app/config/thresholds.yaml`、`app/tests/core/main.cpp`
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-02 / TC-CORE-03**（ENV-A0）；DESKTOP_APP_DESIGN §6.2（六态图/配置化阈值/硬约束/DEGRADED 降级可用§7.1）；INTERACTION_DESIGN v2 §7.1（非 LIVE 禁止旧帧实时硬约束）
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（独立复跑 79/79、验证自有扩展夹具 40/40、阈值校验负例 2 条全部与预期一致）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**：逐行审阅 `stream_state.cpp`/`stream_state.h` 对照设计 §6.2——六态齐备；CONNECTING 首帧超时（≥，state_since 基准）、LIVE/DEGRADED 无新帧超时（≥，last_frame 基准）→ FROZEN；FROZEN 再超 freeze_timeout → begin_reconnect（计数 +1；达 `reconnect_threshold` → OFFLINE + `Escalated`，否则 RECONNECTING）；RECONNECTING 经 `reconnect_backoff_ms` 退避 → CONNECTING；恢复出帧清零计数并按质量栈进 LIVE/DEGRADED；`may_present_as_live` 仅在 is_streaming 且帧龄 < freeze_timeout 为 true（严格 `<`，恰好等于阈值即拒绝）；事件缓冲含 `camera_id/kind/t_ms/reconnect_count/detail`。质量判定 `classify_quality` 仅消费 `FrameStats`（不触图像），mean<40 → too_dark、mean>220 → too_bright、stddev<3 → occluded、lap_var<30（且 ≥0）→ blurred；负值统计不产生对应标志，均值参与 dark/bright 双界判定。阈值 schema 扩展 `first_frame_timeout_ms`（必须 ≥1）、`reconnect_backoff_ms`（必须 ≥0）进 `load_thresholds` 必填通道，与 `confirm_frames` 同源。
2. **板端独立复跑**（`--stamp 20261015-verify-v1 --run-tests --keep-remote`）：工作树处 HEAD（commit ad27e8b 等同源码），52 文件上传 `/tmp/railhmi_build`，全部步骤有限等待：构建 `BUILD_EXIT=0`，测试 **79 passed / 0 failed（TEST_EXIT=0）**，`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`。74 条 stream 断言逐项与实现者 r1 同名 PASS 逐文件比对一致（含六态/升级序列/事件/非 LIVE 接口/classify 全套）。sqlite 用例 `[SKIP]`（板端无 libsqlite3-dev，与 ITEM-003/004 已登记一致，外围缺口非本层判据）。
3. **验证自有扩展夹具**（**40/40**，非实现用例；源码 `verify_stream_ext.cpp` 单文件 g++ 直接联编 `stream_state.cpp`，命令与输出在 `VERIFY-FIXTURES-...-v3.transcript.json`）——对实现未覆盖的边界独立验证：
   - classify 阈值边界：`40.0/220.0/3.0/30.0` 严格不触发（`<`/`>` 正确）、跨过阈值即触发；`stddev`/`lap_var` 负值不影响 mean 判定（"partial invalid stats → only occluded"）。
   - 全五态拒绝旧帧实时（offline/connecting/reconnecting/frozen/offline-after-escalated）；帧龄恰好等于 `freeze_timeout` 拒绝、差 1ms 通过（严格 `<` 语义核验）。
   - 显式重连通道：`on_reconnect_attempt` 参与计数与升级（threshold=2 一次显式调用即 OFFLINE + Escalated）；threshold=3 连调三次先 RECONNECTING 再升级；每次 attempt 恰好一条 `ReconnectAttempt` 事件。
   - 事件缓冲语义：drain 取出全部并清空（第二次 drain 为空）。
   - DEGRADED 链端到端：坏质量首帧 → DEGRADED 流含 `Started+QualityDegraded+FirstFrame` 事件三件套 → 无新帧冻结 → 冻结超时升级达 threshold → OFFLINE（粘滞）→ 携标志恢复回 DEGRADED、计数清零、`Recovered`。质量标志恢复区别于纯 LIVE 恢复。
   - OFFLINE 粘滞：escalated 后任意 tick 不脱离 OFFLINE（无隐式自启动）。
   - `start()` 语义：重建后计数清零、Started 事件缓冲；两次重放序列（每轮逐状态 check）与实现六态转换序列无分歧。
4. **阈值配置负例**（`VERIFY-FIXTURES-...-v3` 内 fx1/fx2/fx3 板上 `railhmi-config-check`）：`first_frame_timeout_ms: 0` → `must be >= 1`、`reconnect_backoff_ms: -1` → `must be >= 0`（皆 `CFG_EXIT=1`、报具体字段路径）；未修改参照 → `config OK: 6 cameras, 4 roi, 1 integrations`（CFG_EXIT=0）。
5. **指纹链**：实现者 r1（20261015-ITEM005-r1）、验证复跑（20261015-verify-v1）、验证夹具 v3 三轮均 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与 USER-ITEM-002/003/004 历史一致。

### 1.1 方法修正记录（验证侧自查，不含实现缺陷）

- v1 轮夹具 `multi-flag describe` 期望互斥组合（`too_dark`↔`too_bright` 同帧）——断言本身不可满足，非实现缺陷；v3 轮改用可实现组合 `too_bright+occluded+blurred` 后通过，v1/v2 transcript 归档仅为过程留痕。
- v2 轮 clearance 后由本轮 cleanup 一并清走 `/tmp/railhmi_build`，导致 v3 首次运行在空目录上（全部 cp 失败，未产生测试结论）；重建构建树后重跑，v3 为有效轮。无效中间轮（v1 夹具错误、v2/v3 半轮）不做判据。
- v1 轮 CLI 本机 MSYS 路径转换把传参 `/tmp/...` 改写为 `C:/Users/.../temp/...`，板上命令按无效路径失败且未触达任何板面状态；以 `MSYS_NO_PATHCONV=1` 重跑核正。该轮证据（`VERIFY-FIXTURES-...-v1.*`）为工具配置过程留痕，非板面结论。
- 结论不变：两轮有效轮（verify-v1 CoreTests + v3 夹具）判据全部独立通过。

## 2. 判据逐条核对（工作项描述 / TC-CORE-02、03）

| 判据 | 结果 | 证据 |
|---|---|---|
| 六态齐备（CONNECTING/LIVE/FROZENOFFLINE/RECONNECTING/DEGRADED） | ✅ | 实现 38 断言 + 夹具 40 断言全部覆盖；枚举/`to_string` 六值静态核对 |
| 模拟时钟驱动六态转换（启动→CONNECTING→LIVE→FROZEN→RECONNECTING→达阈值→OFFLINE） | ✅ | `test_six_states_and_escalation` r1/verify-v1 逐次 tick 单调时钟 PASS；夹具头三态反向序列（freeze→attempt→backoff→reconnect）逐次 PASS |
| 首帧/冻结/重连阈值退避（含配置层校验 `first_frame_timeout_ms``reconnect_backoff_ms`） | ✅ | 基线 5000/1000 解析断言 PASS；fx1/fx2 报具体字段路径且非零退出；`first_frame_timeout_ms: 0` / `reconnect_backoff_ms: -1` 拒收 |
| 重连计数与阈值升级设备事件 | ✅ | `reconnect_count = 3 (threshold)` + `Escalated` 实现断言；夹具重复通道（`explicit attempt reaches threshold`/`two attempts below threshold`/`one ReconnectAttempt event per attempt` 独立复现升级恰在阈值处） |
| 非 LIVE 禁止旧帧实时（`may_present_as_live`） | ✅ | 实现 5 个非 LIVE 状态断言；夹具全五态拒绝 + 严格 `<` 边界（恰好 `freeze_timeout` 拒绝、差1ms通过） |
| 质量标志（过暗/过曝/遮挡/模糊）与 `classify_quality` | ✅ | 实现 6 断言（逐标志/正常/无效统计）；夹具阈值边界 9 断言独立通过；组合 flags 顺序逐一静态核对 |
| 质量标志注入可得 DEGRADED | ✅ | `quality flag -> degraded`/`degraded counts as streaming`/`degraded may present fresh frame`；夹具含 DEGRADED→FROZEN 全链端到端与带标志恢复 |
| 断流→重连计数→达阈值升级序列复现（用户可观察） | ✅ | r1/verify-v1 §2.1 原文摘录值逐项核对一致（per-event `reconnect_count` 1→2→3 与 OFFLINE 升级时机） |
| `Recovered` 事件及重连计数清零 | ✅ | 实现断言 + 夹具从 offline 恢复（含带质量标志恢复 → DEGRADED）、normal 恢复 → LIVE |

## 3. 观察与边界（不改变 PASS 结论；交实现/后续项参考）

- **O-1（观察）**: classify 三阈值语义边界（`<` vs `<=`）仅由帧统计连续值自然覆盖，设计文档未明确严格要求；当前实现读作"恰好等于阈值不触发"，夹具已按此独立核验一致，若 UI 告警文案期望"≥"含义需实现/设计确认（非本层判据缺口）。
- **边界**: 本层只做状态机与阈值判定；实际取流/解码/冻结检测接口驱动（ITEM-013）、事件到报警级别映射（ITEM-015）、运行时状态模型发布（后续项）边界在 `ITEM-005 README.md §3` 已如实声明，本证据不外推。
- **环境事实**: 板端无 libsqlite3-dev/Qt5 Quick（历史登记一致）；仅标准库 core 层不受影响；收尾 `/tmp/railhmi_build /tmp/railhmi_verify /tmp/railhmi_fx` 全部按名清理（cleanup 步骤 `fixture_cleanup_done`）。
- **易变项说明**: `/tmp` 目录残留仅来自本轮验证工作，已确认清理到位。
- 扩展夹具在 g++ 直编 `stream_state.cpp` 下编译零告警，无外部依赖。

## 4. 复现

```sh
# 独立复跑（CoreTests 79/79 + config-check good/bad；--keep-remote 供追加夹具）
python app/tools/board_build.py --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-005-stream \
  --stamp 20261015-verify-v1 --run-tests --keep-remote

# 验证自有扩展夹具（40/40 + 阈值负例；--expected-fingerprint 需与已知一致，否则中止）
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python \
  docs/hmi/test_evidence/ITEM-005-stream/verify_stream_fixtures.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --remote-build-dir /tmp/railhmi_build \
  --cpp docs/hmi/test_evidence/ITEM-005-stream/verify_stream_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-005-stream \
  --stamp verify-item005-20261015-v3
# 板上直接: cd /tmp/railhmi_build && g++ -std=c++17 -Wall -Isrc/core/include \
#   /tmp/railhmi_verify/verify_stream_ext.cpp src/core/src/stream_state.cpp \
#   -o /tmp/railhmi_verify/verify_stream_ext && /tmp/railhmi_verify/verify_stream_ext
# 收尾清理（夹具运行器已释放 build tree；剩余按名清理）
rm -rf /tmp/railhmi_build /tmp/railhmi_fx
```

## 5. 证据文件

- `build-CoreTests-192_168_0_103-20261015-verify-v1.{log,transcript.json}` — 验证复跑原始日志与逐命令 transcript（指纹与实现者 r1 一致）
- `VERIFY-FIXTURES-verify-item005-20261015-v3.{log,transcript.json}` — 验证自有夹具（40/40 + fx1/fx2 neg + fx3 ref）逐条命令/输出/清理
- `verify_stream_ext.cpp` — 夹具源码；`verify_stream_fixtures.py` — 验证运行器
- 实现者被核验交付: `build-CoreTests-192_168_0_103-20261015-ITEM005-r1.{log,transcript.json}`（79/79 + config-check）
- v1（MSYS 路径转换错误导致命令定位失败，非板面结论）、v2（夹具互斥断言错误）过程留痕: `VERIFY-FIXTURES-...-v1/v2.{log,transcript.json}`，不作为判据