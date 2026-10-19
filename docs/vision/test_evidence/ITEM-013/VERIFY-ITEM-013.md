# VERIFY-ITEM-013 — 无相机自检与可归档夹具证据(独立验证,复核轮 v2)

- Capability: `software_tester.software_verification`
- 轮次: `VERIFY-20261021-item013-v2`(2026-10-20 执行,_stamp 对齐实现轮 `20261021-item013-r3`)
- 被测版本: 实现方 r3(commit `a45e49e`,自检 **17** 判例,core JUnit `tests=1452 failures=0`)
- 结论: **TEST_PASS**(v1 的 D-01 修复在位且经反转取证证明判据咬合;其余口径全部独立复验通过)
- 环境: RK3588 `192.168.0.103`(指纹与实现轮一致 `SHA256:NzHFHZ0Z…`),aarch64,g++ 11.4.0
- 执行入口: 交付 runner `app/tools/board_vision_selftest.py --keep-remote` 独立复跑
  + 自有夹具 `verify_item013_vext_v2.cpp`(呈证协议翻转)板端 g++ 直编
  + 反转取证 `verify_item013_vext.cpp`(v1 夹具逐字节复用,预期 3 FAIL)
  + 本地独立解析 `verify_item013_cmp_v2.py`(41/41;drcy-run 对 impl r3 归档 39/39 非空转)
- 本轮无相机、无真实 RKNN 前向: 与交付自检口径一致(no_device);真实模型前向证据见 ITEM-009/012,不互替。

## 0. 相对 v1 复核的变化

| 项 | v1(verify-...-v1) | v2(本轮) |
|---|---|---|
| 被测 | r2: 16 判例,FI-AI-12 缺席 | r3: 17 判例,+`semantic.unknown_label_intercepted`(FI-AI-12) |
| D-01 呈证判据 | v02/v03 断言 FI-AI-12 **不在场**(PASS→缺陷呈证) | v2 夹具翻转: 断言 FI-AI-12 **在位**;v1 夹具逐字节复用做反转取证(修复树上必然 FAIL) |
| cmp oracle | 28 检,summary 16 | 41 检,summary 17 + requirement 六 FI 矩阵 + JUnit `selftest_covers_FI-AI-12` 判例存在 |

## 1. V-RUN: 交付入口独立复跑(fresh, verify-20261021-item013-v2)

一次有界会话全步骤退出 0(BUILD_CORE/ BUILD_MEDIA/ TEST/ DET1/ DET2/ SELF/ LIST/ MEDIA_SELFTEST 全 `*_EXIT=0`):
上传 app+2 模型 → 构建 → core-tests `tests=1452 failures=0`(impl r3 恒等口径,JUnit 头 `tests="1452" failures="0"`,total testcase 1452)
→ 自检确定性×2 → 板端 `sha256sum` 两列同值 `9bfbd80709a52c0e…det1/det2` — 与 impl r3 归档确定性 JSON **逐字节恒等**
→ 归档 verbose JSON → `--list` 17 id → media 自检 `RESULT: PASS failures=0` → 产物取回。

## 2. V-EXT: 自有夹具 v2(`verify_item013_vext_v2.cpp` 18/18,板端 g++ 直编,`VEXT_RUN_EXIT=0`)

- v01 交付 17 判例全绿(total=17 failed=0,r2 时为 16 — 数量判据同步演进);
- **v02/v03/v04/v05 = D-01 修复复核反转判据**: FI-AI-12 现在在 requirement 中在位(`FI-AI-10 present=1 FI-AI-12 present=1`),
  拦截判例 `semantic.unknown_label_intercepted` PASS 且判定面完整(`blocked=1 mapped=1 events=1 detections=1 event_type=person_on_rail`),
  JSON 归档面可见 FI-AI-12(与头注释一致性成立 — v1 呈证的矛盾已消除);
- v06–v11 FI-AI-12 机制在位回归(继承 v1 v04–v09,逐判据保留): `blocked_unknown_label=1`、仅 person mapped、零 alarm、
  每调用计数不累积、detail 行计数可见(`in=2 mapped=1 candidates=1 unknown=1 no_track=0`);
- v12/v14/v15/v16/v17/v18 envelope 单 gate/JSON pending/距离/调试占位回归(v1 v10–v16 等价保留);
- 夹具过程修正留痕: 首版写入产生 `FAIL_TOKEN` 占位串残留与一次畸形 Write 调用(gate 拒绝未产生文件)——
  写后立即 `python` 完整性复核 + brace/paren 深度检查,上板前清零(ITEM-004 Write 损坏教训的执行)。

## 3. V-NEG: 反转取证(v1 夹具逐字节复用,`verify-item013-vext-v1-replay.log`)

v1 呈证夹具在修复树上(`build-core/src/core/librailhmi_core.a` r3)预期 FAIL 并兑现:

```
vext FAIL v01_delivered_selftest_16_pass :: total=17 failed=0      (数量判据按 16 构造,修复后 17)
vext FAIL v02_selftest_fi_ai_12_not_in_requirements :: FI-AI-10 present=1 FI-AI-12 present=1
vext FAIL v03_selftest_json_no_fi_ai_12 :: JSON 也无 FI-AI-12(与头注释声称矛盾)
vext RESULT pass=13 fail=3   VEXT1_RUN_EXIT=1
```

v1 六条 v01–v09 中的机制判据(v04–v09)照常 PASS — 排除"夹具/树整体异常"的替代根因,3 条 FAIL 恰好对应 D-01 修复面。
正判据(v2 18/18)+ 反转(v1 fail=3)双向证明判据咬合,非空转。

