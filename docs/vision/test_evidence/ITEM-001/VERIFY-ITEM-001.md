# ITEM-001 验证轮 — 模型能力描述与就绪探测（VERIFY-20261018-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r3（commit dfbf43a，stamp `20261018-item001-r3`）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-001`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_model_probe.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261018-item001-v1` | 全新板端会话：上传→CoreTests→MediaCheck→selftest→真实 rknn_query→核验→配置汇总→取回 13 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item001_compare.py`（38 判据，不采信交付 acceptance 行） | 实现 r3 归档逐字段语义比对 |
| V-EXT | 验证自有扩展夹具 `verify/vext_item001.cpp` 板端 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），28 判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item001_vext.py` | 断言 selftest 未覆盖的核验边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。

## 2. 独立复跑结果（fresh vs 实现 r3）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 11 个步骤真实退出码（`*_EXIT` token 解析；外层 shell 恒 0 不可用） | build-core/core-tests/build-media/selftest/rknn-seg/rknn-det/verify-seg/verify-det/config-nodesc/config-with-desc=0，verify-negative=1；`--keep-remote` 无 cleanup；主机指纹在位 |
| T2 selftest | 8 判例逐行 `[PASS]` 且 actual==expect；与 r3 全文恒等 | `RESULT: PASS passed=8 failed=0`，逐字节一致 |
| T3 真模型 | seg 13 输出 + 输入 NHWC `1,640,640,3` int8/affine zp=-128 + 恰一 proto `1,32,160,160` + 三尺度 80/40/20；det 3 输出全 255 通道 | 全部命中（rknn_query 真实读取，运行时 API 2.3.2 / 驱动 0.9.8） |
| T4 恒等 | fresh vs r3 张量集合（dir/index 键 → name/n_dims/dims/type/fmt/qnt/zp/scale）逐字段恒等 | seg、det 双模型 identical |
| T5 核验 | verify-seg `ready=1 READY matched=10 mismatched=0`；verify-det `ready=1 READY matched=8`；负例（真实 seg 描述按 yolov5s 声明）`MODEL_MISMATCH declared=yolov5_detect identified=yolov8_seg`（**文件名被忽略**）；verify-seg 的 zp/scale 与原始 query JSON 交叉一致 | 全部命中 |
| T6 配置 | 4 登记：front/yolov5s `available=1 READY`，其余三 profile `PROFILE_UNSUPPORTED`；`declared_failed=0 unsupported_profiles=3 unverified=0 profile_gap=true`；nodesc 轮 front `NOT_READY file present; structure not verified` | 全部命中（文件存在≠ready 判据有效） |
| T7 core 单测 | JUnit `tests=958 failures=0`；fresh 与 r3 判例名多重集恒等（958 条、928 唯一名）；38 个 ITEM-001 判例名（spec/identify/ready/FI-AI-01..03/TC-T-CAP-03/profile evidence/declared/path/roundtrip/JSON）全部在位 | 全部命中 |
| T8 脱敏 | fresh 全部产物 + 板端 core-evidence/descriptors 双目录凭据语义扫描（`user:pass@` URL / `password=` 键值） | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=38/38`（退出码 0）。

## 3. 验证自有扩展夹具（vext 28 判例，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item001.cpp`，g 编号）：

- 结构识别负控：g01 双 proto 拒绝、g02 非方空间不判 detect、g03 proto 通道 64 不识别为 seg、g22 分支残缺 unknown
- 实板布局：g04 NHWC `1,640,640,3` 输入经 normalize_chw 归一后 ready（真实模型输入布局的夹具级锚定）
- 匹配数学：g05 缺 score_sum 拒绝、g06 detect 通道 257 拒绝、g07 输出量化不设门（当前契约行为锚定，防回归漂移）、g21 双输入拒绝
- 文本往返：g08 负 zp/小数 scale 精度窗、g09 未知 type/fmt/qnt 容错映射 Unknown/Unsupported、g10 空描述永不 ready
- 规格/路径：g11 声明名大小写无关、g12 `.rknn` 后缀/尾斜杠、g13 spec 与 profile 无关
- evidence 门：g14 wheel_rail 无 seg 证据、g15 front 接受 seg、g16/g17 requirement 仅 front 有证据
- 输出面：g18 reason 词表无 `/`/`@`、g19/g20 JSON reason/ready/file 字段

