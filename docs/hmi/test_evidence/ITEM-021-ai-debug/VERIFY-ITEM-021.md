# VERIFY-ITEM-021 — AI 调试页与司机视图隔离（software_verification 权威轮）

- 工单: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-021`
- 能力: `software_tester.software_verification`
- 审核对象: 实现 commit `efdabac`（实现者权威轮 stamp `20261016-ITEM021-r2`，板端 586/586）
- 验证章: `VERIFY-20261016-v4`（权威轮；v1..v3 为过程轮，全部失败源为验证侧夹具预期构造错误，非产品缺陷，详见 D-注记 §5）
- 结论: **TEST_PASS**

本报告为验证角色自身产出的 TestEvidence。不改产品源码；全部独立复跑/断言由验证侧夹具完成。

## 1. 验收范围与判据来源

ITEM-021：实现 AI 调试页与司机视图隔离——展示检测框、ROI、置信度、距离、帧率、端到端延迟、模型版本与最近结果时间；叠加层逐项开关默认关闭；调试描边独立高对比色（不与报警红混淆）；司机角色不渲染本页与任何叠加；司机直达本页显示无权限。

## 2. 独立复跑结果（全部 exit=0 / FIXTURES_RC=0）

| # | 步骤 | 结果 |
|---|------|------|
| 1 | 指纹校验 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 与登记一致 |
| 2 | 交付入口 `board_build.py --mode TestRun` 独立复跑 | `TESTRUN_EXIT=0`；tokens-selftest 8/8、ui-scan `AIDBG_WIRE_EXIT=0`、sim-inject `SIM_EXIT=0`、status-probe `REPLAY_EXIT=0` |
| 3 | core 单测逐行比对 | impl 586 行 == verifier 586 行，`identical=True`、fails=0、`RESULT: PASS passed=586 failed=0` |
| 4 | JUnit 独立解析 | `tests=586 failures=0`；ITEM-021 精确用例名 14/14 命中 |
| 5 | ai-probe 公式断言 | fails=[]（config/queue×4/schedule×2/ai-status/debug×4/acceptance/result 全公式命中：distance=8.0+priority→9.0/10.0/11.0/12.0 m、latency=30+priority×3→33/36/39/42、conf=80、fps=5、dropped=35） |
| 6 | ai-probe 跨轮字节一致 | 与实现者 r2 归档逐行 `identical=True`；二跑 diff `IDENTICAL` |
| 7 | 验证侧 ext 夹具（`verify_ai21_ext.cpp` 链接本轮自产 `librailhmi_core.a`） | **40/40**（距离选择/格式化/逐通道隔离/司机矩阵/ITEM-017 回归，§4） |
| 8 | QML/桥接接线扫描（33 项，上板对 fresh 副本） | mismatch=[] 全 True（§5） |
| 9 | CLI 负例 | `--nope`→`NARG_EXIT=2`；`--help`→`HELP_EXIT=0` |
| 10 | 确定性 | 重跑 `ai-probe --out verify/…` diff `IDENTICAL` |
| 11 | 回归留痕 | 同轮 core-evidence 含 status-probe(+driver)/sim-inject/alarm-probe/clip-probe/replay-probe/ai-probe 全套 |
| 12 | 清理 | `/tmp/railhmi_build`、`/tmp/railhmi_clips` 按名删除，`cleanup_ok=True`（ls exit=2 为删除确认） |

日志: `VERIFY-FIXTURES-VERIFY-20261016-v4.log` / `.transcript.json`（本轮 12 步逐项 exit 输出）。

## 3. 用户可观察点核验（接线扫描域 T1–T8，均 True）

- **T1 叠加层**: 四个逐项开关（overlayBoxes/Roi/Labels/Masks）`property bool … : false` 默认关、每 chip+handler 逐项接通、`overlayAllOff`+`allOff()`+`overlayAny`、页面文案含「默认关闭」。
- **T2 调试描边**: `debug_box=#00E5FF` / `debug_roi=#FFD400` 同时登记于 `design_tokens.yaml`（含 `--tokens` 管线）、`tokens.json`、`Theme.qml`、`Tokens.qml`；P5 仅经 `Theme.color.debug_box/debug_roi` 消费（P5 代码除注释外无 hex 字面量，与交付 checker 同口径）；两值 ≠ 报警红 `#FF6B6B`。
- **T3 字段**: 检测框/ROI/遮罩/标签 + confidenceText/distanceText/fpsText/端到端延迟/modelVersion/lastResultText 全在 P5；距离经 boxes 桥 `distanceM`。
- **T4 司机隔离**: P5 守卫 `railhmiRole !== "driver"` + `aiDebugDenied`「无权限」占位 + 内容行 `visible: page.allowed`；外壳 main.qml 路由 `ai_debug→P5AiDebug.qml`+`DeniedView`+按角色过滤 nav（`entries: railhmiNav`）+ 注入 `railhmiRole`。
- **T5 core 权限源**: `navigation.cpp` Driver 白名单仅 Realtime/AlarmCenter/Replay；`inference.cpp ai_debug_visible` 与之 identity。
- **T6 司机主视图清洁**: P1Realtime 无任何 aiBox/aiRoi/aiMask/aiLabel 调试叠加标识。
- **T7 桥接**: `main.cpp` `m["distanceText"]`/`bm["distanceM"]`/`build_ai_debug` 消费 + 快照五字段。
- **T8 管线**: `board_build.py` TestRun 内跑 `check_ai_debug_wiring.py --root src/ui --tokens design_tokens.yaml`（AIDBG_WIRE_EXIT）。

