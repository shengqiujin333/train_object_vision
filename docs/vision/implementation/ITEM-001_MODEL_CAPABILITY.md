# 视觉推理 ITEM-001 实现 — 模型能力描述与就绪探测

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-001`
- 版本: v1（2026-10-18）
- 上游输入: `docs/vision/VISION_DESIGN.md`（设计与硬约束）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-04/§6）、`docs/vision/VISION_TEST_DESIGN.md`（G1/FI-AI-01..03、TC-T-CAP-01..06）、`app/config/{cameras,app}.yaml`、`models/*.rknn`
- 本次范围: 只实现 ITEM-001（模型能力描述与就绪探测）。未实现预处理/检测后处理/分割后处理/同帧判定/结果发布/语义接入/线程调度（后续 ITEM），未改 core 既有权重逻辑、UI、配置 schema 或 SQLite DDL。

---

## 1. 交付内容（源码与入口）

| 交付 | 路径 | 说明 |
|---|---|---|
| 能力核验纯逻辑 | `app/src/core/include/railhmi/core/vision_model_capability.h`、`app/src/core/src/vision_model_capability.cpp` | 张量描述/规格/结构识别/逐项核验/稳定 reason/文本与 JSON 序列化/模型路径解析。仅标准库，无 Qt/GStreamer/rknn。 |
| 能力核验探针 | `app/src/core/tools/model_probe.cpp`（`build-core/tools/railhmi-model-probe`） | `--selftest`（夹具两条路径）、`--descriptor`（真实描述核验）、`--config`（按配置逐路汇总 model_version/原因）。 |
| 真模型张量读取 | `app/src/media/include/railhmi/media/rknn_model_probe.h`、`app/src/media/src/rknn_model_probe.cpp` | dlopen 绑定 `librknnrt.so`，用 `rknn_query` 读取 in/out 数量、张量属性与 SDK/驱动版本，产出 `ModelDescriptor`。无编译期 rknn 依赖。 |
| 真模型探针 | `app/src/media/tools/rknn_probe.cpp`（`build-media/tools/railhmi-rknn-probe`） | `--model <path>` 输出文本/JSON 张量描述；文本可被核心探针 `--descriptor` 直接解析。 |
| 单元测试 | `app/tests/core/test_vision_model_capability.cpp` | 规格映射/结构识别/匹配与不匹配/缺失/运行时不可用/profile 证据/配置解析/文本往返/JSON。 |
| 板端执行入口 | `app/tools/board_vision_model_probe.py` | 一次有界会话：上传→CoreTests→MediaCheck→selftest→真实 rknn_query→核验→配置汇总→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`、`app/src/media/CMakeLists.txt`（仅新增条目，未改现有目标语义）。

---

## 2. 如何满足工作项要求

| 要求 | 实现 | 可观察证据 |
|---|---|---|
| 按 `ai_profile` 加载**配置登记**的模型名/版本（不写死） | `declared_models(app.ai.models)` 读取 profile/name/version；`resolve_model_path(models_dir, name)` 解析 `<dir>/<name>.rknn`；models 目录来自 `--models-dir` 或 `RAILHMI_MODELS_DIR` 或按配置目录推导，从不写死设备地址 | `--config` 输出逐条 `profile=/model=/version=/path=`；无任何 IP/凭据字符串 |
| 用 `rknn_query` 读取张量数量/形状布局/量化参数与运行时版本 | `probe_rknn_model`：dlopen + `rknn_init` → `RKNN_QUERY_IN_OUT_NUM` / `INPUT_ATTR` / `OUTPUT_ATTR` / `SDK_VERSION` → `rknn_destroy` | `item001-rknn-seg/det` JSON；真实 API `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8` |
| 与后处理期望**逐项匹配**通过后才 ready | `check_capability`：结构识别（不按文件名）→ 输入数量/尺寸/通道/量化 → 检测 3 尺度×255 通道；分割 13 输出=3 组×{box64,score,score_sum,seg32}+proto32×160×160 | `item001-verify-seg/det`：`ready=1 reason=READY matched=10/8 mismatched=0` |
| 缺失/不匹配/运行时不可用 → `unavailable + 稳定 reason` | 稳定 token：`MODEL_MISSING`/`MODEL_LOAD_FAILED`/`MODEL_MISMATCH`/`RKNN_RUNTIME_UNAVAILABLE`/`PROFILE_UNSUPPORTED`/`NOT_READY` | selftest 8/8；`item001-verify-negative`：真实 seg 描述按 `yolov5s` 声明 → `MODEL_MISMATCH` |
| **不得只按文件名判断** | `identify_kind` 只看张量结构；声明名只用于选规格，结构不符即 `MODEL_MISMATCH` | `item001-verify-negative` 负例 |
| `--selftest` 匹配/不匹配两条路径确定可复现 | `build_self_cases()` 8 个夹具用例（2 匹配 + 6 不匹配/缺失/不可用/未知声明） | `item001-selftest-*.log` `RESULT: PASS passed=8 failed=0` |
| 探针可读每路 `model_version` 与不可用原因 | `--config` 产出 `ModelAvailability`（profile/model_name/model_version/path/file_present/available/reason/detail）+ JSON | `item001-config-desc-*.log/.json`：front/yolov5s `available=1 reason=READY`；无证据 profile `reason=PROFILE_UNSUPPORTED` |

---

## 3. 关键实现决定与依据

1. **纯逻辑与真模型分层**：核验数学放在 `railhmi_core`（最小工具链可测），真实 `rknn_query` 放在 `railhmi_media`（target）。好处：host/板端都能跑确定性夹具，不需要 NPU 也能验证匹配/不匹配；真实读取只在板端发生，且不污染 core 依赖。
2. **无编译期 rknn 依赖**：用 `dlopen`/`dlsym` 绑定，避免依赖 `rknn_api.h` 安装路径，宿主无库也能编译；运行时无库返回 `RKNN_RUNTIME_UNAVAILABLE`（不伪造 ready）。
3. **最小自声明 ABI 已对照真实头文件核验**：`rknn_tensor_attr` 前缀字段顺序与 `/usr/include/rknn_api.h`（2.3.2）一致；枚举 `rknn_tensor_type`/`rknn_tensor_format`/`rknn_tensor_qnt_type` 与之一致。查询缓冲额外预留并按 `index` 回显、`n_dims` 合理性校验，异常显式失败。
4. **释放符号兼容**：真实 `librknnrt.so` 2.3.2 导出 `rknn_destroy`，**没有 `rknn_release`**。运行时先试 `rknn_release` 再试 `rknn_destroy`（见 §5 返工记录）。
5. **输入布局自适应**：真实模型输入为 **NHWC `1,640,640,3`**（不是训练文档常见的 NCHW），`normalize_chw` 依 `fmt` 归一，兼容 NCHW/NHWC 与格式未知时的启发式。
6. **结构而非名称**：真实 seg 输出张量名是 `789`/`onnx::ReduceSum_799` 等，证明不能靠名字判断；结构识别只用 dims/format。
7. **退出码语义**：`declared_failed` 只统计"目标 profile 有证据但模型缺失/加载失败/结构不符"；`PROFILE_UNSUPPORTED`（无证据）与 `NOT_READY`（文件在但未核验）是**预期状态**单独计数，`--strict-profiles` 才把 profile 缺种类计为失败。

---

## 4. 运行入口与命令

```
# core 单测（含本项新测试）
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests

# 夹具自检（可归档；无设备、无 rknn 库）
./build-core/tools/railhmi-model-probe --selftest --out selftest.log
./build-core/tools/railhmi-model-probe --selftest --json --out selftest.json

# 真实模型张量描述（板端，dlopen librknnrt）
./build-media/tools/railhmi-rknn-probe --model <dir>/yolov8s-seg.rknn \
    --out seg.descriptor.txt --json-out seg.json

# 用真实描述核验
./build-core/tools/railhmi-model-probe --descriptor seg.descriptor.txt --declared yolov8s-seg \
    --profile front --out verify-seg.log

# 按配置逐路汇总（<descriptor-map> 内为 <model_name>.descriptor.txt）
./build-core/tools/railhmi-model-probe --config ./config --models-dir <dir> \
    --descriptor-map <map> --out config.log --json --out config.json
```

---

## 5. 返工/修复记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1（CoreTests 首跑） | 编译失败 `use of an operand of type bool in operator++` | `one_count` 误声明为 `bool` | 改为 `int` | 重新构建 `BUILD_EXIT=0`，958/0 |
| r1（板端真模型） | `RKNN_RUNTIME_UNAVAILABLE`，`probe error: librknnrt.so missing rknn_init/rknn_query/rknn_release` | 2.3.2 未导出 `rknn_release`（只有 `rknn_destroy`）；且运行时不可用时错误地把 `file_present` 置 0 | 释放符号改成 `rknn_release`→`rknn_destroy` 回退；先判文件是否存在再判运行时 | `item001-rknn-*-r1.transcript.json` 记录失败；r3 两条真模型 `RKNN_*_EXIT=0` |
| r2 | 配置汇总把 `PROFILE_UNSUPPORTED` 计入 `declared_failed`，`RESULT: FAIL` 语义误导 | 计数口径过宽 | 分离 `declared_failed`/`unsupported_profiles`/`unverified`；`--strict-profiles` 才按 profile 缺种类失败 | r3 `declared_failed=0 ... RESULT: PASS`（r2 为过渡轮，证据已由 r3 取代） |

---

## 6. 边界与交接

- 本增量**不**实现预处理、检测/分割后处理、同帧两阶段、结果发布、语义白名单、跟踪、线程/调度接入、测距、P5 调试视图（对应 ITEM-002..013）。
- **P5 显示**：`build_ai_debug` 已消费 `AiRuntime` 的 `model_version`/`available`/`unavailable_reason`；本项提供的能力/原因词表是它的数据来源，实际运行期接线属 ITEM-012，本次未改 UI。
- **真实 Detector 接入**（替换 `SimDetector`）属 ITEM-009，本次未改 `AiWorker`。
- **配置缺口（交后续/需求确认）**：`app.ai.models` 只给 `front` 登记了 `yolov5s`，未登记 `yolov8s-seg`；而 front 两阶段需要 seg+detect 两种。探针已如实报 `profile.front ... yolov8_seg=missing`（`profile_gap=true`）。不在此增量私自改配置。
- 数据整理、训练、ONNX/RKNN 导出不由本能力处理；本项只读取现有 `.rknn`。

---

## 7. 已运行环境（真实证据）

| 项 | 值 | 来源 |
|---|---|---|
| 目标 | RK3588（192.168.0.103），Linux 6.1.141 aarch64，8 核 | `item001-r3` transcript `env` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 | 同上 |
| librknnrt | `/usr/lib/librknnrt.so`（7726232 B） | 同上 |
| RKNN API / 驱动 | `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` / `0.9.8` | `rknn_query(SDK_VERSION)` |
| 主机指纹 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（ssh-ed25519，受信任 known_hosts） | transcript |

详细证据索引见 `docs/vision/test_evidence/ITEM-001/ITEM-001-EVIDENCE.md`。
