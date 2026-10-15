# ITEM-016 软件验证 TestEvidence — 关键状态区与顶部模式信息条

- 判定轮: `VERIFY-20261016-v3`（本文件为该轮判定文档；v1/v2 过程轮留痕见 §7）
- 被验证交付: commit `2aa1af1`（embedded_linux_engineer.desktop_app_implementation returned IMPLEMENTED，ITEM-016）
- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-016`
- 依据: `DESKTOP_APP_DESIGN.md`（关键状态区/顶部条）；`INTERACTION_DESIGN.md` §9（异常点击跳转、无权限提示）；`VISUAL_DESIGN.md` §3.4/§3.5/§3.6/§7.6（颜色非唯一编码：级别=颜色+形状+文字）；`TEST_DESIGN.md` TC-SMOKE-09
- 结论: **TEST_PASS**（判据逐条满足，证据见下；边界如实标注）

## 1. 判据总表（工作项描述逐句）

| # | 验收判据（工作项） | 结果 | 证据 |
|---|---|---|---|
| J1 | 关键状态区七类状态（受电弓/轮轨/车门/摄像头/AI/录像/存储） | ✅ ext s0/s0b：混合快照 7 项顺序与验证人手填矩阵逐字段一致；core 用例 `seven key categories in fixed order`；probe 两角色 items=7 | ext 79/79 + core-tests `build-...VERIFY-20261016-v3.core-evidence_core-tests-...log` |
| J2 | 每项三重编码（颜色+形状+文字），正常简洁/异常突出 | ✅ ext e0(×5)/e0b/c/d/e：五级 shape(dot/spinner/triangle/square/minus-circle)、glyph(●/↻/▲/■/⊖)、label(正常/信息/警告/报警/未接入)、color(ok/info/degraded/error/neutral) 逐级显式核对；e0f/e0g 十对互异；a0 系列：Ok=非突出简洁，Warning/Alarm=abnormal；QML StatusItem 异常底色+三级前景色+字形+文字，正常项无底色 | ext run-ext 段逐行 + QML 静态接线 |
| J3 | 顶部信息条：时间/可见光模式位+红外未接入/录像/AI/系统健康 | ✅ ext s1/s1b/s1c/s1d/s1g/s1h：7 项固定序；time=neutral/none/无字形不可点；mode=dot·可见光+`红外未接入`副字、无切换控件；AI alarm→P5、健康 warning triangle→P6；QML TopStatusBar 时间由 Timer 刷新 | ext + QML |
| J4 | 未接入项（车速/门磁）显示"未接入" | ✅ ext s1e/s1f：item[5]/item[6] 完整三重 minus-circle/⊖/未接入，label 逐字核对；probe 日志 `vehicle_speed ... value=未接入` 两角色一致 | ext + probe 两角色 |
| J5 | 黑白截图下形状+文字可辨 | ✅ e0f/e0g 5×4/2 十对 glyph/label 互异（黑白印刷时形状与文字仍唯一分辨）；形状 dot/spinner/triangle/square/minus-circle 五种互不重合（e0b 逐级 + e0f 两两） | ext |
| J6 | 异常项点击跳转对应详情页，无权限则提示 | ✅ p0/p0b：七类目标页手对照表 P4/P4/P4/P6/P5/P3/P6 + 四角色×7 异常项 allowed == `role_can_access`；s2 系列：driver 版异常仅 AI 可点且 allowed=false（对应 UI"无权限提示"路径）、admin 全通；probe 独立解析两角色 fails=[]；QML `onStatusNavigate` 设置 `denied` 显示无权限 | ext + probe-assert 两角色 + QML 接线 |

## 2. 板端独立复现（验证人独立 SSH 轮，stamp VERIFY-20261016-v3）

- 指纹: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（不一致即中止；见 `VERIFY-FIXTURES-...v3.log` 第 1-2 行/transcript）。
- **交付入口复跑**: board_build.py TestRun（ENTRY_EXIT=0）在板端原位构建 → `build-TestRun-192_168_0_103-VERIFY-20261016-v3.core-evidence_core-tests-VERIFY-20261016-v3.log` **443/[PASS] 行**。
- **逐行比对**（程序化，不采信交付侧描述）: 验证轮 443 行 == 实现者 `build-TestRun-...-ITEM016-r2...log` 443 行，`identical=True`（impl=443 verify=443）。
- **JUnit XML 独立解析**: `tests=443 failures=0`；status 域用例 status_cases=25（≥8 阈值）。
- 回归: tokens selftest / UI 无硬编码扫描 / sim-inject / 归档下载全走完（见 `build-...VERIFY-20261016-v3.transcript.json`）。

## 3. 交付 `railhmi-status-probe` 两角色独立断言（不复用交付自身的 acceptance 行）

| 角色 | 独立解析判据 | 结果 |
|---|---|---|
| maintainer（mixed） | 7 项固定序/三重编码 5 值集/ abnormal↔clickable 不变式/异常项 target 与 allowed=true/未接入项/顶部项身份与模式副字/PASS 行 | `{"fails": [], "items": 7, "tops": 7}` |
| driver（mixed） | 同上 + 全部异常项 allowed=false（无权限路径） | `{"fails": [], "items": 7, "tops": 7}` |
| 负例 arg | `--nope` 退出码 2，stderr `unknown arg: --nope` | N1_EXIT=2 |
| 负例 role | `--role nobody` 退出码 2，stderr `bad role: nobody` | N2_EXIT=2 |

探针原文: `build-...VERIFY-20261016-v3.core-evidence_status-probe-...log`（maintainer）与 `...status-probe-driver-...log`（driver）。

## 4. 验证自有扩展夹具（79/79，链接交付 librailhmi_core.a）

编译 GXX_EXIT=0（零警告），运行 EXT_EXIT=0：

- **e0 组（30）**: 五级 to_string/shape/glyph/label/color 逐级显式核对（25）+ 十对 glyph/label 两两互异（e0f/e0g，黑白可辨依据）
- **a0 组（9）**: 异常精确性（仅 Warning/Alarm 突出）+ Info 级在 build_key_status 的传递（spinner/↻/info/值直通/非突出）+ 空值→级别默认文案
- **s0 组（6）**: 混合快照 7 项固定序与手填矩阵逐字段一致/隔离性（abnormal 仅 warning/alarm）/值直通/空值默认（正常/报警）
- **s1 组（9）**: 顶部 7 项 kind+key 固定序；time/mode/车速/门磁不可点；mode 含可见光+红外未接入副字；AI alarm→P5；系统健康 warning triangle→P6
- **s2 组（4）**: driver/admin 权限矩阵 + 默认快照全不可点
- **p0 组（2）**: 七类目标页手对照（P4/P4/P4/P6/P5/P3/P6）+ 4 角色×7 异常项 allowed == role_can_access（28 组合）
- **d0/d1 组（4）**: 两构建函数确定性（两次调用逐字段一致）+ 项目不耦合角色

## 5. QML 接线静态核对

QML 无法板端执行（E-01b Qt5Quick/Qml 缺失，ITEM-010..015 同边界），静态结构核对：

- StatusItem 异常项 bg + level 色 + glyph + label/value；正常项简洁无底色；点击仅在 abnormal 发信号
- KeyStatusStrip 七项来自 `railhmiKeyStatus`（main.cpp 注入 build_key_status，含按角色预算 allowed），无字面量状态值
- TopStatusBar 时间 Timer 刷新；mode 无切换控件；未接入项不可点
- shell.onStatusNavigate(pageId, allowed) → `currentPageId = pageId; denied = !allowed`（异常点击跳转/无权限两条路径唯一收口）

## 6. 边界（如实）

- **QML 运行期未执行**: 同前序项边界；黑白截图可辨性由 glyph/label 互异性（三重编码非颜色通道）保证机器可断言，逐帧视觉呈现归用户确认。
- **实时数据源未接入**: AI/录像/系统健康在 snapshot 缺省时按设计以级别默认文案呈现，车速/门磁未接入项为设计输入；E-01b 与数据源接线归对应后续项，不属 ITEM-016 缺陷。
- **权限四角色全矩阵**在 core 层显式核对（p0b 28 组合），QML 侧 denied 提示为静态接线核对。

## 7. 过程轮留痕

- **v1**: 夹具 mkdir 漏建验证工作目录 → ext 上传 ENOENT，主链（443/443 一致、JUnit、探针）已完成；文件留痕 `VERIFY-FIXTURES-...v1p.log/.json`。
- **v2**: 主链全绿，ext 编译步 `cd /tmp/railhmi_build/app` 为验证侧模板错误（TestRun 远端根即 /tmp/railhmi_build，无 /app 子目录，从 ITEM-015 目录布局误推），v3 修正。
- **v3 权威轮**: 全步绿（见 §2-§4），EXIT 全 0，N1/N2=2，清理确认 `fixture_cleanup_done` + 目标目录按名消失。

## 8. 文件清单（本轮验证产出）

- `VERIFY-ITEM-016.md`（本文件）
- `verify_status16_ext.cpp` / `verify_status16_fixtures.py` / `run_verify_round.sh`
- v3（权威）: `VERIFY-FIXTURES-VERIFY-20261016-v3.log` / `.transcript.json`；`build-TestRun-...VERIFY-20261016-v3.log` / `.transcript.json` 及 `...core-tests log/xml`、`...sim-inject`、`...status-probe(-driver)` 归档
- v1/v2 留痕: `VERIFY-FIXTURES-VERIFY-20261016-v1p.*` / `...v2.*`（见 §7）
- 实现者原档: `build-TestRun-...20261016-ITEM016-r1/r2.*`（对比基准，不属于本轮产出）
