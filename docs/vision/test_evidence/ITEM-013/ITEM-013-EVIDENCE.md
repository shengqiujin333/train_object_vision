# ITEM-013 证据 — 无相机自检与可归档夹具证据

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-013`
- 轮次标记: `20261021-item013-r2`（终轮）；r1 为返工留痕
- 日期: 2026-10-21
- 实现文档: `docs/vision/implementation/ITEM-013_SELFTEST.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_selftest.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）
- **无设备自检**：本轮未连接相机、未执行 RKNN 前向；自检为纯夹具/纯逻辑（`no_device=true`）

## 1. 本轮执行步骤（一次有界会话）

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ + 两个 `.rknn` 资产（仅用于 media 回归，自检不加载） | 0 | 仅 `/tmp/railhmi_vision_item013_20261021-item013-r2` |
| 构建 CoreTests / MediaCheck | `bash build.sh CoreTests` / `MediaCheck` | 0 | 通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1445 failures=0`（ITEM-012 的 1412 + 本增量 33） |
| 自检（确定性 ×2） | `railhmi-vision-selftest --selftest --deterministic`（两次） | 0×2 | 输出逐字节相同 |
| 确定性核对 | `sha256sum det1.json det2.json` | 0 | 两个文件同一哈希 `af7227f3…` |
| 自检（归档、verbose） | `railhmi-vision-selftest --selftest --verbose --json …` | 0 | `16/16 PASS` + 归档 JSON |
| 判例清单 | `railhmi-vision-selftest --list` | 0 | 16 个 id |
| media 回归自检 | `railhmi-vision-pipeline --selftest` | 0 | PASS（30/30） |
| 归档副本 | 本地写入 `docs/vision/test_evidence/SELFTEST/` | — | 2 份 JSON |
| 清理 | `rm -rf /tmp/railhmi_vision_item013_20261021-item013-r2` | 0 | `cleaned` |

## 2. 自检判例结果（16/16 PASS）

```
type=selftest schema=railhmi.vision.selftest.v1 no_device=true environment=ai-host-fixture cases=16
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
[PASS] availability/availability.ai_stopped :: worker stopped -> AI unavailable [TC-T-DISP-02]
[PASS] availability/availability.duty_unsupported :: … [TC-T-MIS-01/02]
[PASS] availability/availability.distance_uncalibrated :: … [TC-T-DIST-01]
[PASS] debug/availability.debug_unknown_not_zero :: … [TC-T-DBG-03]
type=summary total=16 passed=16 failed=0
RESULT: PASS failures=0
```

## 3. 关键数值（可独立比对，来自归档 JSON `evidence`）

| 判例 | evidence |
|---|---|
| `preprocess.synthetic_letterbox` | `scale=0.500000`、`pad_x=0`、`pad_y=140`、`bytes=1228800`、`r_mean=34.38`、`b_mean=-72.50` |
| `detect.synthetic_box` | `boxes=1`、`label=person`、`x=0.498437`、`y=0.496094`、`w=0.015625`、`h=0.020312`、`expected_x=0.498437`、`tolerance=0.001` |
| `seg.synthetic_mask` | `boxes=1`、`mask_w=640`、`mask_h=640`、`positive=14400`、`reason=SEG_OK` |
| `two_stage.person_on_mask` | `person_total=1`、`mask_hit=1`、`alarm_hits=1` |
| `two_stage.mask_missing_unavailable` | `available=false`、`reason=TWO_STAGE_UNAVAILABLE`、`alarm_hits=0` |
| `envelope.stale_duplicate_out_of_order` | `accepted=2`、`stale_generation=1`、`duplicate_seq=1`、`out_of_order_seq=1`、各 reason=`stale_generation`/`duplicate_seq`/`out_of_order_seq` |
| `availability.ai_stopped` | `status_text=AI 不可用` |
| `availability.distance_uncalibrated` | `distance_m=-1.00`、`text=—` |
| `availability.debug_unknown_not_zero` | `fps_text=—`、`confidence_text=—`、`unavailable_reason=PROFILE_UNSUPPORTED` |

## 4. 结构化 JSON（归档）

```json
{"schema":"railhmi.vision.selftest.v1","no_device":true,"environment":"ai-host-fixture",
 "summary":{"total":16,"passed":16,"failed":0},"cases":[…16 条…],
 "pending":["真机精度/检出率/误报率：需 ENV-D 现场数据与冻结测试集实测（本自检不声称通过）",
            "人工视觉确认（框/掩码是否合理）：只能由用户完成（本自检不替代）",
            "4/6 路并发、热稳态温度与长跑稳定性：需板端长时实测（本自检仅夹具路径）",
            "真实相机 RTSP 取流与真实目标距离：需现场授权（ENV-D）"]}
