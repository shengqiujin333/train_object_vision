# ITEM-013 证据 — 无相机自检与可归档夹具证据

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-013`
- 轮次标记: `20261021-item013-r3`（终轮：修复验证轮 D-01 后重跑）；r2 为上一轮、r1 为最早返工留痕
- 日期: 2026-10-21
- 实现文档: `docs/vision/implementation/ITEM-013_SELFTEST.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_selftest.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）
- **无设备自检**：本轮未连接相机、未执行 RKNN 前向；自检为纯夹具/纯逻辑（`no_device=true`）

## 1. 本轮执行步骤（一次有界会话）

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ + 两个 `.rknn` 资产（仅 media 回归用，自检不加载） | 0 | 仅 `/tmp/railhmi_vision_item013_20261021-item013-r3` |
| 构建 CoreTests / MediaCheck | `bash build.sh CoreTests` / `MediaCheck` | 0 | 通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1452 failures=0`（ITEM-012 的 1412 + 本增量 40） |
| 自检（确定性 ×2） | `railhmi-vision-selftest --selftest --deterministic`（两次） | 0×2 | 输出逐字节相同 |
| 确定性核对 | `sha256sum det1.json det2.json` | 0 | 同一哈希 `9bfbd807…` |
| 自检（归档、verbose） | `railhmi-vision-selftest --selftest --verbose --json …` | 0 | `17/17 PASS` + 归档 JSON |
| 判例清单 | `railhmi-vision-selftest --list` | 0 | 17 个 id |
| media 回归自检 | `railhmi-vision-pipeline --selftest` | 0 | PASS（30/30） |
| 归档副本 | 本地写入 `docs/vision/test_evidence/SELFTEST/` | — | 2 份 JSON（r3） |
| 清理 | `rm -rf /tmp/railhmi_vision_item013_20261021-item013-r3` | 0 | `cleaned` |

## 2. 自检判例结果（17/17 PASS）

```
type=selftest schema=railhmi.vision.selftest.v1 no_device=true environment=ai-host-fixture cases=17
[PASS] model/model.missing :: absent model is not ready (MODEL_MISSING) [FI-AI-01/TC-T-CAP-02]
[PASS] model/model.tensor_mismatch :: seg-like descriptor does not satisfy yolov5s spec [FI-AI-02/TC-T-CAP-03]
[PASS] model/model.ready :: detect + seg descriptors match declared specs [TC-T-CAP-01]
[PASS] preprocess/preprocess.synthetic_letterbox :: … [TC-T-PRE-01/03/04]
[PASS] preprocess/preprocess.empty_frame :: empty/invalid frame rejected explicitly [FI-AI-04/TC-T-PRE-05]
[PASS] detect/detect.no_detection :: all-zero tensors -> ready with empty boxes (no error) [TC-T-DET-04]
[PASS] detect/detect.synthetic_box :: synthetic person box decoded at expected normalized coordinates [TC-T-DET-02/FI-AI-11]
[PASS] seg/seg.synthetic_mask :: 13-tensor seg fixture -> 1 rail box with full-size binary plane [TC-T-SEG-01/02/03]
[PASS] two_stage/two_stage.person_on_mask :: person center inside rail mask -> alarm hit [TC-T-TWOPHASE-01]
[PASS] two_stage/two_stage.mask_missing_unavailable :: missing mask -> unavailable, never 'rail safe' [FI-AI-07/TC-T-TWOPHASE-03]
[PASS] envelope/envelope.stale_duplicate_out_of_order :: … [FI-AI-10/TC-T-PUB-02]
[PASS] envelope/envelope.unavailable_vs_empty :: … [TC-T-PUB-03/FI-AI-07]
[PASS] semantic/semantic.unknown_label_intercepted :: unknown label intercepted (no alarm); person still mapped and confirmed [FI-AI-12/TC-T-SEM-01]
[PASS] availability/availability.ai_stopped :: worker stopped -> AI unavailable [TC-T-DISP-02]
[PASS] availability/availability.duty_unsupported :: … [TC-T-MIS-01/02]
[PASS] availability/availability.distance_uncalibrated :: … [TC-T-DIST-01]
[PASS] debug/availability.debug_unknown_not_zero :: … [TC-T-DBG-03]
type=summary total=17 passed=17 failed=0
RESULT: PASS failures=0
```

## 3. D-01 修复（FI-AI-12 覆盖缺口）

**验证轮发现**：头注释声称覆盖 `FI-AI-01/02/04/07/10/12`，但 16 个判例的 `requirement` 无任何 `FI-AI-12`
（未知/未映射 label 拦截）——宣称与内容不符（`VERIFY-ITEM-013.md` §4）。

**修复**：新增判例 `semantic.unknown_label_intercepted`：

- 输入：同一 envelope 内 `person` + `weird_class`（两框 `in_roi=1`、`score=0.90`）；
- 配置：`SemanticAdapter` 白名单 `{person,obstacle}`、`require_track=true`、`TrackPolicy::Lightweight`；
  `RuleEngine(confirm_frames=1)`；
- 断言：`blocked_unknown_label=1`、`mapped=1`、`detections=1`（仅 person）、`events=1`（仅 `person_on_rail`）——
  未知类零报警；
- 防回归：单测新增对 `FI-AI-01/02/04/07/10/12` 六个覆盖编号的存在性断言。

板端实测证据（r3）：

```json
{"id":"semantic.unknown_label_intercepted","area":"semantic","requirement":"FI-AI-12/TC-T-SEM-01",
 "status":"PASS","detail":"unknown label intercepted (no alarm); person still mapped and confirmed",
 "evidence":{"blocked_unknown_label":"1","detections":"1","event_type":"person_on_rail",
             "events":"1","input_boxes":"2","mapped":"1"}}
