# VERIFY-ITEM-013 — 无相机自检与可归档夹具证据(独立验证)

- Capability: `software_tester.software_verification`
- 轮次: `VERIFY-20261021-item013-v1`(2026-10-20 执行,stamp 对齐实现轮 `20261021-item013-r2`)
- 被测版本: 实现方 r2(commit `c35f75f`,自检 16 判例,core JUnit `tests=1445 failures=0`)
- 结论: **TEST_FAIL**(1 条业务缺陷 D-01 交实现;其余全部独立口径通过)
- 环境: RK3588 `192.168.0.103`(指纹与实现轮一致 `SHA256:NzHFHZ0Z…`),aarch64,g++ 11.4.0
- 执行入口: 交付 runner `app/tools/board_vision_selftest.py --keep-remote` 独立复跑 + 自有夹具 `verify_item013_vext.cpp` 板端 g++ 直编 + 本地独立解析 `verify_item013_cmp.py`
- 本轮无相机、无真实 RKNN 前向: 与交付自检口径一致(no_device);真实模型前向证据见 ITEM-009/012,不互替。

## 1. V-RUN: 交付入口独立复跑

一次有界会话全步骤退出 0:上传 340 文件+2 模型 → build CoreTests/MediaCheck → core-tests
(`tests=1445 failures=0`,与 impl r2 恒等) → 自检确定性×2 → sha256 → 归档+verbose →
`--list` → media 自检(`RESULT: PASS failures=0`) → 10 份产物取回。

## 2. V-CMP: 独立口径(`verify_item013_cmp.py` 28/28)

不采信交付 acceptance 行,独立核对:

- **确定性**: fresh `det1`/`det2` 逐字节恒等,且 fresh det1 与实现 r2 归档 `…deterministic.json`
  **逐字节恒等**(跨轮 sha256 `af7227f3…` 同值)— 夹具判定跨轮/跨构建可复现。
- **JSON 结构**: `summary 16/16/0`、归档 JSON 含 `timestamp_ms`、确定性 JSON 不含、`pending` 4 条、
  JSON 无 `192.168.`/`rtsp://`/`TRAIN_`(脱敏)。
- **手指算术独立重推导**:
  - `detect.synthetic_box`: x=319/640=0.498437 逐位命中(DFL 峰值通道 47/63 → bw=10px,bh=13px)。
  - `seg.synthetic_mask`: 掩码 640×640、positive=14400(r2 修复后 DFL 峰值通道 47/63 →
    120px 框 + coeff[0]=1 / proto 通道 0 全 1 的点积逐位对应)。
  - `preprocess.synthetic_letterbox`: scale=0.5、pad_x=0、pad_y=140、
    bytes=1228800=640×640×3、r_mean>b_mean 保序(恒等对照)。
  - `envelope.*`: accepted=2、stale_gen/dup/ooo 各 exactly 1。
  - `availability.ai_stopped`: `AI 不可用` 精确文本。
  - `mismatched_items=0`: 结构识别层 early-return(vision_model_capability.cpp:452-458)的
    合法语义 — 夹具期望必须与该层实现一致,抽退出后的 mismatched 本就为空。与 r1 返工说明(a)(b)一致。
- JUnit 1445/0;selftest 域判例(名字含 `selftest`)≥30;`--list` 16 id 与 JSON cases 顺序恒等。
- media 自检 `RESULT: PASS failures=0`(ITEM-009..012 路径回归不回退)。
- CLI 负例: `unknown-arg → exit 2`、`--help → exit 0`;无参数运行照常跑自检(参数面容忍,
  探针 main() 无 no-action 分支 — 观察项 O-2,非缺陷)。

## 3. V-EXT: 自有夹具(`verify_item013_vext.cpp` 16/16,板端 g++ 直编,两轮恒等)

- v01 交付 16 判例全绿回归;
- v04–v09 **FI-AI-12 未知 label 拦截机制在位证明** — `SemanticAdapter` 正确完成
  `blocked_unknown_label=1`、未知类零 alarm、person 照常 mapped、拦截计数在 detail 行可见
  (`in=2 mapped=1 candidates=1 unknown=1`);V10: `blocked_not_allowed` 独立计数互不串扰。
- v11–v12 JSON pending/逃逸回归;v13–v14 envelope 单 gate 语义
  (跨相机同 seq=duplicate_seq、不可用 envelope 照常入队);v15–v16 距离/调试占位回归。
- 夹具过程修正留痕: 首版 v2 用 `std::vector<EnvelopeBox>`/`in.boxes={p,u}` 编译错误
  (契约是 `const vector<DetectBox>*` + `in_roi_flags`);v13/v14 首版假设"每相机独立 gate"
  是验证侧越权假设(交付为单相机单 gate 口径),按实现契约值重推导后通过。