## 4. ext 夹具 40/40 明细（验收边界，交付 CoreTests 未覆盖）

- **距离选择/格式化（13 判例）**: 12.34→"12.3 m"；多框取最小有效 3.05/7.00→"3.0 m"；全 -1→"—"（通道仍 available）；混 -1/4.50→"4.5 m"（-1 跳出不进 min）；0.0 是有效距离→"0.0 m"；9.96→"10.0 m"；123.45→"123.4 m"；逐通道不串（CAM-01 保持 12.3 m）；结果在但无框→dash 且 available；结果全无（`last_result_ms=-1`）→「等待结果」+ dash；worker-down 叠加无结果→「AI 不可用」+ dash。
- **司机隔离矩阵（19 判例）**: `ai_debug_visible` 四角色（Driver false，M/D/A true）且与 `role_can_access` identity；`resolve_navigation` 直达 AiDebug 司机 `allowed=false`（requested=AiDebug 保留），其余三角色放行；`visible_pages(Driver)` 恰 3 页且不含 AiDebug；其余角色 6 页含 AiDebug；`default_page(Developer)=AiDebug`（design v2 §8）。
- **ITEM-017 回归（8 判例）**: worker-dead→4 路「AI 不可用」；alive 无结果→「等待结果」；age 3001>3000→「推理陈旧」；age==3000 边界仍 available。

## 5. 过程轮记录（D-注记：全部为验证侧夹具预期构造错误，未交付产品缺陷）

- v1（VERIFY-FIXTURES-…-v1）：①探针 log 正则缺 `re.M`，多行锚空匹配致 "debug lines=0"；②ext `check` 签名参数序自错；③P5 头注释含 hex 例色致字面量扫描假阳性（补与交付 checker 同口径的 `//` 剥离）+ tokens YAML 对齐多空格（regex 用 `:\s*"`）。
- v2：ext 漏设 `AiChannelRuntime.last_result_ms`（默认 -1=无结果），全部通道被标「等待结果」——离线 dry-run 之外的板端才现形的默认值构造错；同时"无结果时 distance_text 为空"预期本身错误（距离循环无条件运行，未知恒为 dash）。修正：`channels_from` 显式置 0、无结果判例显式置 -1、无 worker-down+无结果叠加判例（v2 起新增 3 判例）。
- v3：残留 1 处——「无结果」判例自身通道已带 `last_result_ms=0`（age 1000<stale → available 为正确语义）；改为显式置 -1 后 v4 全绿。
- 离线自查 `offline_selfcheck.py`（无板）: ai-probe 预言 vs r2+v1 实测、core 解析计数、junit 用例集、SCAN_PY 本地 remap 路径 exec——无板自查输出 PASS，用于上板前零成本抓预期错。

## 6. 权威证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v4.log` / `.transcript.json` — 权威轮逐步输出/指纹
- `build-TestRun-192_168_0_103-VERIFY-20261016-v4.*` — 本轮独立复跑归档（core-tests 586 行+JUnit XML+ai-probe+回归全套）
- `verify_ai21_ext.cpp` / `verify_ai21_fixtures.py` / `run_verify_round.sh` / `offline_selfcheck.py` — 验证侧夹具四件套
- 过程轮留痕: v1/v2/v3 FIXTURES 日志（含失败步骤输出）

D-注记均已在轮内复盘并随夹具修订闭环，无需交实现返工；本单终态 TEST_PASS。
