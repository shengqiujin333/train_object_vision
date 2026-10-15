# ITEM-010 验证记录 — 设计令牌主题层（VERIFY-20261016-v1）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-010`
- 判定: **TEST_PASS**（本能力边界内的软件验证；QML 运行期呈现归 UI 任务，见 §5 边界）
- 验证者: software_tester.software_verification（Claude Code 独立会话）
- 时间: 2026-10-16 01:24 (UTC+8)
- 被测交付: commit `b15db1d`（embedded_linux_engineer.desktop_app_implementation → IMPLEMENTED，ITEM-010-r2）

## 1. 判据与结论总表

| # | 验收观察点 | 来源 | 方法与清单 | 结论 |
|---|---|---|---|---|
| 1 | 令牌由 design_tokens.yaml 生成 tokens.json / Tokens.qml / Theme.qml / qmldir | 任务描述 | 主机+板端独立复跑 `gen_tokens.py --selftest` 8/8 | PASS |
| 2 | Theme 单例含 color / alarm L1-L3 / stream_state / mode / integration_state / detection_badge / typography / spacing / radius / z_index | 任务描述 | 内容级一致性夹具 `verify_tokens_consistency.py`：tokens.json 与源 YAML 深度比对 0 处差异；15 个关键令牌探针；rgba 数值解码；qmldir 单例注册 | PASS |
| 3 | 改任一颜色或字号 → 重新生成 → 输出变化 | 任务描述 / TC-TOKEN-01 | 实现者自检颜色传播 + 我的扩展夹具 `verify_tokens_ext.py` 字号（h2 20→21）传播 | PASS |
| 4 | QML 与 C++ UI 无硬编码十六进制色值/字号 | 任务描述 / TC-SMOKE-15 | `check_no_hardcoded_ui.py` 主机+板端扫描 exit=0；扫描器负例夹具（注入违规→exit=1，注释/生成文件豁免正确） | PASS |
| 5 | 生成物正确编码（rgba 往返、数值保真） | 验证方案 | rgba(120,200,30,0.5) → Qt.rgba 数值往返；overlay_scrim rgba(6,9,12,0.72) 解码复算 | PASS |
| 6 | 非法/缺失输入行为 | 异常路径 | 缺组 exit=3；yaml 不存在 exit=2；扫描器 root 不存在 exit=2 | PASS |
| 7 | 回归（CoreTests 不退步） | 验证方案 | 板端独立复跑 `run_core_tests.sh`：274/274，junit lists 与实现者归档逐条一致，日志逐行一致（stamp 归一后 diff=0） | PASS |
| 8 | 板端边界（采纳自实现承诺） | 实现者 §3 | SSH 直查：qtdeclarative5-dev 未安装、QtQuick.2 qml 目录空 | 边界属实（见 §5） |

## 2. 独立板端复跑（20261016-VERIFY-ITEM010-v1）

入口：`app/tools/board_build.py --mode TestRun --stamp 20261016-VERIFY-ITEM010-v1`
（Paramiko SSH，192.168.0.103:22 admin，凭据 env `TRAIN_RK3588_SSH_CREDENTIAL`，主机指纹 SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU；transcript 前 20 行含完整调用参数）

| 步骤 | exit | 结果 |
|---|---|---|
| upload 76 files + design_tokens.yaml | 0 | — |
| testrun（CoreTests 回归） | 0 | 274 passed / 0 failed，`== 274 passed, 0 failed ==` |
| tokens-selftest | 0 | 8/8 PASS（必需组、确定性、JSON、Theme 探针、颜色变更传播） |
| ui-scan | 0 | scanned 2 files under src/ui，PASS |
| sim-inject | 0 | L1 告警 t=1000 注入出现 |
| cleanup | 0 | cleaned |

与实现者归档（…-ITEM010-r2）比对：junit `<testsuites tests="274" failures="0">` 相同；274 个 testcase 名称逐条一致；core-tests 日志 278 行 stamp 归一后 0 diff。比对器：`verify_board_compare.py`。

## 3. 扩展夹具（verify_tokens_ext.py，16 项全 OK）

实现者自检只断言了**颜色**变更传播，而 TC-TOKEN-01 要求颜色与字号都传播；以下边界由验证者补齐：

