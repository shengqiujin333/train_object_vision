# ITEM-007 验证轮 — 语义适配与规则接入 front（VERIFY-20261019-item007-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r1（commit 5a7346b，stamp `20261019-item007-r1`）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-007`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_semantic.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item007-v1` | 全新板端会话：上传 302 文件→CoreTests 构建+测试→selftest→11 fixtures→config→取回 27 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item007_compare.py`（28 判据，不采信交付 RESULT 行） | fresh vs 实现 r1 归档逐字段语义比对 + 独立重推导 |
| V-EXT | 验证自有扩展夹具 `verify/vext_item007.cpp` 板上 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），32 判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item007_vext.py` | 断言 core-tests/selftest 未覆盖的验收边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现 r1 同板同指纹），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。

## 2. 独立复跑结果（fresh vs 实现 r1）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 真实退出码从 `*_EXIT` token 解析（外层 shell 恒 0） | build-core/core-tests/selftest/11×fixture/config 全 0；`--keep-remote` 无 cleanup 步骤 |
| T2 逐行恒等 | selftest/11 fixtures/config 共 13 份 `.log` fresh vs impl r1 | 13/13 全文逐字节恒等 |
| T3 core 单测 | JUnit 独立解析（`\sname=` 锚防 classname 子串匹配） | fresh `tests=1142 failures=0`；判例名多重集与 r1 恒等（1142=1142）；语义域（person on rail/unknown label/aggregated/invalidate/generation/null track/low confidence/off-rail/sorted）32 判例 |
| T4 TC-T-SEM-01 | person 在轨语义独立重推导 | `event type=person_on_rail level=1 track=1`，`events=1 mapped=3`（第 3 帧确认） |
| T5 TC-T-SEM-02 | 未知 label 拦截（不采信 RESULT，独立解析事件行+RESULT 字段交叉） | `events=0`、无 event 行、`unknown=3 mapped=0` |
| T6 TC-T-SEM-03 | 多目标不重复开事件 | track 1/2 各一事件（`events=2`），`mapped=6 det_last=2`；同帧离轨目标不误清在轨确认（`events=1` 仅 track1，`track_reset=0`） |
| T7 TC-T-SEM-04 | generation 变化清确认（独立解析 gen 行） | `gen_reset_keys=1 events=0/0/1`（新代重新累计 3 帧才报警） |
| T8 禁 0 冒充/低置信 | 无 track 丢弃计数不下发；在轨低置信映射为候选外 | no_track：`events=0 no_track=3 det_last=0`；low_confidence：`events=0 low_conf=3 det_last=1` |
| T9 obstacle/不可用/聚合/排序 | 事件与语义两输出面独立解析（kv 分表） | obstacle → `front_obstacle level=1 track=3`；unavailable → `ok=0 available=0 reason=SEMANTIC_UNAVAILABLE det=0`；aggregate → `ok=1 SEMANTIC_OK det=2 cand=2 in=3 mapped=2 agg=1`；sorted → `ok=1 SEMANTIC_OK det=2 agg=1` |
| T10 配置 | 阈值来源交叉核对 | `confirm_frames=3 confidence_min=0.5 suppress_window_ms=3000` 与 `app/config/thresholds.yaml` 逐项一致 |
| T11 selftest 独立统计 | `[PASS]/[FAIL]` 行计数（不读 RESULT） | pass=19 fail=0，`RESULT: PASS failures=0` |
| T12 脱敏 | fresh 27 份产物 + 板端 core-evidence/vext 双目录凭据语义扫描 | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=28/28`（退出码 0，归档 `verify/item007-compare-verify-20261019-item007-v1.json`）。

## 3. 验证自有扩展夹具（vext 32 判例，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item007.cpp`，g 编号；32 check 调用、无分支二选一）：

