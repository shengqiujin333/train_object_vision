# ITEM-011 证据 — 应用外壳与角色化导航

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-011`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §3（信息架构/导航）/§8（权限与角色）/§11#10、#14；`docs/hmi/DESKTOP_APP_DESIGN.md` §7.2；`docs/hmi/VISUAL_DESIGN.md` §8.1/§3.6；`docs/hmi/TEST_DESIGN.md` TC-SMOKE-07/TC-ROLE-*
- 源码: `app/src/core/include/railhmi/core/navigation.h`、`app/src/core/src/navigation.cpp`、`app/src/ui/qml/main.qml`、`app/src/ui/qml/components/{TopStatusBar,NavBar}.qml`、`app/src/ui/qml/pages/{PageView,DeniedView}.qml`、`app/src/ui/src/main.cpp`

## 1. 交付内容

1. **导航/权限模型（railhmi-core，纯逻辑、可单测）**：
   - 六页固定顺序 `realtime / alarm_center / replay / all_cameras / ai_debug / maintenance`（含稳定 id 与中文标题）；
   - 权限矩阵（交互 §8）：**司机**仅可见实时/报警/回放；**维护/研发/管理员**可见全部；
   - `visible_pages(role)` 按角色过滤的固定导航项；`default_page(role)`（司机→实时、维护→全摄像头、研发→AI调试、管理员→设备维护）；
   - `resolve_navigation(role, page)`：受限直达返回 **denied**（界面显示“无权限 + 返回”，不空白崩溃）；
   - `mode_display()`：主=`可见光`、次=`红外未接入`、`interactive=false`（本期无模式切换控件）。
2. **QML 应用外壳**（消费生成 Theme，ITEM-010）：
   - `main.qml` 单窗口：顶部 `TopStatusBar` + 固定 `NavBar` + 页面宿主（`PageView` / 受限 `DeniedView`）；
   - `NavBar.entries = railhmiNav`（C++ 按角色过滤后注入），受限直达切到 `DeniedView` 并“返回”复位；
   - `TopStatusBar` 模式位固定 `可见光` + `红外未接入`（灰显），**无切换控件**；
   - `main.cpp` 由 `cfg.settings.app.default_role` 计算 `railhmiNav`/`railhmiAllPages`/`railhmiDefaultPage`/模式位并注入 QML。
3. **无硬编码**：外壳/组件/页面颜色与字号全部来自 `Theme.*`；`check_no_hardcoded_ui.py` 覆盖 `app/src/ui`。

## 2. 实测（板端本机，经登记 SSH 入口；TestRun）

| 步骤 | 结果 |
|---|---|
| `railhmi-core-tests` | **319 passed / 0 failed**，`RESULT: PASS`（新增导航域 45 条断言） |
| 导航关键断言 | `driver sees 3 nav entries`、`driver nav = realtime/alarm/replay`、`driver nav excludes AI debug/maintenance/all cameras`、`maintainer sees all 6 nav entries`、`driver direct AI debug -> denied (show no-permission + return)`、`mode has no interactive toggle` |
| `gen_tokens.py --selftest` | `TOKENS_EXIT=0`（8/8） |
| `check_no_hardcoded_ui.py --root src/ui` | `SCAN_EXIT=0`（无硬编码色值/字号） |
| `railhmi-sim-inject` | `SIM_EXIT=0` |

依赖 `ui_visual_design`：布局/导航位置、模式位、无权限页样式以视觉规范为准（本层不改需求/配色）。

## 3. 边界

- **QML 未在板上编译/运行**：板端缺 Qt5 Quick/Qml 开发包（`build_env_check.md` §3.1），无法构建/启动 QML 外壳；
  本项已验证（a）权限/路由核心逻辑（板端 319/319）、（b）外壳/组件/页面源码无硬编码（板端扫描 PASS）、
  （c）QML 消费 C++ 注入的按角色过滤导航项。**“司机启动即不渲染 AI 调试/维护入口”的运行期呈现**需平台补齐 Qt 后由 UI/测试任务验证。
- 页面内容（六页具体实现）由后续工作项接入；本层交付外壳、导航与受限返回。

## 4. 证据文件

- `build-TestRun-192_168_0_103-20261015-ITEM011-r1.transcript.json` / `.log`
- `...-ITEM011-r1.core-evidence_core-tests-...-r1.log` / `.xml`（319/319）
- `...-ITEM011-r1.core-evidence_sim-inject-...-r1.log` / `.json`

## 5. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-011-shell --stamp <tag>
# 板上直接: ./build-core/tests/railhmi-core-tests ；python3 tools/check_no_hardcoded_ui.py --root src/ui
```