驱动判定（`vext-item001-…json`）：10/10 检查 PASS — V2 板上 28/0 + 计数对账（源 29 处 check 调用，g10 if/else 二选一 → 运行时恒 28 行）、V3 CLI 负例 unknown-arg=2/no-action=2/help=0/缺描述文件=2、V4 `--selftest --json` 同参两轮 sha256 恒等、V5 板端泄漏 0、V6 板端 `/tmp/railhmi_vision_item001_verify-20261018-item001-v1` 树与 vext 构件清理确认（`No such file` 即删除成功确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 按 ai_profile 加载配置登记的模型（不写死） | T6：模型名/版本全部来自 `app.ai.models`（4 条登记逐条出现，路径按 `--models-dir` 解析）；vext g11-g13 锚定解析逻辑 |
| rknn_query 读取数量/形状/量化/版本 | T3/T4：真实 query 双模型 13+3 输出逐张量归档，API/驱动版本在位 |
| 逐项匹配通过才 ready | T5：seg matched=10 / det matched=8 逐项 `[ok]` 明细；mismatched=0 |
| 缺失/不匹配 → unavailable + 稳定 reason | T5 负例 MODEL_MISMATCH + T2 夹具 FI-AI-01/02/03 + T6 NOT_READY/PROFILE_UNSUPPORTED；vext g01-g06/g21-g22 边界负控 |
| 不得只按文件名判断 | T5 负例 detail 明示 `file name ignored`；T6 nodesc 文件存在仍 NOT_READY |
| --selftest 两条路径确定可复现 | T2 夹具 8 判例 + V4 同参两轮 sha256 恒等（板端实测） |
| 探针/P5 可读 model_version 与不可用原因 | T6：每条登记 `model_version=rknn-2.3.2/...` + reason；词表脱敏由 vext g18 锚定 |

## 5. 观察项（不阻塞，交后续参考）

- O-1 `profile_gap=true`：`app.ai.models` 未给 front 登记 seg（两阶段需要 seg+detect），wheel_rail 名下登记的 yolov8s-seg 因 profile 无证据不计可用。这是配置事实的如实上报（实现文档 §6 已声明交需求确认），不是本增量缺陷；后续 ITEM 需要两阶段时须先补配置。
- O-2 输出张量量化不设门（vext g07）：规格只校验输出结构，不校验输出 dtype/量化。与当前后处理期望一致（结构优先），若后续后处理依赖输出量化参数，需在规格中补 `out_must_be_quantized`。

## 6. 验证侧过程记录（自查修正，无产品缺陷误报）

1. 驱动脚本 repo 根推导层级错 2 次（verify 目录深 5 级）——路径计算先 dry-check 再上板。
2. 独立比对首轮 3 失败均为验证侧构造错：外层 shell 退出码恒 0（须解析 `*_EXIT` token，ITEM-002 既有教训复现）、selftest regex 组配对错位、JUnit 重名判例（958 条 928 唯一名，ITEM-008 教训复现）——修正后 38/38。
3. vext 静态库路径首版用平铺 `build-core/src/librailhmi_core.a`，板端 ld 直报后改分层 `build-core/src/core/…`（ITEM-005 教训同源）。
4. vext 判例数断言从手抄 26 改为按夹具源 `check(` 调用统计（29 处，g10 分支二选一 → 28 行），既有教训"不手抄"再复现。
5. 首轮 V6 清理误删构建树 → 驱动加 `--rebuild`（重建交付树后复用 vext）。

## 7. 证据清单（本目录，`verify-20261018-item001-v1` 标记）

- `vision-item001-192_168_0_103-verify-20261018-item001-v1.transcript.json` / `.log` — 独立复跑结构化/人类可读 transcript
- 同名 `app_core-evidence_item001-*` 13 份 — core JUnit、selftest log/json、rknn-seg/det JSON、verify-seg/det/negative、config-nodesc/desc log/json、双模型文本描述
- `verify/verify_item001_run.py`、`verify/verify_item001_compare.py`、`verify/verify_item001_vext.py`、`verify/vext_item001.cpp` — 验证自有驱动/比对/扩展夹具
- `vext-item001-verify-20261018-item001-v1.json` — vext 板端判定归档

## 8. 覆盖边界（本轮 TEST_PASS 不声称的范围）

- 仅证明 ITEM-001 与本版本（commit dfbf43a）的已测范围：模型能力核验、真实 rknn_query 探针、稳定 reason、配置汇总。
- 未执行真实推理（无前向/精度/延迟数字）、未连接相机、未验证训练/转换、未做 P5 运行期接线（ITEM-012）、未替换 SimDetector（ITEM-009）——均为交付文档声明的边界。
- 主机侧仅静态审阅与脚本 dry-run；所有功能结论来自板端实测。