- 受控清理后缀匹配数字边界（不误清）：g01 `invalidate_track(…,1)` 只清 track1、track11 确认保留并在第 3 帧出事件、g02 `invalidate_track(…,0)` 与 track10 双向同理、g03 `invalidate_track(…,11)` 反向同理——`"|1"` 不匹配 `"|11"`/`"|0"` 不匹配 `"|10"` 的后缀边界全绿
- track 语义：g04 track 0 是合法身份（事件携带 `track_id=0`，与 null 不同）、g05 `require_track=false` 相机级适配：null track 映射 `track_id=-1` 下发且确认出事件（不冒 0）
- 白名单：g06 known-but-not-allowed（如 `car`）单独计数 `not_allowed=3 unknown=0` 且零报警（与 unknown 拦截互不串扰）、g07 空串/大小写变形 `Person`/`dog` 全部拦截 `unknown=3`
- 受控清理作用域：g08 CAM-01 generation 复位只清本相机 1 个 key、CAM-02 确认存活并出事件、g09 `invalidate_track` 只清本相机本 track、g17 `invalidate_generation` 计数本相机 3 个 key（track1 确认后 confirm/last_emit/key_event_id）、事件不删除、CAM-02 track3 第 3 帧确认
- 陈旧代防御：g10 stale generation 静默丢弃 `reason=SEMANTIC_STALE_GENERATION` 不下发，且既有确认状态存活（回到当前代第 3 帧照常出事件）
- 聚合与排序：g11 三 track 输入 detections 严格升序（3/5/7）且事件按序发射、g12 同 track 聚合保留高分整框（confidence 与 distance 一并取自 0.9 分框，`aggregated=1`）
- 确认边界：g13 confidence 恰等于 `confidence_min` 仍为候选（`>=` 边界，`low=0`、第 3 帧出事件）、g14 低置信帧只清本 track 确认（`low=1 track_reset=1`），重计后第 6 帧才出事件
- 空面：g15 ready+空框无报警无判定（`available=1`、零事件）、g16 同代连续帧 `gen_reset=0`（清理只在换代时发生）

驱动判定（`vext-item007-…json`）：9/9 检查 PASS — V0 保留树在场、V1 板端 g++ 直编 2.6s、V2 板上 32/0 + 判例名集合与夹具源恒等（src=32 board=32）+ g01–g17 域全 present、V3 CLI 负例 unknown-arg=2/no-action=2/help=0/未知 fixture=2、V4 `--selftest` 与 `--fixture person_on_rail` 同参两轮 sha256 恒等、V5 板端泄漏 0、V6 板端 `/tmp/railhmi_vision_item007_verify-20261019-item007-v1` 树与 vext 构件清理确认（`No such file` 即删除成功确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 按受支持标签白名单严格映射为 `core::Detection` 并交给 RuleEngine | T4/T9：person/obstacle 事件语义与级别独立重推导；vext g11/g12 映射面 |
| 未知 label 被拦截（用户可观察：注入未知 label 不产生报警） | T5：`events=0 unknown=3 mapped=0`；vext g07 空串/大小写/dog 变体全拦截 |
| 同帧多目标先聚合再按稳定 track 排序 | T9：aggregate `det=2 agg=1`、sorted 升序；vext g11 严格升序 + 事件发射序、g12 聚合取高分整框 |
| 禁止用 track_id=0 冒充同一目标 | T8：no_track `det_last=0`；vext g04 track 0 合法携带、g05 null→-1 不冒 0 |
| generation 变化经受控接口清理（跨 generation 不复用旧确认状态，用户可观察） | T7：`gen_reset_keys=1 events=0/0/1`；vext g08 跨相机隔离、g10 stale 防御、g16 同代不清 |
| 失帧经受控接口清理、不整路误清无关目标 | T6：off_rail 同帧离轨不清在轨确认（`track_reset=0`）；vext g01/g02/g03 后缀边界、g09 跨相机隔离、g14 低置信只清本 track |
| 阈值来自配置 | T10：三阈值与 `thresholds.yaml` 逐项一致 |

