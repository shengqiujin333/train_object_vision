# 视觉推理 ITEM-013 实现 — 无相机自检与可归档夹具证据

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-013`
- 版本: v1（2026-10-21）
- 上游: `docs/vision/VISION_TEST_DESIGN.md` G13（TC-T-SELF-01..03；JSON 归档 `docs/vision/test_evidence/SELFTEST/`）、`docs/vision/VISION_DESIGN.md` §0.3/§7/§11、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6、ITEM-001..012 的模块
- 范围: 只实现 ITEM-013（自检入口 + 夹具 + 归档）。不训练、不改模型、不连接相机；真机精度与人工视觉确认明确标注为**待执行**。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 自检套件（纯逻辑） | `app/src/core/include/railhmi/core/vision_selftest.h`、`src/vision_selftest.cpp` | 16 个确定性判例，覆盖模型能力/预处理/检测/分割/两阶段/结果关联/可用性/调试占位；每判例带 `requirement`（TC/FI 编号）与 `evidence`（关键数值）；`pending` 显式列出待执行项；`selftest_json()` 支持确定性输出（排除 timestamp）。 |
| 自检入口 | `app/src/core/tools/selftest_probe.cpp`（`railhmi-vision-selftest`） | `--selftest`、`--json`、`--out`、`--verbose`、`--deterministic`、`--list`；退出码 0/1/2。 |
| 单元测试 | `app/tests/core/test_vision_selftest.cpp` | 全套通过、判例 id 唯一/域覆盖/均带 requirement、pending 标注、JSON 结构与确定性、关键证据在场、JSON 不含地址/凭据。 |
| 板端入口 | `app/tools/board_vision_selftest.py` | 上传→CoreTests→core-tests→MediaCheck→自检两次确定性 JSON（板端 sha256 比对）→归档 JSON（verbose）→`--list`→media 自检→取回→**归档副本到 `SELFTEST/`**→清理。 |

构建接入：`app/src/core/CMakeLists.txt`（库源 + 探针）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 判例矩阵（16 个，全部无设备可复现）

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
| `availability.ai_stopped` | availability | TC-T-DISP-02 | `worker_alive=false` → `AI 不可用`（`StatusLevel::Unavailable`） |
| `availability.duty_unsupported` | availability | TC-T-MIS-01/02 | 三职责（登记了模型名）仍 `PROFILE_UNSUPPORTED` + `unknown` |
| `availability.distance_uncalibrated` | availability | TC-T-DIST-01 | 未标定 → `distance_m=-1`、文本 `—` |
| `availability.debug_unknown_not_zero` | debug | TC-T-DBG-03 | 不可用路 `fps/confidence/box_count/distance` 全为 `—`，原因非空 |

## 3. 结构化 JSON 与归档

```json
{"schema":"railhmi.vision.selftest.v1","no_device":true,"environment":"ai-host-fixture",
 "summary":{"total":16,"passed":16,"failed":0},
 "cases":[{"id":"...","area":"...","requirement":"...","status":"PASS","detail":"...","evidence":{...}}],
 "pending":["真机精度/检出率/误报率：需 ENV-D ...","人工视觉确认（框/掩码是否合理）：只能由用户完成 ...",
            "4/6 路并发、热稳态温度与长跑稳定性 ...","真实相机 RTSP 取流与真实目标距离 ..."]}
```

- `--deterministic` 省略 `timestamp_ms`，两次运行逐字相同（板端 `sha256sum` 比对，本轮 `af7227f3…`）。
- 归档位置按测试设计：`docs/vision/test_evidence/SELFTEST/vision-selftest-<host>-<stamp>.json`（含 timestamp）与
  `…deterministic.json`（确定性副本）。
- `pending` 四项在每次运行的 JSON 与日志中都可见（TC-T-SELF-03）。

## 4. 板端实测（r2 终轮，RK3588 192.168.0.103）

| 项 | 实测 |
|---|---|
| core 单测 | `tests=1445 failures=0`（ITEM-012 的 1412 + 本增量 33） |
| `railhmi-vision-selftest --selftest` | **16/16 PASS**（`type=summary total=16 passed=16 failed=0`） |
| 确定性 | 两次 `--deterministic` JSON 逐字节相同（sha256 `af7227f3…`，板端 `sha256sum` 双列一致） |
| 关键数值 | 检测框 `x=0.498437`（期望同值）、分割掩码 `640×640`/`positive=14400`、两阶段 `mask_hit=1 alarm_hits=1`、陈旧/重复/乱序各 1 |
| `--list` | 16 个判例 id（与 JSON 顺序一致） |
| 归档 | `SELFTEST/vision-selftest-192_168_0_103-20261021-item013-r2{,.deterministic}.json` |
| media 自检（回归） | `railhmi-vision-pipeline --selftest` PASS（30/30，ITEM-009..012 路径未回退） |

## 5. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | 自检 3 个判例失败：`model.missing`（期望 MODEL_MISMATCH，实为 MODEL_MISSING）、`model.tensor_mismatch`（要求 `mismatched` 非空）、`two_stage.person_on_mask`（`mask_hit=0`） | (a) 缺失模型的正确稳定原因是 `MODEL_MISSING`，实现断言写错；(b) `check_capability` 对该负例只给 reason 不填 mismatched；(c) 分割夹具 DFL 通道算错：x2/y2 应取 side 内 `d=15`（通道 47/63），误写成通道 32/48（`d=0`）→ 框宽≈0、掩码被裁成 1 像素、人员中心落空 | (a) 断言改 `MODEL_MISSING`；(b) 只断言 `MODEL_MISMATCH` + not ready；(c) DFL 峰值通道改为 47/63（框宽 120px，`positive` 由 1 → 14400） | r2：16/16 PASS，两阶段命中恢复 |

## 6. 观察与移交

1. **自检 ≠ 精度验收**：本增量只证明“夹具路径可复现、判定与关键数值确定”；真机检出率/误报率、INT8 掩码质量、
   4/6 路并发与热稳态仍为 `pending`，由 ENV-D 与用户完成。
2. **host 与板端分档**：本轮在板端运行同一纯逻辑自检（不经 RKNN 前向），结论与 host 相同；真实模型前向的证据
   在 ITEM-009/012 的链路证据中，二者不互相替代。
3. **归档命名**：`SELFTEST/` 中的文件名含主机与轮次戳；跨轮比较应使用 `.deterministic.json`（无时间戳）。
4. **后续建议**：把 `railhmi-vision-selftest --selftest` 纳入部署后健康检查（无相机也能判定推理栈配置是否可用）；
   若将来提供真实职责模型，可在 `pending` 之外追加“职责模型 ready”判例。