## 4. V-CMP: 独立口径(`verify_item013_cmp_v2.py` fresh 41/41;drcy-run 对 impl r3 归档 39/39)

不采信交付 acceptance 行,独立核对:

- **确定性/跨轮恒等**: fresh det1==det2 逐字节恒等,且 fresh det1 与 impl r3 `…deterministic.json`
  **逐字节恒等**(sha256 `9bfbd807…` 同值)— 新判例进入确定性 JSON 后仍可复现。
- **JSON 结构**: `summary 17/17/0`、归档 JSON 含 `timestamp_ms`、确定性 JSON 不含、`pending` 4 条、
  两份 JSON 均无 `192.168.`/`rtsp://`/`TRAIN_`(脱敏)、`cases=17`、`FI-AI-12` 在归档 JSON 中在位。
- **requirement 六 FI 矩阵**: FI-AI-01/02/04/07/10/12 每项均在某判例 requirement 中(v1 缺口关闭)。
- **新判例证据独立断言**: `semantic.unknown_label_intercepted` evidence
  `blocked_unknown_label=1 / mapped=1 / detections=1 / events=1 / event_type=person_on_rail / input_boxes=2`。
- **手指算术独立重推导**(继承 v1,全部保持): x=319/640=0.498437、mask 640×640 positive=14400、
  pad_y=140 scale=0.5 bytes=1228800、two_stage `mask_hit=1 alarm_hits=1`、envelope accepted=2+三类各 1、
  `AI 不可用` 精确文本、`mismatched_items=0` early-return 语义(O-1 口径保持)。
- JUnit `1452/0`;selftest 域判例 40;`selftest_covers_FI-AI-12` 防回归判例在 XML 实际出现。
- `--list` 17 id 与 JSON cases 顺序恒等。
- media 自检 `RESULT: PASS failures=0`(ITEM-009..012 路径回归不回退)。

## 5. D-01 复核结论

- **修复在位**: 新增判例 `semantic.unknown_label_intercepted`(FI-AI-12/TC-T-SEM-01)进入 `--selftest` 套件,
  头注释承诺与套件内容一致;单测新增六个 FI 覆盖存在性断言防复发(`selftest_covers_FI-AI-12` 在 fresh JUnit 实际在场)。
- **机制零回归**: SemanticAdapter 白名单拦截/计数/零报警路径的既有独立判据(v1 v04–v09)逐字节保留全绿。
- **与其余 16 判例兼容性**: 全部保留,evidence/requirement/确定性口径不变(det1 与 r2..r3 语义面一致,跨版本字节比较
  不适用由 impl §6.3 已声明口径,本轮以"同版本两次运行恒等"为准——det/det2 恒等 + 与 r3 恒等双验)。

## 6. 过程留痕与验证侧修正(非产品缺陷)

- 驱动 `verify_item013_driver_v2.py` 以 `V-NEG unexpected: v1 fixture passed` 报 exit=8 结论——
  **驱动自身的判据 bug**: v1 夹具实际按 VEXT1_RUN_EXIT=1 完整 FAIL(pass=13 fail=3,证据日志全留),
  驱动按 rrun 外层 exit 判定(ITEM-002 教训①同源复现: 外层 shell `echo EXIT=$?` 恒 0,
  真实退出码在 `_EXIT` token)。replay 日志为准,反转取证有效;驱动退出码判据缺陷登记为验证工具修正项,不影响业务判定。
- 本轮两条产物路径: `verify-item013-vext-v2.log`(v2 夹具)、`verify-item013-vext-v1-replay.log`(反转取证)。
- 板端收尾: 业务判定全绿后按名清理保留树(`PRE 0 → ITEM013_BOARD_CLEANED → POST 2`),
  补做于驱动 exit=8 非业务失败之后;驱动设计的"失败轮保留树"闸门未被业务触发,收尾在能力收口前完成。
- 凭据临时文件 `.verify_item013_cred.tmp` 已确认未留盘(驱动删除步骤 + 复核);板端凭据仅进程环境。

## 7. 验证覆盖边界

- 本轮无相机、无 RKNN 前向 — 与交付自检一致;真机精度/检出率/人工视觉确认为 G13 `pending`
  四条(TC-T-SELF-03),不由本轮声称通过;交付 JSON/EVIDENCE pending 标注经 cmp 独立核对在位。
- 独立板端复跑用同一交付 runner,是第三方背书;所有关键数值独立重算,不采信交付自报。

## 8. 原始文件清单(本目录 + SELFTEST 归档)

V-RUN(fresh 复跑 v2):
- `vision-item013-192_168_0_103-verify-20261021-item013-v2.transcript.json` / `.log`
- `….app_core-evidence_item013-core-tests-….xml` (tests=1452 failures=0)
- `…-selftest-…(log+json)`、`…-selftest-det{1,2}-…(log+json)`、`…-selftest-list-….txt`
- `…-media-selftest-…(log+json)`

V-EXT/V-NEG:
- `verify_item013_vext_v2.cpp` + `verify-item013-vext-v2.log`(18/18)
- `verify_item013_vext.cpp`(v1 逐字节复用)+ `verify-item013-vext-v1-replay.log`(fail=3 反转)
- `verify_item013_cmp_v2.py` + `verify_item013_driver_v2.py`(驱动,含退出码判据缺陷留痕)
- `vision-item013-192_168_0_103-verify-20261021-item013-v2.driver.transcript.json`

验收登记观察项(继承 v1,口径不变):
- O-1 `mismatched_items=0`: 结构识别 early-return 语义;
- O-2 无参数运行等同 `--selftest`;
- O-3 media 自检日志的 `rknn_init, load model failed!` 是失败路径用例预期噪声。