```

```
[PASS] semantic/semantic.unknown_label_intercepted :: unknown label intercepted (no alarm); person still mapped and confirmed [FI-AI-12/TC-T-SEM-01]
type=summary total=17 passed=17 failed=0
```

## 4. 关键数值（可独立比对，来自归档 JSON `evidence`）

| 判例 | evidence |
|---|---|
| `preprocess.synthetic_letterbox` | `scale=0.500000`、`pad_x=0`、`pad_y=140`、`bytes=1228800`、`r_mean=34.38`、`b_mean=-72.50` |
| `detect.synthetic_box` | `boxes=1`、`label=person`、`x=0.498437`、`y=0.496094`、`w=0.015625`、`h=0.020312`、`expected_x=0.498437`、`tolerance=0.001` |
| `seg.synthetic_mask` | `boxes=1`、`mask_w=640`、`mask_h=640`、`positive=14400`、`reason=SEG_OK` |
| `two_stage.person_on_mask` | `person_total=1`、`mask_hit=1`、`alarm_hits=1` |
| `two_stage.mask_missing_unavailable` | `available=false`、`reason=TWO_STAGE_UNAVAILABLE`、`alarm_hits=0` |
| `envelope.stale_duplicate_out_of_order` | `accepted=2`、`stale_generation=1`、`duplicate_seq=1`、`out_of_order_seq=1` |
| `semantic.unknown_label_intercepted` | `input_boxes=2`、`mapped=1`、`blocked_unknown_label=1`、`detections=1`、`events=1`、`event_type=person_on_rail` |
| `availability.ai_stopped` | `status_text=AI 不可用` |
| `availability.distance_uncalibrated` | `distance_m=-1.00`、`text=—` |
| `availability.debug_unknown_not_zero` | `fps_text=—`、`confidence_text=—`、`unavailable_reason=PROFILE_UNSUPPORTED` |

## 5. 结构化 JSON（归档）

```json
{"schema":"railhmi.vision.selftest.v1","no_device":true,"environment":"ai-host-fixture",
 "summary":{"total":17,"passed":17,"failed":0},"cases":[…17 条…],
 "pending":["真机精度/检出率/误报率：需 ENV-D 现场数据与冻结测试集实测（本自检不声称通过）",
            "人工视觉确认（框/掩码是否合理）：只能由用户完成（本自检不替代）",
            "4/6 路并发、热稳态温度与长跑稳定性：需板端长时实测（本自检仅夹具路径）",
            "真实相机 RTSP 取流与真实目标距离：需现场授权（ENV-D）"]}