## 4. 业务缺陷 D-01 → 交实现

**FI-AI-12 自检覆盖缺口(coverage gap 即缺陷)** — severity: P2
- **版本**: r2 (commit c35f75f,vision_selftest.{h,cpp} / selftest_probe.cpp / test_vision_selftest.cpp)
- **判据来源**: `VISION_TEST_DESIGN.md` §G13 `TC-T-SELF-01`:"无设备运行 --selftest;
  **FI-AI-01/02/04/07/10/12 各路径覆盖** + 结构化 JSON 输出"。
- **实际观察**:
  - 头文件声明对 FI-AI-12 的覆盖承诺(TC-T-PUB/SELF-01 判据域范围引用):
    `app/src/core/include/railhmi/core/vision_selftest.h:8` 声称覆盖
    `FI-AI-01/02/04/07/10/12`;
  - 实际 16 判例的 requirement 字段只含 FI-AI-01/02/04/07/10/**11** — 无任何判例引用 FI-AI-12;
  - 归档 JSON/verbose 运行日志(impl r2 与 fresh 轮)同样零 FI-AI-12 出现
    (cmp `FI-AI-12 not present in fresh run log`);
  - FI-AI-12 = 未知/未映射 label 拦截(零报警 + 拦截计数可见),机制本体在
    `vision_semantic_adapter.cpp:59-72`(`blocked_unknown_label`)且 VEXT 已独立证明其语义正确;
    单测域在 `test_vision_semantic_adapter.cpp:97`(ITEM-007 既有覆盖)、
    `test_vision_duty_capability.cpp:384`(ITEM-010 既有覆盖),**但都不是 G13 要求的
    `--selftest` 套件内覆盖**。
- **期望**: TC-T-SELF-01 明确要求自检套件内含 FI-AI-12 路径;头注释声称覆盖但套件内容缺席,
  "宣称与内容不符"本身即交付一致性缺口。
- **建议修复边界**(实现能力):
  - 追加 1 判例(建议 id `semantic.unknown_label_intercepted`,area `semantic` 或 `envelope`):
    复用已证明的路径 — 构造含未知 label 的 EnvelopeBox via
    `build_envelope`(label=`"weird_class"`、in_roi=1)+ `SemanticAdapter` 白名单
    `{"person","obstacle"}`,断言 `blocked_unknown_label==1 && mapped==1 &&
    detections 全 person && 零 alarm`(v05–v09 的语义已可正向复用);
  - 同步: 头注释 5:8 移除或保持 FI-AI-12(实现补上后这句才成立);
  - 覆盖缺口不影响其余 16 判例的有效性 — 全部保留,夹具构造机制可复用。
- **原始证据**: 本文 §5 文件清单(a)。vext v02/v03 判例名即呈证判据。

## 5. 验证覆盖边界

- 本轮无相机、无 RKNN 前向 — 与交付自检一致;真机精度/检出率/人工视觉确认为 G13 `pending`
  四条(TC-T-SELF-03),不由本轮声称通过;实施者已如实在 JSON/EVIDENCE 标注。
- 独立板端复跑用同一交付 runner,是第三方背书;所有关键数值独立重算,不采信交付自报。

## 6. 原始文件清单(本目录 + SELFTEST 归档)

V-RUN(fresh 复跑):
- `vision-item013-192_168_0_103-verify-20261021-item013-v1.transcript.json` / `.log`
- `….app_core-evidence_item013-core-tests-….xml` (tests=1445 failures=0)
- `….app_core-evidence_item013-selftest-….log` / `.json`、`…-selftest-det{1,2}-….(log+json)`
- `…-selftest-list-….txt`、`…-media-selftest-….(log+json)`

V-EXT/夹具:
- `verify_item013_vext.cpp` + `verify-item013-vext-v1.log`(板上含 V2/V3 FI-AI-12 呈证运行)
- `verify_item013_cmp.py`(本地独立口径)

验收登记观察项:
- O-1: `model.tensor_mismatch` 的 `mismatched_items=0` 是结构识别 early-return 语义
  (判定在 mismatched 构造之前);不是缺陷,夹具若期望非空则应走字段级不匹配夹具路径。
- O-2: `railhmi-vision-selftest` 无参数运行等同 `--selftest`(main() 无 no-action 分支);
  参数面容忍行为,与部分工件登记口径不矛盾,不判缺陷。
- O-3: media 自检日志包含板端 RKNN 报错行 (`rknn_init, load model failed!`) —
  预期行为(media 探针覆盖真实/失败两种路径;退出码 0 与 `RESULT: PASS` 确认自检整体通过)。
