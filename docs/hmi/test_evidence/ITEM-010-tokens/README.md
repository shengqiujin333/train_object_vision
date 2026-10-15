# ITEM-010 证据 — 设计令牌主题层

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-010`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §5.2（`tools/gen_tokens.py` / Tokens.qml）；`docs/hmi/VISUAL_DESIGN.md` §12（实现规则：不得硬编码）；`docs/hmi/design_tokens.yaml`（机器可读令牌）；`docs/hmi/TEST_DESIGN.md` TC-TOKEN-01、TC-SMOKE-15
- 源码: `app/tools/gen_tokens.py`、`app/tools/check_no_hardcoded_ui.py`、`app/src/ui/qml/{Theme,Tokens}.qml`（生成）、`app/src/ui/qml/qmldir`（生成）、`app/resources/tokens.json`（生成）、`app/src/ui/qml/main.qml`

## 1. 交付内容

1. **构建期生成器** `app/tools/gen_tokens.py`（Python+PyYAML）：由 `docs/hmi/design_tokens.yaml` 生成
   - `app/resources/tokens.json`（机器可读，C++/工具消费）
   - `app/src/ui/qml/Tokens.qml`（QML 单例：令牌数据）
   - `app/src/ui/qml/Theme.qml`（QML 单例：运行时主题外观，消费方使用 `Theme.*`）
   - `app/src/ui/qml/qmldir`（`singleton Theme/Tokens`）
   类型推断：hex → `property color`、`rgba()` → `Qt.rgba(...)`、整数/实数/布尔/字符串/列表/嵌套 → 对应 QML 类型；非标识符键做安全映射。
2. **运行时 Theme 单例分组**（均来自 design_tokens.yaml）：`color`、`alarm(L1/L2/L3)`、`stream_state`、
   `mode`、`integration_state`、`detection_badge`、`typography`（含字号 scale）、`spacing`、`radius`、`z_index`。
3. **UI 消费令牌**：`app/src/ui/qml/main.qml` 全部改为 `Theme.color.* / Theme.spacing.* / Theme.typography.scale.*`，
   不再硬编码色值/字号；`app/src/ui/CMakeLists.txt` 加入构建期 `railhmi_tokens` 生成目标。
4. **静态扫描** `app/tools/check_no_hardcoded_ui.py`：扫描 `app/src/ui/**`（排除生成的 Theme/Tokens.qml），
   禁止十六进制色值与 `pixelSize/pointSize/fontSize: <数字>`；命中即退出 1。
5. **生成器自检** `gen_tokens.py --selftest`：必需分组齐备、JSON 可解析、生成确定性、**令牌变更传播**到输出。

## 2. 实测

| 环境 | 步骤 | 结果 |
|---|---|---|
| 主机 | `gen_tokens.py`（生成） | 写出 `tokens.json` + `Theme/Tokens.qml` + `qmldir`（tokens.json 8903 B） |
| 主机 | `gen_tokens.py --selftest` | **8/8 PASS**（必需组/确定性/JSON/分组/变更传播） |
| 主机 | `check_no_hardcoded_ui.py` | **PASS**（扫描 `app/src/ui`，无硬编码色值/字号） |
| 板端（RK3588，登记 SSH 入口） | `python3 tools/gen_tokens.py --selftest` | `TOKENS_EXIT=0`，同样 8/8 PASS（板端 PyYAML 5.4.1） |
| 板端 | `python3 tools/check_no_hardcoded_ui.py --root src/ui` | `SCAN_EXIT=0`，PASS |
| 板端 | `run_core_tests.sh`（回归） | `274 passed / 0 failed`，`RESULT: PASS` |

“改令牌→重新生成→输出变化”由自检 `token change propagates to outputs` 复现：改 `color.bg_base` 后
`tokens.json` 与 `Theme.qml` 均变化；重启后界面元素随 Theme 绑定变化（运行期呈现需 Qt，见 §3）。

## 3. 边界

- **QML 未在板上编译/运行**：板端缺 Qt5 Quick/Qml 开发包（`build_env_check.md` §3.1），无法构建 QML；
  本项已核对生成物与消费方源码不硬编码，但“重启后界面随令牌变化”的**运行期呈现**需平台补齐 Qt 后由 UI 任务验证。
- 静态扫描范围限产品 UI（`app/src/ui`）；渲染 spike（`app/spikes/render_path`）为诊断工具，不在产品主题约束内。
- 令牌风格由 `ui_visual_design` 提供，本层不改需求/配色，只做生成与绑定。

## 4. 证据文件

- `build-TestRun-192_168_0_103-20261015-ITEM010-r2.transcript.json` / `.log`（板端 TestRun：tokens-selftest + ui-scan + 测试 + sim-inject）
- `...-ITEM010-r2.core-evidence_core-tests-...-r2.log` / `.xml`（测试归档 274/274）
- `...-ITEM010-r2.core-evidence_sim-inject-...-r2.log` / `.json`（同轮 sim-inject）
- 生成物：`app/resources/tokens.json`、`app/src/ui/qml/Theme.qml`、`Tokens.qml`、`qmldir`

## 5. 复现

```sh
# 生成 + 自检 + 静态扫描（主机或板端）
python app/tools/gen_tokens.py --yaml docs/hmi/design_tokens.yaml \
  --json app/resources/tokens.json --qml-dir app/src/ui/qml
python app/tools/gen_tokens.py --selftest --yaml docs/hmi/design_tokens.yaml
python app/tools/check_no_hardcoded_ui.py --root app/src/ui
# 经登记 SSH 入口（含 tokens-selftest 与 ui-scan）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-010-tokens --stamp <tag>
```