```

- 归档文件（测试设计 G13 要求的位置）：
  - `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r3.json`
  - `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r3.deterministic.json`
  - （上一轮 r2 的两份归档保留作历史；新增判例会改变内容，跨版本不比字节）
- JSON 断言：不含设备地址（`192.168.`）、RTSP URL 或凭据变量名（单测 `selftest_json_no_device_or_secret`）。

## 6. 确定性（同一构建两次运行）

```
$ sha256sum item013-selftest-det1-….json item013-selftest-det2-….json
9bfbd807…（两行同一哈希，逐字节相同）
```

`--deterministic` 省略 `timestamp_ms`；跨**版本**新增判例后哈希自然改变，本节只证明“同版本两次运行确定”。

## 7. 待执行项标注（TC-T-SELF-03）

每次运行的日志都会打印 4 条 `type=pending`（真机精度/检出率/误报率、人工视觉确认、4/6 路并发与热稳态、
真实 RTSP 与真实距离），JSON 同名字段同步携带 → 本增量**不**声称这些项通过。

## 8. 返工留痕

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | `…-r1.log` 等 | 自检 3 项失败：`model.missing`（reason 断言错）、`model.tensor_mismatch`（要求 mismatched 非空）、`two_stage.person_on_mask`（分割夹具 DFL 峰值通道错 → 框宽≈0） | 断言改 `MODEL_MISSING`；去掉 mismatched 要求；DFL 峰值通道 32/48 → 47/63（`positive` 1→14400） |
| **r3** | `…-r3.*`（本轮全部产物） | **D-01（验证轮）**：`FI-AI-12` 未在 `--selftest` 套件内覆盖，与头注释宣称不符 | 新增 `semantic.unknown_label_intercepted` 判例 + 单测 FI 覆盖断言；r3 17/17 PASS |

## 9. 验证轮观察项（非缺陷，口径确认）

- O-1 `model.tensor_mismatch` 的 `mismatched_items=0`：结构识别层 early-return 的合法语义（判定先于 mismatched 构造）。
- O-2 无参数运行等同 `--selftest`：探针参数面容忍行为，不判缺陷。
- O-3 media 自检日志含 `rknn_init, load model failed!`：失败路径用例的预期 stderr 噪声，退出码与 `RESULT` 为准。

## 10. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 提供 `--selftest` 与文件源夹具（合成/已知图像到期望框、掩码与判定） | §2/§4：17 判例；合成图→letterbox/量化、合成张量→期望框坐标、13 张量→640×640 掩码、两阶段命中 |
| 覆盖模型缺省、张量不匹配、空帧、无检测、陈旧结果、AI 不可用等路径 | §2：`model.missing`/`model.tensor_mismatch`/`preprocess.empty_frame`/`detect.no_detection`/`envelope.*`/`availability.ai_stopped` 全 PASS |
| 覆盖 FI-AI-01/02/04/07/10/12（G13 TC-T-SELF-01） | §3：六个 FI 均有判例（`FI-AI-12` 由 `semantic.unknown_label_intercepted` 覆盖）+ 单测存在性断言 |
| 输出结构化 JSON | §5：`railhmi.vision.selftest.v1`，含 summary/cases/evidence/pending |
| 无设备条件下各路径可复现判定并归档 | §6 确定性 + §5 归档到 `SELFTEST/`；`no_device=true` |
| 真机精度与人工视觉确认单独标注为待执行，不由本增量声称通过 | §7：四条 `type=pending`；JSON `pending` 字段 |

## 11. 覆盖边界（本轮不声称的范围）

- 未连接任何真实相机（ENV-D）；未执行真实 RKNN 前向（自检为纯夹具/纯逻辑；真实前向链路证据见 ITEM-009/012）。
- `pending` 四项未执行，不作为本轮通过项。
- 自检不替代独立验证（software_tester）与用户人工视觉确认；host 与板端运行同一纯逻辑自检，结论一致但不外推真机性能。

## 12. 证据清单（本目录 + SELFTEST 归档）

**ITEM-013（r3 终轮）**

- `vision-item013-192_168_0_103-20261021-item013-r3.transcript.json` / `.log` — 结构化/人类可读 transcript
- `…-r3.app_core-evidence_item013-core-tests-20261021-item013-r3.xml` — JUnit（`tests=1452 failures=0`）
- `…-r3.app_core-evidence_item013-selftest-20261021-item013-r3.log` / `.json` — 归档运行（17/17 + verbose 证据 + pending）
- `…-r3.app_core-evidence_item013-selftest-det1-…` / `det2-…`（`.log`+`.json`）— 两次确定性运行
- `…-r3.app_core-evidence_item013-selftest-list-20261021-item013-r3.txt` — 17 个判例 id
- `…-r3.app_core-evidence_item013-media-selftest-…`（`.log`+`.json`）— media 回归自检

**归档（测试设计要求位置）**

- `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r3.json`
- `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r3.deterministic.json`

**上一轮与返工留痕**

- `…-r2.*`（16/16 轮的全套产物）与 `SELFTEST/…-r2{,.deterministic}.json`
- `…-r1.log` 等（3 项自检失败：reason 断言 + DFL 通道错误）