1. **字号传播**：typography.scale.h2 20→21 → 双轮全量生成 → Theme.qml differing、`size: 20`/`size: 21` 各自出现、tokens.json 数值同步。
2. **qmldir 每次**都含 `singleton Theme 1.0` / `singleton Tokens 1.0`。
3. **异常路径**：缺必需组 exit=3；yaml 文件不存在 exit=2；扫描器 root 不存在 exit=2。
4. **rgba 数值保真**：rgba(120,200,30,0.5) → Qt.rgba(0.470588, 0.784314, 0.117647, 0.5)，≤0.05/255 误差。
5. **扫描器力度**：qml 代码行 `#FF0000`、`font.pixelSize: 32` 命中 exit=1；Theme/Tokens.qml 生成文件豁免；`//` 注释内 hex 不误报（`//` 分割逻辑有效）；cpp/h 源内 QColor literal 命中。

### 过程轮修正（如实登记）

v1 过程轮我的夹具预期有误：`setPixelSize(14)` 调用形式与 `0xAABBCC` 数值字面量**本来就不在扫描器声明范围**（它禁止 `pixelSize: N` 赋值形状与 `#hex` 字面量，README §1.4 已透明）。修正夹具预期后通过；这不是产品缺陷。

### 观察项（交实现参考，不阻塞）

- **O-1**: 扫描器对**调用形式**（`setFontPixelSize()`、`setPixelSize(n)` 封装）与**数值字面量着色**（`0xAABBCC` 作色值）不识别，QML/Cpp 均以赋值形状和 `#hex` 为判据。当前产品 `src/ui` 无这类调用（全量扫描 exit=0），后续 UI 任务引入封装 API 时此盲区不会被静态扫描捕获，届时需扩展判据或约定封装层走 Theme 通道。
- **O-2**: `overlay_scrim` 在 tokens.json 中保留字符串 `"rgba(6,9,12,0.72)"`、在 Theme.qml 中转成 `Qt.rgba(...)`，两个通道数值一致（本验证复算通过）；C++ 消费此字符串需自行解析，属实现者官方口径，非缺陷。

## 4. 主机侧独立验证

- `gen_tokens.py --selftest`：8/8 PASS，exit=0。
- `check_no_hardcoded_ui.py`：scanned 2 files（main.qml、main.cpp），PASS exit=0。生成文件 Theme/Tokens.qml 按声明豁免。
- `verify_tokens_consistency.py`：tokens.json 与源 YAML 深度相等（0 mismatch）；15 个关键令牌探针（bg_base、三档 alarm、display/body_lg 字号、s4/spacing、radius.modal、z_index.alarm_banner、stream_state/mode/integration_state/detection_badge 组、text_primary、accent）全 OK；overlay_scrim Qt.rgba 数值解码 OK；qmldir 单例 OK；main.qml 引用 Theme 且无硬编码 hex。
- `verify_tokens_ext.py`：16/16。

## 5. 边界（与实现者 §3 一致，经我 SSH 直查确认）

- `qtdeclarative5-dev` 未安装、`/usr/lib/aarch64-linux-gnu/qt5/qml/QtQuick.2` 目录不存在（dpkg 仅 qtbase5-dev）→ **QML 在板上无法编译/呈现**。TC-TOKEN-01 的"重启后界面元素随令牌变化"运行期视觉确认归 UI 任务（Qt 环境补齐后）；本验证覆盖生成/扫描/绑定源正确性与回归不退步，未冒充不存在的视觉确认。
- NVR / 摄像头 / 硬件测试不在本能力。
- 静态扫描范围限 `app/src/ui`（render spike 为诊断工具）。

## 6. 证据文件（本目录）

- `VERIFY-ITEM-010.md` — 本文档
- `build-TestRun-192_168_0_103-20261016-VERIFY-ITEM010-v1.transcript.json` / `.log` — 我的板端 TestRun 全过程（含指纹）
- `...VERIFY-ITEM010-v1.core-evidence_core-tests-...{log,xml}` — 274/274 归档
- `...VERIFY-ITEM010-v1.core-evidence_sim-inject-...{log,json}` — 同轮 sim-inject
- `verify_tokens_consistency.py` / `verify_tokens_ext.py` / `verify_board_compare.py` / `verify_board_qt_check.py` — 验证者夹具（可复现）
- 实现者轮归档（…-ITEM010-r2.*）保留在本目录，用于逐行比对。

## 7. 复现命令

```sh
# 主机：内容一致性
python docs/hmi/test_evidence/ITEM-010-tokens/verify_tokens_consistency.py
# 主机：扩展夹具（字号传播、扫描器力度、异常路径、rgba 往返）
python docs/hmi/test_evidence/ITEM-010-tokens/verify_tokens_ext.py
# 板端：独立复跑（需要 TRAIN_RK3588_SSH_CREDENTIAL 与 192.168.0.103 可达）
python app/tools/board_build.py --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-010-tokens --stamp <stamp>
# 板端 Qt 边界直查（Paramiko）
python docs/hmi/test_evidence/ITEM-010-tokens/verify_board_qt_check.py
```
