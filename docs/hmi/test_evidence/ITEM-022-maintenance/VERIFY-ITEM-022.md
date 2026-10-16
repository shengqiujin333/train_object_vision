# VERIFY-ITEM-022 — 设备维护页（software_verification 权威轮）

- 工单: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-022`
- 能力: `software_tester.software_verification`
- 审核对象: 实现 commit `20a192b`（实现者权威轮 stamp `20261016-ITEM022-r1`，板端 612/0）
- 验证章: `VERIFY-20261016-v3`（权威轮；v1/v2 为过程轮，全部失败源为验证侧夹具预期构造错误，非产品缺陷，详见 §5）
- 结论: **TEST_FAIL**（core/桥接/QML 全绿，但任务书要求的手动重连入口与按日滚动维护日志缺失，见 §4 D-01/D-02）

## 1. 验收范围与判据来源

ITEM-022：设备维护页——摄像头在线/码流/帧率/最近帧/质量、AI 状态与模型版本、录像进程与剩余容量、网络错误计数与温度、云台 Pan/Tilt/Zoom 实际值与目标值及容差校验、重连入口与按日滚动的维护日志；NVR 以设备卡显示登记/可达/未配置状态与通道映射（未提供时显示未配置，不虚构通道号，不提供自动修改入口）；一键归位与重新调用前方预置位并二次确认；调用后显示目标值与实际值及容差结果，超差显示失败并令前方 AI 未就绪，操作写入维护日志。判据来源: 工单原文 + `docs/hmi/INTERACTION_DESIGN.md` v2 §5.6（含"重连：支持手动重连"；"维护日志：…记录时间/用户/结果"）+ §7.4 + `docs/hmi/VISUAL_DESIGN.md` v2 §8.6 + `docs/hmi/TEST_DESIGN.md`（TC-PTZ-01/02、TC-HLT-*、TC-INTG-01）。

## 2. 独立复跑结果（v3 全绿，FIXTURES_RC=0）

| # | 步骤 | 结果 |
|---|------|------|
| 1 | 指纹校验 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 与登记一致 |
| 2 | 交付入口 `board_build.py --mode TestRun --keep-remote` 独立复跑 | `TESTRUN_EXIT=0`、`TOKENS_EXIT=0`、`MAINT_WIRE_EXIT=0`、`SIM_EXIT=0`、各探针 EXIT=0 |
| 3 | core 单测逐行比对 | impl 612 行 == verifier 612 行，`identical=True`，fails=0，`RESULT: PASS passed=612 failed=0` |
| 4 | JUnit 独立解析 | `tests=612 failures=0`；ITEM-022 精确用例名 11/11 命中 |
| 5 | maintenance-probe 公式断言 | fails=[]（ptz-check Δ0.20/0.30/0.05→容差内、Δ4.00→超差；两动作 confirm=true applied=true；两写日志；front-ai READY/NOT_READY；cam 行实时/fps/最近帧/质量；recording 75%; health 文本） |
| 6 | maintenance-probe 跨轮字节一致 | 与实现者 r1 归档逐行 `identical=True`；二跑 diff `IDENTICAL` |
| 7 | 验证侧 ext 夹具（`verify_maint22_ext.cpp` 链接本轮自产 `librailhmi_core.a`） | **83/83**（容差边界含 ≤、验证侧独立复算 delta 相等、动作/日志三态映射、front-AI 真值表、维护行 stale 边界/缺 runtime→未启动/Degraded 在线/质量组合顺序/重连 rune 文本、录像 GB/MB 边界+整数除法、health 四舍五入+占位、channel-view 助手回归，§4） |
| 8 | QML/桥接接线扫描（22 项，上板对 fresh 副本） | mismatch=[] 全 True（七分区卡、PTZ 目标/实际/容差/前方 AI、二次确认模态、recentLogs、NVR 无自动修改入口、桥接 core 调用、main.cpp 注入、TestRun 管线、tokens-only 配色） |
| 9 | 确定性 | 重跑 maintenance-probe diff `IDENTICAL` |
| 10 | 回归留痕 | 同轮 core-evidence 含 status-probe(+driver)/sim-inject/alarm-probe/clip-probe/replay-probe/ai-probe/maintenance-probe 全套 |
| 11 | 清理 | `/tmp/railhmi_verify022`、`/tmp/railhmi_build`、`/tmp/railhmi_clips` 按名删除，`cleanup_ok=True`（ls exit=2 为删除确认） |

本用户可观察点（PTZ 目标 vs 实际 + 容差、超差 → 前方 AI NOT_READY、写日志、NVR 未配置呈现 + 无自动修改入口、分区卡全字段）全部核验通过。**不通过的是任务书中另两项要求（手动重连入口、按日滚动日志），见 §4。**

## 3. 用户可观察点核验明细（接线扫描 T1–T11，均 True）

- **T1** 七分区卡（maintCameraCard/AiCard/RecordingCard/HealthCard/PtzCard/LogCard/NvrCard）齐备。
- **T2** PTZ 三轴 实际/目标 显示 + `ptzCheckText` 容差 + `ptzFrontAi` READY/NOT_READY。
- **T3** [一键归位][重调前方预置位]（`askPtz`→`pendingKind`）+ `ptzConfirmModal` 二次确认（Yes 执行 `M.ptzAction(kind)`，No 取消置空）+ 确认后 reload。
- **T4** 维护日志列表消费 `recentLogs` 逐列展示 action/user/result/detail。
- **T5** NVR `DeviceCard`（registered/state/channelMapping）且代码无 修改/setChannel/editNvr（无自动修改入口）。
- **T6/T7** 摄像头行 online/stateLabel/fpsText/lastFrameText/qualityLabel；AI 卡 modelVersion/fpsText/lastResultText/available。
- **T8** 重连计数文本在 main.cpp 侧已桥接（`reconnectText`），P6 未消费（见 D-01 关联）。
- **T9** 桥接委托 core（ptz_check/ptz_home/ptz_recall_front_preset/build_ptz_log/front_ai_ready）；main.cpp 注入 railhmiMaintenance(Cameras)。
- **T10** TestRun 跑 `check_maintenance_wiring.py`（MAINT_WIRE_EXIT）并取回 maintenance-probe 日志（MAINT_EXIT）。
- **T11** P6 代码无 hex 字面量，仅 `Theme.color.*`。

## 4. 缺陷（TEST_FAIL 依据，交回实现）

- **D-01 (P2) 缺少手动重连入口** — 任务书与交互方案 §5.6 均要求维护页可手动重连，P6 无任何重连控件/桥接方法（源码级确证：P6Maintenance.qml 无 reconnect 拥有以下关键词，maintenance_model.{h,cpp} 无 Q_INVOKABLE reconnect*，main.cpp 只桥接只读 `reconnectText`）。这也与显示层缺陷同源：T8 的重连计数文本桥接后 P6 也未展示。用户可观察期望：维护页每路摄像头行（或设备卡）提供"重连"操作并生效。
- **D-02 (P3) 维护日志未按日滚动** — 任务书要求"按日滚动的维护日志"。当前桥接为内存 `logs_`（cap 500, push_back + erase(begin)），无日键/日分组，也未复用 ITEM-007 的 `maintenance_log` SQLite 表（`SqliteRepository::insert_maintenance` 存在但桥接未接）。实现者自己的 README §3 已声明该边界（"真实入库归后续集成"），但任务书验收的文字层没有允许豁免：判据是"按日滚动"。
- 两者证据: `verify_scan22.py` 在板端 fresh 副本上的 defect-probes 输出（D1_manual_reconnect_entry_present=False、D2_log_day_rolling_present=False）+ 源码位置；ext 夹具/接线扫描 v3 全绿证明除该两项之外其余判据达标。

## 5. 过程轮记录（D-注记：全部为验证侧夹具预期构造错误，未交付产品缺陷）

- v1（72/7）：①zoom "delta==0.1" 边界用了 1.1−1.0（FP 0.10000000000000009 > 0.1，实际上判定的是"刚超差"而非"恰在容差"，且其在该输入值下的 out_of_tolerance 正确）→ 复测改用二进制精确的半分值并把真正 "inclusive" 边界放 pan/tilt（11−10=1.0 非完整幂次封装：0.5/0.25）；②age 期望错——`last_frame_ms=0` 在 now=5000 时是"5 秒前"而非"age 0"；③"过曝"写成 `\xE6\x9B\x9A`（曚，交付方为正确的 曝 `\xE6\x9B\x9D`）；④health needle 期望 `"温度 59°C"` 有蛋——见 v2。
- v2（82/1）：C++ 十六进制转义贪婪吞并——needle `…59\xC2\xB0C` 中 `\xB0C` 被单次转义解析为 0xB0C，导致 needle 字节错；用相邻字面量分割 `"…59\xC2\xB0" "C"` 修复。同时审计 `offline_selfcheck3.py` 确认夹具中无再用 `\xHH` 后跟十六进制字。
- v1/v2 均未产生产品缺陷误报；v3 全绿（83/83 + 22 扫描项 + 探针公式/确定性全过）。
- 三份离线自查脚本（无板）在上板前零成本抓出夹具预期错与转义风险：`offline_selfcheck.py`（oracle/解析/扫描预言）、`offline_selfcheck2.py`（构造错审计 + arity/mojibake/FP 边界）、`offline_selfcheck3.py`（greedy hex-escape 风险）。
- 过程轮中 T8 检查项从"重连文本已消费"改为事实登记（main.cpp 桥接=True / P6 消费=False），否则会把 D-01 掩盖为全绿假象；这也是一条防止"扫措辞漂移写成 expected-True"的教训。

## 6. 权威证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v3.log` / `.transcript.json` — 权威轮逐步输出/指纹
- `build-TestRun-192_168_0_103-VERIFY-20261016-v3.*` — 本轮独立复跑归档（core-tests 612 行 + JUnit XML + maintenance-probe + 回归全套）
- `verify_maint22_ext.cpp` / `verify_maint22_fixtures.py` / `run_verify_round.sh` / `offline_selfcheck*.py` — 验证侧夹具
- 过程轮留痕: v1/v2 FIXTURES 日志（含失败步骤输出）

D-01/D-02 交回实现返工；本单终态 **TEST_FAIL**。
