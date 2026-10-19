# ITEM-001 InferenceEvidence — 模型能力描述与就绪探测

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-001`
- 日期: 2026-10-18
- 权威轮次: **r3**（`20261018-item001-r3`）；r1 为返工前失败证据（保留）；r2 为过渡轮（已由 r3 取代，文件已移除）
- 事实口径: 下列全部为**本次实际执行**的主机（RK3588）或夹具输出；未执行项在 §6 明确标注。

---

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 判据 | 证据 |
|---|---|---|---|---|
| 夹具：结构匹配 → ready（seg） | ready / READY | `ready=true reason=READY` matched=10 mismatched=0 | TC-T-CAP-01 | `…selftest-…r3.log` / `.json` |
| 夹具：结构匹配 → ready（detect） | ready / READY | `ready=true reason=READY` matched=8 mismatched=0 | TC-T-CAP-01 | 同上 |
| 夹具：模型缺失 | MODEL_MISSING | `ready=false reason=MODEL_MISSING` | FI-AI-01 | 同上 |
| 夹具：输出数不匹配 | MODEL_MISMATCH | `ready=false reason=MODEL_MISMATCH` | FI-AI-02 | 同上 |
| 夹具：量化不符（float 输入） | MODEL_MISMATCH | `ready=false reason=MODEL_MISMATCH` mismatched=`input[0].quant ... actual=float32/none` | FI-AI-03 | 同上 |
| 夹具：声明名与结构不符 | MODEL_MISMATCH | `declared=yolov5_detect identified=yolov8_seg` | TC-T-CAP-03 | 同上 |
| 夹具：运行时不可用 | RKNN_RUNTIME_UNAVAILABLE | `reason=RKNN_RUNTIME_UNAVAILABLE` | — | 同上 |
| 夹具：未知声明名 | MODEL_MISMATCH | `reason=MODEL_MISMATCH` | — | 同上 |
| 真实 yolov8s-seg `rknn_query` | 读取成功 | `RKNN_SEG_EXIT=0`，13 输出 + proto 归档 | TC-T-CAP-04 | `…rknn-seg-…r3.json` / `…descriptors_yolov8s-seg.descriptor.txt` |
| 真实 yolov5s `rknn_query` | 读取成功 | `RKNN_DET_EXIT=0`，3 尺度 255 通道归档 | TC-T-CAP-05 | `…rknn-det-…r3.json` / `…descriptors_yolov5s.descriptor.txt` |
| 真实 seg 描述核验（声明 yolov8s-seg） | ready | `ready=1 reason=READY kind=yolov8_seg` | TC-T-CAP-01/04 | `…verify-seg-…r3.log` |
| 真实 det 描述核验（声明 yolov5s） | ready | `ready=1 reason=READY kind=yolov5_detect` | TC-T-CAP-01/05 | `…verify-det-…r3.log` |
| 真实 seg 描述按 yolov5s 声明（负例） | MODEL_MISMATCH | `ready=0 reason=MODEL_MISMATCH detail=declared=yolov5_detect identified=yolov8_seg … file name ignored` | TC-T-CAP-03 | `…verify-negative-…r3.log` |
| 配置汇总（有真实描述） | 逐路 model_version + 原因 | front/yolov5s `available=1 reason=READY`；无证据 profile `reason=PROFILE_UNSUPPORTED`；`declared_failed=0 unsupported_profiles=3 unverified=0 profile_gap=true` | TC-T-CAP-06 | `…config-desc-…r3.log` / `.json` |
| 配置汇总（无描述，仅文件存在） | 不因文件存在而 ready | front `reason=NOT_READY detail=file present; structure not verified (no rknn descriptor)` | TC-T-CAP-03/06 | `…config-nodesc-…r3.log` |
| core 单测（含本项新测试） | 全绿 | `tests=958 failures=0` | TC-T-SMOKE-01 | `…core-tests-…r3.xml` |
| 夹具自检总判 | PASS | `RESULT: PASS passed=8 failed=0` | TC-T-SELF-01（部分） | `…selftest-…r3.log` |

---

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，`Linux 6.1.141 aarch64`，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（ssh-ed25519，受信任 known_hosts，RejectPolicy） |
| 编译器 / CMake | `g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0` / `cmake 3.22.1` |
| librknnrt | `/usr/lib/librknnrt.so`（7726232 B） |
| RKNN API / 驱动 | `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` / `0.9.8`（`rknn_query(SDK_VERSION)`） |
| 模型 | `models/yolov8s-seg.rknn`（13264166 B）、`models/yolov5s.rknn`（8477315 B），上传到板端临时目录读取 |
| 入口 | `app/tools/board_vision_model_probe.py`（Paramiko SSH/SFTP，connect/build/command 全部有限超时；仅写目标 `/tmp`，结束清理） |
| 远端临时目录 | `/tmp/railhmi_vision_item001_20261018-item001-r3`（已清理） |

---

## 3. 真实模型张量描述（`rknn_query` 原始结果摘要）

### yolov8s-seg（13 输出）
- 输入：`images` `1,640,640,3`（**NHWC**）int8 affine zp=-128 scale=0.00392157（=1/255）
- 输出（NCHW int8 affine）：
  - 3 尺度 × {box `1,64,H,W`、score `1,1,H,W`、score_sum `1,1,H,W`、seg `1,32,H,W`}（H×W = 80/40/20）
  - proto `1,32,160,160`
- 张量名示例：`789`、`onnx::ReduceSum_799`、`762`、`753` → **不能按名字判断**，本次只用结构核验

### yolov5s（3 输出）
- 输入：`images` `1,640,640,3`（**NHWC**）int8 affine zp=-128 scale=0.00392157
- 输出：`1,255,80,80` / `1,255,40,40` / `1,255,20,20`（255 = 3×(5+80) COCO）

---

## 4. 性能/资源测量范围

本次为**能力核验**增量，不测量推理延迟/FPS/NPU/内存：探针只做 `rknn_init` + 若干 `rknn_query` 后释放，不执行推理。真实推理耗时/内存/FPS 属 ITEM-002/009 与执行轮（`docs/video/VIDEO_SYSTEM_DESIGN.md` §8 方法）。本文件不声称任何性能数字。

---

## 5. 证据文件清单（`docs/vision/test_evidence/ITEM-001/`）

| 文件 | 内容 |
|---|---|
| `vision-item001-192_168_0_103-20261018-item001-r3.transcript.json` | r3 全流程结构化 transcript（命令/退出码/输出/主机指纹） |
| `vision-item001-192_168_0_103-20261018-item001-r3.log` | r3 人类可读 transcript |
| `…r3.app_core-evidence_item001-core-tests-…r3.xml` | core 单测 JUnit（`tests=958 failures=0`） |
| `…r3.app_core-evidence_item001-selftest-…r3.log` / `.json` | 夹具自检（8 用例，全 PASS） |
| `…r3.app_core-evidence_item001-rknn-seg-…r3.json` | 真实 yolov8s-seg 张量描述 JSON |
| `…r3.app_core-evidence_item001-rknn-det-…r3.json` | 真实 yolov5s 张量描述 JSON |
| `…r3.app_core-evidence_item001-verify-seg-…r3.log` | 真实 seg 描述核验（READY） |
| `…r3.app_core-evidence_item001-verify-det-…r3.log` | 真实 det 描述核验（READY） |
| `…r3.app_core-evidence_item001-verify-negative-…r3.log` | 负例（MODEL_MISMATCH） |
| `…r3.app_core-evidence_item001-config-desc-…r3.log` / `.json` | 配置汇总（有描述） |
| `…r3.app_core-evidence_item001-config-nodesc-…r3.log` | 配置汇总（无描述 → NOT_READY） |
| `…r3.descriptors_yolov8s-seg.descriptor.txt` / `…descriptors_yolov5s.descriptor.txt` | 板端产出的文本描述（核验输入） |
| `vision-item001-…-r1.transcript.json` / `.log` + `…r1.descriptors_*` | **返工前失败证据**：`RKNN_RUNTIME_UNAVAILABLE`（`rknn_release` 符号缺失）+ `core-evidence` 目录缺失 |
| `build-CoreTests-192_168_0_103-20261018-item001-r1.log` / `.transcript.json` | r1 core 构建/测试（含 `one_count` bool 编译错误与修复后 958/0；文件为修复后复跑记录） |

---

## 6. 未执行 / 未声称（诚实边界）

1. **未执行真实推理**：无前向执行、无 box/mask 输出，无精度/延迟/FPS/NPU/内存测量。
2. **未连接/未取流任何相机**（ENV-D 未执行）；本次只用板端文件模型，不涉及 RTSP。
3. **未验证 ONNX→RKNN 转换正确性、数据集、训练**（属数据/训练/导出能力）。
4. **未做 P5 运行期显示验证**：本项只提供能力/原因词表与探针输出；P5 接线属 ITEM-012。
5. **未替换 `SimDetector`**：真实 Detector 接入属 ITEM-009。
6. **未做人工视觉确认**：本次无图像/视频内容需核验。
7. `config-desc` 报 `profile_gap=true`（front 需要 seg+detect，但 `app.ai.models` 只为 front 登记了 `yolov5s`）。这是**配置缺口事实**，不是本增量实现的失败；不在此处改配置。
8. 本次结果**不替代**验证能力的独立复现；板端真模型探针通过 ≠ 推理精度验收。
