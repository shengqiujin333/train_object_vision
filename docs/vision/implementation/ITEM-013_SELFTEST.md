# 视觉推理 ITEM-013 实现 — 无相机自检与可归档夹具证据

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-013`
- 版本: v2（2026-10-21，按验证轮 D-01 补 FI-AI-12 覆盖后重跑 r3；v1/r2 见返工记录）
- 上游: `docs/vision/VISION_TEST_DESIGN.md` G13（TC-T-SELF-01..03；要求覆盖 FI-AI-01/02/04/07/10/12；JSON 归档 `docs/vision/test_evidence/SELFTEST/`）、`docs/vision/VISION_DESIGN.md` §0.3/§7/§11、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6、`docs/vision/test_evidence/ITEM-013/VERIFY-ITEM-013.md`（D-01）
- 范围: 只实现 ITEM-013（自检入口 + 夹具 + 归档）。不训练、不改模型、不连接相机；真机精度与人工视觉确认明确标注为**待执行**。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 自检套件（纯逻辑） | `app/src/core/include/railhmi/core/vision_selftest.h`、`src/vision_selftest.cpp` | **17 个**确定性判例，覆盖模型能力/预处理/检测/分割/两阶段/结果关联/**未知 label 拦截（FI-AI-12）**/可用性/调试占位；每判例带 `requirement`（TC/FI 编号）与 `evidence`（关键数值）；`pending` 显式列出待执行项；`selftest_json()` 支持确定性输出（排除 timestamp）。 |
| 自检入口 | `app/src/core/tools/selftest_probe.cpp`（`railhmi-vision-selftest`） | `--selftest`、`--json`、`--out`、`--verbose`、`--deterministic`、`--list`；退出码 0/1/2。 |
| 单元测试 | `app/tests/core/test_vision_selftest.cpp` | 全套通过、判例 id 唯一/域覆盖/均带 requirement、**六个 FI（含 FI-AI-12）覆盖断言**、pending 标注、JSON 结构与确定性、关键证据在场、JSON 不含地址/凭据。 |
| 板端入口 | `app/tools/board_vision_selftest.py` | 上传→CoreTests→core-tests→MediaCheck→自检两次确定性 JSON（板端 sha256 比对）→归档 JSON（verbose）→`--list`→media 自检→取回→**归档副本到 `SELFTEST/`**→清理。 |

构建接入：`app/src/core/CMakeLists.txt`（库源 + 探针）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 判例矩阵（17 个，全部无设备可复现）

| id | area | 覆盖 | 判定要点（证据字段） |
|---|---|---|---|
| `model.missing` | model | FI-AI-01 / TC-T-CAP-02 | 文件缺失描述 → `ready=false`、`reason=MODEL_MISSING` |
| `model.tensor_mismatch` | model | FI-AI-02 / TC-T-CAP-03 | seg 结构冒充 yolov5s 声明 → `MODEL_MISMATCH` |
| `model.ready` | model | TC-T-CAP-01 | det(3 输出)+seg(13 输出) 夹具描述均 ready |
| `preprocess.synthetic_letterbox` | preprocess | TC-T-PRE-01/03/04 | 1280×720 合成图 → `scale=0.5`、`pad_y=140`、`1,228,800` 字节、R 均值 > B 均值 |
| `preprocess.empty_frame` | preprocess | FI-AI-04 / TC-T-PRE-05 | 空/非法帧显式拒绝（reason 非空、无部分数据） |
| `detect.no_detection` | detect | TC-T-DET-04 | 全 0 张量 → `ok`、`boxes=0`、`reason=DETECT_EMPTY` |
| `detect.synthetic_box` | detect | TC-T-DET-02 / FI-AI-11 | 合成 person 框：`x=0.498437,y=0.496094,w=0.015625,h=0.020312`（与 `expected_x` 在 0.001 内） |
| `seg.synthetic_mask` | seg | TC-T-SEG-01/02/03 | 13 张量夹具 → 1 rail 框、`mask 640×640`、`positive=14400` |
| `two_stage.person_on_mask` | two_stage | TC-T-TWOPHASE-01 | 人员中心落在 rail 掩码 → `mask_hit=1`、`alarm_hits=1` |
| `two_stage.mask_missing_unavailable` | two_stage | FI-AI-07 / TC-T-TWOPHASE-03 | 缺掩码 → `available=false`、`reason=TWO_STAGE_UNAVAILABLE`、`alarm_hits=0` |
| `envelope.stale_duplicate_out_of_order` | envelope | FI-AI-10 / TC-T-PUB-02 | 旧代/重复 seq/乱序各自 rejected 且独立计数（1/1/1，accepted=2） |
| `envelope.unavailable_vs_empty` | envelope | TC-T-PUB-03 / FI-AI-07 | `unavailable+reason` 与 `ready+空框` 字段可区分 |
| `semantic.unknown_label_intercepted` | semantic | **FI-AI-12** / TC-T-SEM-01 | 未知 label（`weird_class`）+ person 同帧 → `blocked_unknown_label=1`、`mapped=1`、仅 person 检出、仅 1 个 `person_on_rail` 事件（未知类零报警） |
| `availability.ai_stopped` | availability | TC-T-DISP-02 | `worker_alive=false` → `AI 不可用`（`StatusLevel::Unavailable`） |
| `availability.duty_unsupported` | availability | TC-T-MIS-01/02 | 三职责（登记了模型名）仍 `PROFILE_UNSUPPORTED` + `unknown` |
| `availability.distance_uncalibrated` | availability | TC-T-DIST-01 | 未标定 → `distance_m=-1`、文本 `—` |
| `availability.debug_unknown_not_zero` | debug | TC-T-DBG-03 | 不可用路 `fps/confidence/box_count/distance` 全为 `—`，原因非空 |

`FI-AI-12` 判例的实现路径（与 ITEM-007 既有语义一致，不新造行为）：
`build_envelope`（person + `weird_class`，两框均 `in_roi=1`）→ `SemanticAdapter`（白名单 `{person,obstacle}`、
`require_track=true`、`TrackPolicy::Lightweight`）→ `RuleEngine`（`confirm_frames=1`）。
断言：未知类被拦截并计数、person 照常映射并确认 1 个 `person_on_rail`、未知类**零报警**。

## 3. 结构化 JSON 与归档

```json
{"schema":"railhmi.vision.selftest.v1","no_device":true,"environment":"ai-host-fixture",
 "summary":{"total":17,"passed":17,"failed":0},
 "cases":[{"id":"...","area":"...","requirement":"...","status":"PASS","detail":"...","evidence":{...}}],
 "pending":["真机精度/检出率/误报率：需 ENV-D ...","人工视觉确认（框/掩码是否合理）：只能由用户完成 ...",
            "4/6 路并发、热稳态温度与长跑稳定性 ...","真实相机 RTSP 取流与真实目标距离 ..."]}