```

- 归档文件（测试设计 G13 要求的位置）：
  - `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r2.json`（含 timestamp）
  - `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r2.deterministic.json`
- JSON 断言：不含设备地址（`192.168.`）、RTSP URL 或凭据变量名（单测 `selftest_json_no_device_or_secret`）。

## 5. 确定性（同一构建两次运行）

```
$ sha256sum item013-selftest-det1-….json item013-selftest-det2-….json
af7227f34845460c81ec63ef57863d8422049d2c3e1101903a1fc219c56df9c1  item013-selftest-det1-….json
af7227f34845460c81ec63ef57863d8422049d2c3e1101903a1fc219c56df9c1  item013-selftest-det2-….json
```

两文件逐字节相同（`--deterministic` 省略 `timestamp_ms`）。

## 6. 待执行项标注（TC-T-SELF-03）

每次运行的日志都会打印：

```
type=pending text=真机精度/检出率/误报率：需 ENV-D 现场数据与冻结测试集实测（本自检不声称通过）
type=pending text=人工视觉确认（框/掩码是否合理）：只能由用户完成（本自检不替代）
type=pending text=4/6 路并发、热稳态温度与长跑稳定性：需板端长时实测（本自检仅夹具路径）
type=pending text=真实相机 RTSP 取流与真实目标距离：需现场授权（ENV-D）
```

→ 本增量**不**声称真机精度、检出率、人工视觉确认通过。

## 7. 返工留痕

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | `…-r1.log`、`…r1…selftest-….log`、`…r1…selftest-det{1,2}….log` | 自检 3 项失败：`model.missing`（断言用错 reason）、`model.tensor_mismatch`（要求 mismatched 非空）、`two_stage.person_on_mask`（`mask_hit=0`，因分割夹具 DFL 峰值通道写错 → 框宽≈0、掩码 1 像素） | 断言改 `MODEL_MISSING`；去掉 mismatched 要求；DFL 峰值通道由 32/48 改为 47/63（框宽 120px，`positive` 1→14400） |

## 8. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 提供 `--selftest` 与文件源夹具（合成/已知图像到期望框、掩码与判定） | §2/§3：16 判例；合成图→letterbox/量化、合成张量→期望框坐标、13 张量→640×640 掩码、两阶段命中 |
| 覆盖模型缺省、张量不匹配、空帧、无检测、陈旧结果、AI 不可用等路径 | §2：`model.missing`/`model.tensor_mismatch`/`preprocess.empty_frame`/`detect.no_detection`/`envelope.stale_duplicate_out_of_order`/`availability.ai_stopped` 全 PASS |
| 输出结构化 JSON | §4：`railhmi.vision.selftest.v1`，含 summary/cases/evidence/pending |
| 无设备条件下各路径可复现判定并归档 | §5 确定性 + §4 归档到 `SELFTEST/`；`no_device=true` |
| 真机精度与人工视觉确认单独标注为待执行，不由本增量声称通过 | §6：四条 `type=pending`；JSON `pending` 字段 |

## 9. 覆盖边界（本轮不声称的范围）

- 未连接任何真实相机（ENV-D）；未执行真实 RKNN 前向（自检为纯夹具/纯逻辑；真实前向链路证据见 ITEM-009/012）。
- `pending` 四项（真机精度/检出率/误报率、人工视觉确认、4/6 路并发与热稳态、真实 RTSP 与真实距离）未执行，
  不作为本轮通过项。
- 自检不替代独立验证（software_tester）与用户人工视觉确认；host 与板端运行同一纯逻辑自检，结论一致但不外推真机性能。

## 10. 证据清单（本目录 + SELFTEST 归档）

**ITEM-013（r2 终轮）**

- `vision-item013-192_168_0_103-20261021-item013-r2.transcript.json` / `.log` — 结构化/人类可读 transcript
- `…-r2.app_core-evidence_item013-core-tests-20261021-item013-r2.xml` — JUnit（`tests=1445 failures=0`）
- `…-r2.app_core-evidence_item013-selftest-20261021-item013-r2.log` / `.json` — 归档运行（16/16 + verbose 证据 + pending）
- `…-r2.app_core-evidence_item013-selftest-det1-…` / `det2-…`（`.log`+`.json`）— 两次确定性运行
- `…-r2.app_core-evidence_item013-selftest-list-20261021-item013-r2.txt` — 16 个判例 id
- `…-r2.app_core-evidence_item013-media-selftest-…`（`.log`+`.json`）— media 回归自检

**归档（测试设计要求位置）**

- `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r2.json`
- `docs/vision/test_evidence/SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r2.deterministic.json`

**返工留痕**

- `…-r1.log`、`…r1…selftest-….log`、`…r1…selftest-det{1,2}….log` — 3 项自检失败（reason 断言 + DFL 通道错误）