## 5. 观察项（不阻塞，交后续参考）

- O-1 实现文档（`ITEM-007_SEMANTIC_RULES.md` §1）写「探针 selftest 30 判例」，板端实测为 **19**（`[PASS]` 行独立计数）。结果无碍（19/0 全绿），文档计数与事实不符，建议实现方订正。
- O-2 off_rail 夹具 `track_reset=0`：track2 首帧即离轨、其 track 无确认 key 可清，`invalidate_track` 计 0 属预期行为（非缺陷）。
- O-3 `RuleEngine::on_detection` 非候选整相机 reset 语义未改（实现文档 §6.5 声明）；调用方须只下发适配器输出，交接 ITEM-009 落实。本验证通过适配器公开入口驱动，未绕过该契约。

## 6. 验证侧过程记录（自查修正，无产品缺陷误报）

1. V-CMP 首轮 26/28：`C3_same_track_aggregated`/`C3_sorted_stats_consistent` 失败为验证侧解析错——aggregate/sorted/unavailable 家族输出 `type=result type=semantic ok=…`（res_line 面），与事件型 `type=result events=…` 是两个输出面（ITEM-012 kv 分表教训同源变体），加 `sem_line()` 后 28/28。
2. vext 板端编译失败两轮（板上 g++ 直报比离线审查快，既有教训）：`SemanticAdapter` 静态面只有 `invalidate_track`/`invalidate_generation`（无 `invalidate_camera`，后者是 RuleEngine 成员名）——改走 `invalidate_generation`；`DetectBox` 无 `distance_m` 字段（距离经 `EnvelopeBuildInput::distances_m` 进 envelope，`Detection::distance_m` 为 double）——`bx()` 去掉第 4 参、距离经 `mk()` 显式注入。
3. vext 首轮板面 31/32：g12 失败为验证侧 float 比较构造错——`Detection::confidence` 是 double，由 `float 0.9f` 提升后精确值为 `(double)0.9f`，不等于字面量 `0.9`；板面 detail（`conf=0.900000 dist=20.000000`）示值正确。改精确提升值后 32/32。
4. 上板前离线自查修正 g17 期望算术（4→3：场景只喂 track1/CAM-01 与 track3/CAM-02，`invalidate_generation("CAM-01")` 只能命中 track1 的 3 个 key）——避免一次无效上板（夹具预期先行自查纪律）。

## 7. 证据清单（本目录，`verify-20261019-item007-v1` 标记）

- `vision-item007-192_168_0_103-verify-20261019-item007-v1.transcript.json` / `.log` — 独立复跑结构化/人类可读 transcript
- 同名 `app_core-evidence_item007-*` 27 份 — core JUnit、selftest log/json、11×fixture log/json、config log/json
- `verify/verify_item007_run.py`、`verify/verify_item007_compare.py`、`verify/verify_item007_vext.py`、`verify/vext_item007.cpp`、`verify/item007-compare-verify-20261019-item007-v1.json` — 验证自有驱动/比对/扩展夹具/比对归档
- `vext-item007-verify-20261019-item007-v1.stdout.log` / `.json` — vext 板端判定归档

## 8. 覆盖边界（本轮 TEST_PASS 不声称的范围）

- 仅证明 ITEM-007 与本版本（commit 5a7346b）的已测范围：语义适配与规则接入纯逻辑（夹具 envelope，`in_roi`/`track_id` 显式给定）。
- 未执行真实 RKNN 前向、无真实跟踪（ITEM-008 未实现）、未连接相机、未测多线程/多相机并发与 P5 调试视图——均为交付文档声明的边界（归 ITEM-009 及后续）。
- `RuleEngine::on_detection` 既有语义未重验（不在本工作项范围）；主机侧仅静态审阅与 oracle dry-run，所有功能结论来自板端实测。
