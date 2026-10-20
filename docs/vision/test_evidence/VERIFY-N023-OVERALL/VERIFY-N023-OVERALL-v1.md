# VERIFY-N023-OVERALL — n016 视觉推理交付(ITEM-013)整体复验(独立验证, v1)

- Capability: `software_tester.software_verification`
- 轮次: `VERIFY-20261020-n023-v1`(2026-10-20 执行)
- 被测交付: n016 `computer_vision_engineer.vision_inference_implementation` r3(commit `a45e49e`,
  自检 17 判例; 交付文件 `app/src/core/{src/vision_selftest.cpp,include/railhmi/core/vision_selftest.h,tools/selftest_probe.cpp}`、
  `app/tests/core/test_vision_selftest.cpp`、`app/tools/board_vision_selftest.py`)
- 被测版本核对: `a45e49e` 之后 HEAD=f8a5040 间 **app/ 产品源码零变更**(4 个后续 commit 全为验证/评审
  证据文件; worktree 干净, 逐文件 sha256 与 a45e49e 一致——表面 DIFF 全为 core.autocrlf EOL 归一, 非
  内容变更)。本轮属整体复验轮: 不因"上轮 PASS 后源码未变"而免跑, 按当前树独立复跑交付入口全链。
- 结论: **TEST_PASS**(全口径独立复验通过, 无新缺陷)
- 环境: RK3588 `192.168.0.103`(板端指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`,
  与实现轮/既往验证轮一致), Linux 6.1.141 aarch64
- 本轮无相机、无真实 RKNN 前向: 与交付自检口径一致(no_device); 真实模型前向证据见 ITEM-009/012, 不互替;
  真机精度/人工视觉确认为交付声明 pending(TC-T-SELF-03), 不由本轮声称通过。

## 1. V-RUN: 交付入口在当前树独立复跑(fresh stamp `verify-20261020-n023-v1`)

`app/tools/board_vision_selftest.py --keep-remote` 一次有界会话: 上传 app(含 CoreTests/MediaCheck 工程)
+ yolov8s-seg/yolov5s 两模型 → 板端构建 → core-tests → 自检确定性×2(板端 sha256 比对)→ 归档 verbose
JSON → `--list` → media 自检 → 产物取回。transcript 9 步 exit 全 0; 8 个关键 `*_EXIT` token
(BUILD_CORE/BUILD_MEDIA/TEST/DET1/DET2/SELF/LIST/MEDIA_SELFTEST)独立解析全 0。V-RUN 耗时 237s。

- core JUnit: `tests="1452" failures="0"`(impl r3 恒等口径)
- 自检: 17/17 PASS(`type=summary total=17 passed=17 failed=0`), media 自检 `RESULT: PASS failures=0`
- 确定性: det1==det2 逐字节, 且与 impl r3 归档 `…deterministic.json` **逐字节恒等**
  (sha256 前 16 位 `9bfbd80709a52c0e` 双侧同值)

## 2. V-EXT: 交付边界自有夹具(v2 夹具逐字节复用, 板端 g++ 直编, 18/18 PASS)

`verify_item013_vext_v2.cpp` 链 `build-core/src/core/librailhmi_core.a` 直驱 `run_vision_selftest()`
+ SemanticAdapter/envelope/detect/seg/distance/debug 接口面; 本轮 fresh 日志 `verify-item013-n023-vext-v2.log`:
v01–v18 全 PASS, `vext RESULT pass=18 fail=0`, `VEXT_RUN_EXIT=0`。判据面与 v2 轮逐字节一致, 无改动。

## 3. V-NEG: 反转取证(v1 呈证夹具逐字节复用, `verify-item013-n023-vext-v1-replay.log`)

v1 夹具(按 16 判例/FI-AI-12 缺席构造)在当前修复树上预期 FAIL 兑现: 恰好
`v01/v02/v03` 三条 FAIL + `vext RESULT pass=13 fail=3` + `VEXT1_RUN_EXIT=1`, 机制判据(v04–v09)照常
PASS——排除"夹具/树整体异常"的替代根因, 3 条 FAIL 恰好对应 D-01 修复面。正判据 18/18 +
反转 fail=3 双向证明判据咬合, 非空转。

## 4. V-CMP: 独立口径(`_verify_item013_cmp_overall.py` fresh 41/41; dry-run 对 impl r3 归档 39/39)

不采信交付 acceptance 行, 独立核对(判据集与 v2 轮 41 检逐条一致, 仅 fresh stamp 换为本轮):

- 确定性/跨轮恒等: fresh det1==det2, 且 fresh det1 与 impl r3 `…deterministic.json` 逐字节恒等;
- JSON 结构: summary 17/17/0、归档含 timestamp_ms、确定性无、pending 4 条、cases=17、
  两份 JSON 无 `192.168.`/`rtsp://`/`TRAIN_`(脱敏);
- FI 矩阵: FI-AI-01/02/04/07/10/12 每项均在某判例 requirement 中(含 D-01 修复面 FI-AI-12);
- 关键数值独立重推导: `x=0.498437`、mask 640×640 positive=14400、pad_y=140 scale=0.5 bytes=1228800、
  two_stage `mask_hit=1 alarm_hits=1`、envelope accepted=2+三类各 1、`AI 不可用` 精确文本、
  `mismatched_items=0`(O-1 口径)、FI-AI-12 判定面 `blocked=1 mapped=1 detections=1 events=1
  event_type=person_on_rail input_boxes=2`;
- JUnit 1452/0、selftest 域判例 ≥35、防回归判例 `selftest_covers_FI-AI-12` 在 XML 实际在场;
- `--list` 17 id 与 JSON cases 顺序恒等; media 自检 PASS 行独立解析。

## 5. 覆盖边界与结论

- 本轮为 ITEM-013 交付(自检入口+夹具+归档)的整体复验, 覆盖 G13 TC-T-SELF-01..03 判据面;
- 无相机/无 RKNN 前向/真机精度/4-6 路并发/热稳态为交付声明 pending, 不由本轮声称通过;
- 上轮登记观察项 O-1(`mismatched_items=0` early-return 语义)/O-2(无参数等同 `--selftest`)/
  O-3(media 自检失败路径用例的预期噪声)本轮口径不变, 维持非缺陷登记。

## 6. 过程留痕(验证侧工具修正, 非产品缺陷)

- v1 过程轮驱动 `token_exit` 用 `endswith` 反向匹配写反(实际行形如 `BUILD_CORE_EXIT=0` 以 token
  开头), 8 个 token 全 None 退出 9——修正为 `startswith` 前缀解析并对已归档主 log 离线复核
  8/8 后重跑; 重跑轮 V-RUN 独立复跑 237s 全绿, 两轮产物均保留。
- `DRIVER_EXIT` 经 `| tail` 管道取的是 tail 退出码(ITEM-010 教训②变体)——修正后驱动退出码直接
  回显, 不经管道判定。
- 收尾: 板端保留树按名清理 `ITEM013_BOARD_CLEANED`; 凭据临时文件未留盘(CRED_TMP_ABSENT);
  本地 `__pycache__` 已清; app/ worktree 保持干净。

## 7. 原始文件清单(本目录 + SELFTEST 归档)

- `_verify_item013_run_overall.py` / `_verify_item013_cmp_overall.py`(驱动与 oracle, v2 适配版)
- `vision-item013-192_168_0_103-verify-20261020-n023-v1.transcript.json` / `.log`(V-RUN fresh)
- `…app_core-evidence_item013-{core-tests…xml, selftest…(log+json), selftest-det{1,2}…(log+json),
  selftest-list…txt, media-selftest…(log+json)}`(11 份产物)
- `verify-item013-n023-vext-v2.log`(18/18) / `verify-item013-n023-vext-v1-replay.log`(fail=3 反转)
- `…driver.log` / `…driver.transcript.json` / `_driver_console_r2.log`(驱动留痕)
- SELFTEST 归档: `vision-selftest-192_168_0_103-verify-20261020-n023-v1{,.deterministic}.json`

夹具源逐字节复用自 `docs/vision/test_evidence/ITEM-013/`(verify_item013_vext_v2.cpp /
verify_item013_vext.cpp), 本目录不另存副本; 上轮权威报告 VERIFY-ITEM-013.md(v2)与本报告互补。
