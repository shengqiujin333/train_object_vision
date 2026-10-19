# ITEM-006 验证轮 — 结果关联与发布（VERIFY-20261019-item006-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r3（commit b49ef2b，stamp `20261019-item006-r3`）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-006`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_envelope.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item006-v1` | 全新板端会话：上传 297 文件→CoreTests 构建+测试→selftest→10 fixtures→config→取回 25 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item006_compare.py`（28 判据，不采信交付 RESULT/acceptance 行） | fresh vs 实现 r3 归档逐字段语义比对 + 独立重推导 |
| V-EXT | 验证自有扩展夹具 `verify/vext_item006.cpp` 板端 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），39 判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item006_vext.py` | 断言 core-tests/selftest 未覆盖的验收边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现 r3 同板同指纹），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。

## 2. 独立复跑结果（fresh vs 实现 r3）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 真实退出码从 `*_EXIT` token 解析（外层 shell 恒 0） | build-core/core-tests/selftest/10×fixture/config 全 0；`--keep-remote` 无 cleanup 步骤 |
| T2 逐行恒等 | selftest/10 fixtures/config 共 12 份 `.log` fresh vs impl r3 | 12/12 全文逐字节恒等 |
| T3 core 单测 | JUnit 独立解析（`\sname=` 锚防 classname 子串匹配） | fresh `tests=1118 failures=0`；判例名多重集与 r3 恒等（1118=1118）；envelope/queue/latency/unavailable/empty 域 53 判例 |
| T4 TC-T-PUB-01 | ready envelope 身份链独立重推导 | `(s-env,CAM-01,gen1,seq1)` + capture=1000000/result=1033000 + **latency_ms=33 独立重算一致** + `model_version=rknn-2.3.2/yolov5s`；box `track_id=7`；mask `box_index/64x48/stride 64/rail/plane_present=true` |
| T5 TC-T-PUB-02 | 三类拒绝独立计数（不采信 RESULT，独立解析 stats JSON） | identity：submitted=4 accepted=1 rejected=3 且 stale_generation/duplicate_seq/out_of_order_seq 各 1；逐类 fixture 单独运行各类只计自己（stale_only/duplicate_only/ooo_only）；**拒绝项不入队**（queued=1=仅首个被接受项） |
| T6 TC-T-PUB-03 | 空框 vs 不可用可区分 | empty：`ready+READY+box_count=0`；unavailable：`unavailable+AI_UNAVAILABLE+box_count=0`；seg：`unavailable+SEG_UNAVAILABLE`——availability/reason 二维可区分；no_track `track_id=-1` 不冒 0 |
| T7 TC-T-PUB-04 | 有界队列丢最旧（depth=2 独立重推导） | push seq1..5 → queued=5、queue_dropped=3（5−2 独立推导一致）、pop_first_seq=4（保留最新 {4,5}）；全部 accepted（push 恒真非阻塞） |
| T8 配置 | `--config` 深度来源 | `app.ai.queue_depth=2 depth_used=2 (from app.ai.queue_depth)`，与 `app/config/app.yaml` L28 一致 |
| T9 脱敏 | fresh 25 份产物 + 板端 core-evidence 双目录凭据语义扫描 | 0 命中 |

比对脚本最终口径 `COMPARE SUMMARY passed=28/28`（退出码 0，归档 `verify/item006-compare-verify-20261019-item006-v1.json`）。

## 3. 验证自有扩展夹具（vext 39 判例，板上全 PASS）

CoreTests/selftest 未覆盖的边界（`verify/vext_item006.cpp`，g 编号；39 check 调用、无分支二选一）：

- 世代边界：g01 同 session 更大 generation 接受且 seq 基线重置（gen1→gen2 后 seq0 不乱序）、g02 空构造 publisher 首个 submit 采纳 session + 换 session 拒绝、g03 旧代拒绝后同代新 seq 仍接受（拒绝不破坏 gate 状态）
- 乱序/重复判定序：g04 等值=duplicate、倒退=out_of_order（连续两次倒退均计）、前进=接受
- 延迟边界：g05 999µs 截断为 0 / 1000µs=1ms / 0/0 / 负值钳 0；g06 含排队语义锚定（1.5s 队列+0.5s 推理=2000ms）
- 队列边界：g07 depth=1 逐出全部旧项只留最新（dropped=4）、g08 push 恒真非阻塞（超载 submit 返回 true + 丢最旧计数）、g09 unavailable envelope 照常入队且分类计数（unavailable/empty_boxes/non_empty 三类）、g10 popped 与 queue_dropped 计数一致性 + 空 pop 不计
- JSON/reason：g11 `track_id:-1` 原样输出且无 `track_id:0`、g12 自定义 reason 覆盖 token + 三默认 token 稳定（READY/AI_UNAVAILABLE/SEG_UNAVAILABLE）、g13 ready 空框 vs unavailable JSON 输出可区分、g14 无 boxes 指针 ready 构造框/掩码齐空、g15 track_ids 短表缺省段填 -1、g16 stats JSON 全计数键在场

驱动判定（`vext-item006-…json`）：9/9 检查 PASS — V1 板端 g++ 直编 1.2s、V2 板上 39/0 + 判例名集合与夹具源恒等（src=39 board=39）、V3 CLI 负例 unknown-arg=2/no-action=2/help=0/未知 fixture=2、V4 `--selftest` 与 `--fixture ready` 同参两轮 sha256 恒等、V5 板端泄漏 0、V6 板端 `/tmp/railhmi_vision_item006_verify-20261019-item006-v1` 树与 vext 构件清理确认（`No such file` 即删除成功确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 携带 (session_id,camera_id,generation,seq) 关联键 | T4：ready fixture 四键独立重推导；vext g01-g03 世代边界 |
| capture_mono_us/result_mono_us、latency_ms（完成减接收且含排队） | T4：latency=33 独立重算；vext g05 全边界 + g06 排队语义锚定 |
| availability(ready\|unavailable)+reason | T6：三 reason token + 自定义覆盖（vext g12）；vext g09 unavailable 照常发布 |
| model_version | T4：`rknn-2.3.2/yolov5s` 原样携带 |
| 带 track_id 的 boxes | T4/T6：track_id=7 保留、未知 -1 不冒 0；vext g11/g15 |
| 掩码引用 | T4：box_index/尺寸/stride/kind/plane_present；零拷贝语义由单测锚定（掩码像素内容不序列化） |
| 拒绝旧 session/generation、重复 seq、乱序结果并分别计数 | T5：identity + 三单类 fixture 独立计数互不串扰、拒绝项不入队；vext g02/g03/g04 |
| 结果队列有界、超载丢最旧并计数 | T7：depth=2 丢 3 保留最新；vext g07/g08 深度 1/2 推导一致 |
| AI 不可用 → unavailable+reason+空框；空框与不可用可区分 | T6：两种输出在 availability/reason/box_count 上可区分；vext g13/g14 |
| 陈旧/重复/乱序结果被丢弃且有计数（用户可观察） | T5 stats JSON 十三计数键齐备（vext g16）；envelope_stats_json 观察面 |

## 5. 观察项（不阻塞，交后续参考）

- O-1 `EnvelopePublisher` 计数器与 `FrameGate` 为单生产者/单消费者假设（实现文档 §5.3 已声明）；多线程 submit 的加锁与 handoff 归 ITEM-009，本轮未测竞争。
- O-2 `MaskRef.plane` 零拷贝指针生命周期由调用方保证（实现文档 §5.2 已声明）；envelope 经队列传递时所有权策略须在 ITEM-009 落实（JSON 只输出 `plane_present`，不序列化像素，本轮已锚定该输出面）。
- O-3 真实 RKNN 前向未接入（本增量用夹具 envelope 归属语义，实现文档 §6.1 声明归 ITEM-009）；`latency_ms` 为字段计算口径验证，不代表实测推理延迟。

## 6. 验证侧过程记录（自查修正，无产品缺陷误报）

1. V-CMP 首轮 `C2_junit_envelope_domain_present` 失败为验证侧构造错：testcase 提取正则 `name="…"` 未加空白锚，命中 `classname="railhmi-core"` 子串使"域名"集合全部错收——加 `\s` 锚后 53 判例（ITEM-004 JUnit 属性序教训④同源变体）。
2. vext 首轮板端编译失败：`p.stats().queue_dropped()` 把 `EnvelopeStats` 的 long long 字段当 `EnvelopePublisher` 方法调用——g10 改为字段/方法双口径断言（板端 g++ 直报比离线审查快，既有教训）。
3. vext 第二轮 g10 失败为验证侧期望算术错：4 push 入 depth2 应丢 2（push3 弹 seq1、push4 弹 seq2），首版写 1——离线重推导后修正（ITEM-019"全推入 vs 驻留"教训同源）。

## 7. 证据清单（本目录，`verify-20261019-item006-v1` 标记）

- `vision-item006-192_168_0_103-verify-20261019-item006-v1.transcript.json` / `.log` — 独立复跑结构化/人类可读 transcript
- 同名 `app_core-evidence_item006-*` 25 份 — core JUnit、selftest log/json、10×fixture log/json、config log/json
- `verify/verify_item006_run.py`、`verify/verify_item006_compare.py`、`verify/verify_item006_vext.py`、`verify/vext_item006.cpp`、`verify/item006-compare-verify-20261019-item006-v1.json` — 验证自有驱动/比对/扩展夹具/比对归档
- `vext-item006-verify-20261019-item006-v1.stdout.log` / `.json` — vext 板端判定归档

## 8. 覆盖边界（本轮 TEST_PASS 不声称的范围）

- 仅证明 ITEM-006 与本版本（commit b49ef2b）的已测范围：InferenceEnvelope 关联/接纳/发布纯逻辑（夹具 envelope 与合成提交序列）。
- 未执行真实 RKNN 前向、未连接相机、未测多线程 submit/pop 竞争与真实队列背压、未做 P5/调度线程接线——均为交付文档声明的边界（归 ITEM-009 及后续）。
- 主机侧仅静态审阅与 oracle dry-run；所有功能结论来自板端实测。