```

- `--deterministic` 省略 `timestamp_ms`，两次运行逐字相同（板端 `sha256sum` 比对，本轮 `9bfbd807…`）。
- 归档位置按测试设计：`docs/vision/test_evidence/SELFTEST/vision-selftest-<host>-<stamp>.json`（含 timestamp）与
  `…deterministic.json`（确定性副本）。
- `pending` 四项在每次运行的 JSON 与日志中都可见（TC-T-SELF-03）。

## 4. 板端实测（r3 终轮，RK3588 192.168.0.103）

| 项 | 实测 |
|---|---|
| core 单测 | `tests=1452 failures=0`（ITEM-012 的 1412 + 本增量 40，含 FI 覆盖断言） |
| `railhmi-vision-selftest --selftest` | **17/17 PASS**（`type=summary total=17 passed=17 failed=0`，含 `FI-AI-12` 未知 label 拦截） |
| 确定性 | 两次 `--deterministic` JSON 逐字节相同（sha256 `9bfbd807…`，板端 `sha256sum` 双列一致） |
| 关键数值 | 检测框 `x=0.498437`（期望同值）、分割掩码 `640×640`/`positive=14400`、两阶段 `mask_hit=1 alarm_hits=1`、陈旧/重复/乱序各 1、未知 label `blocked=1/mapped=1/events=1` |
| `--list` | 17 个判例 id（与 JSON 顺序一致） |
| 归档 | `SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r3{,.deterministic}.json` |
| media 自检（回归） | `railhmi-vision-pipeline --selftest` PASS（ITEM-009..012 路径未回退） |

## 5. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | 自检 3 个判例失败：`model.missing`（期望 MODEL_MISMATCH，实为 MODEL_MISSING）、`model.tensor_mismatch`（要求 `mismatched` 非空）、`two_stage.person_on_mask`（`mask_hit=0`） | (a) 缺失模型的正确稳定原因是 `MODEL_MISSING`；(b) `check_capability` 对该负例只给 reason 不填 mismatched；(c) 分割夹具 DFL 通道算错：x2/y2 应取 side 内 `d=15`（通道 47/63），误写成通道 32/48（`d=0`）→ 框宽≈0、掩码被裁成 1 像素 | (a) 断言改 `MODEL_MISSING`；(b) 只断言 `MODEL_MISMATCH` + not ready；(c) DFL 峰值通道改为 47/63（框宽 120px，`positive` 1→14400） | r2：16/16 PASS |
| **r3（验证轮 D-01）** | 头注释声称覆盖 `FI-AI-01/02/04/07/10/12`，但 16 个判例的 `requirement` 无任何 `FI-AI-12`（未知 label 拦截）—— 宣称与内容不符 | 自检套件未包含 FI-AI-12 路径；语义拦截机制本体（`blocked_unknown_label`）只在单测/ITEM-007 夹具中覆盖，未进入 `--selftest` | 新增判例 `semantic.unknown_label_intercepted`（person + `weird_class` 通过 `build_envelope` + `SemanticAdapter` 白名单 + `RuleEngine` 单帧确认）；单测新增 6 项 FI 覆盖断言（含 FI-AI-12），防止宣称与内容再次脱节 | r3：17/17 PASS，`FI-AI-12` 在 JSON/日志可见 |

## 6. 观察与移交

1. **自检 ≠ 精度验收**：本增量只证明“夹具路径可复现、判定与关键数值确定”；真机检出率/误报率、INT8 掩码质量、
   4/6 路并发与热稳态仍为 `pending`，由 ENV-D 与用户完成。
2. **host 与板端分档**：本轮在板端运行同一纯逻辑自检（不经 RKNN 前向），结论与 host 相同；真实模型前向的证据
   在 ITEM-009/012 的链路证据中，二者不互相替代。
3. **归档命名**：`SELFTEST/` 中的文件名含主机与轮次戳；跨轮比较应使用 `.deterministic.json`（无时间戳）。
   注意：新增判例会改变 JSON 内容，因此跨**版本**比较应以“同版本两次运行逐字节相同”为准，不跨版本比字节。
4. **验证轮观察项（非缺陷，已采纳口径）**：O-1 `model.tensor_mismatch` 的 `mismatched_items=0` 是结构识别
   early-return 的合法语义（判定先于 mismatched 构造）；O-2 无参数运行等同 `--selftest`（探针参数面容忍）；
   O-3 media 自检日志中的 `rknn_init, load model failed!` 是“失败路径”用例的预期噪声，退出码与 RESULT 为准。
5. **后续建议**：把 `railhmi-vision-selftest --selftest` 纳入部署后健康检查（无相机也能判定推理栈配置是否可用）；
   若将来提供真实职责模型，可追加“职责模型 ready”判例。
