# ITEM-014 软件验证 TestEvidence — 视频渲染与实时监控页三联布局

- 判定轮: `VERIFY-20261016-v1`（本文件为该轮判定文档）
- 被验证交付: commit `6cb9122`（embedded_linux_engineer.desktop_app_implementation returned IMPLEMENTED，ITEM-014）
- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-014`
- 依据: `DESKTOP_APP_DESIGN.md` §6.1/§7.1；`INTERACTION_DESIGN.md` v2 §7.1（左-前-右、前方最大）；`VISUAL_DESIGN.md` v2 §8.1（三联 26/48/26）；`TEST_DESIGN.md`
- 结论: **TEST_PASS**（判据逐条满足，证据见下；边界如实标注）

## 1. 判据总表

| # | 验收判据（工作项描述） | 结果 | 证据 |
|---|---|---|---|
| J1 | 三联顺序左-前-右，前方面积最大 | ✅ 板端 377/377 实测含两档分辨率断言；验证夹具 s0a/s0b 鉴别力反例 + r 目录 7 尺寸矩阵 | VERIFY-FIXTURES...log `[run-entry]`/`[run-ext]` |
| J2 | 1920x1080 与 1280x720 两档下 P0 元素（三联、关键状态区、当前报警条、顶部状态条）不滚动即可见 | ✅ 板端 `1920x1080/1280x720 P0 elements fit without scroll` 逐项 PASS；夹具 g0/g1 精确几何手算逐字段一致（used==height 恒等式 w0 扫描 720..2160） | 同上 + ext `g0/g1/w0` |
| J3 | 一级报警横幅不遮挡前方中央可视区 | ✅ 板端两档 `L1 alarm banner does not cover front center` PASS；夹具实测裕量 1920: front_center.y=243 − banner 底=112 = **131px**；720 tier: 166−112 = **54px**；横幅遮挡反例 s0d 判 true | ext `g0l/g1l/s0d/c0a/c0b` |
| J4 | 调整窗口尺寸时三联顺序与比例保持 | ✅ 板端 `16:9 ratios preserved` + `resized layouts keep order` PASS；夹具 7 尺寸矩阵（800x480..3840x2160）+ 宽度扫描 640..3840 step8 两档高度全绿 | ext `r/w1/s0f` |
| J5 | 渲染路径（默认 cpu，gl 可开关）接入 QML | ✅ 配置 `app.yaml render_path: cpu`（ITEM-002 板端实测选定）；`main.cpp` 经 `cfg.settings.app.render_path` 解析注入 `railhmiRenderPath`；`VideoSurface.qml` 消费 `railhmiRenderPath` 并显示 `render:<path>`；q6/q7 静态核对 | VERIFY-QML-WIRING...log |
| J6 | 两档 P0 元素核对时视觉可观察 | ⚠️ 部分：几何/P0 可见性以板端 core 单测 + 精确像素几何证明；QML 运行期呈现（E-01b Qt5 Quick/Qml 开发包缺失，板端无法编译运行 QML，历史 ITEM-010/011/012 同边界）以静态 30/30 接线检查 + 结构核对为证；**真实视觉确认归用户**，不在自动验证判据内 | `verify_qml_wiring.py` 30/30 |

## 2. 板端独立复现（验证人独立 SSH 轮，stamp VERIFY-20261016-v1）

- 入口: `bash tests/run_core_tests.sh`（交付入口）；`TESTRUN` 构建 0，测试 **377 passed / 0 failed**。
- **逐行比对**（程序化）: 验证轮归档 `[PASS]` 行 **377** == 实现者 `build-TestRun-...-ITEM014-r1.core-evidence_core-tests-...log` 377 行，`identical=True`。
- JUnit XML 独立解析: `tests=377 failures=0`。
- 主机指纹: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现者 ITEM-014 transcript 一致；检查见 `VERIFY-FIXTURES-...transcript.json` / `.log`）。
- tokens selftest `TOKENS_EXIT=0`；UI 无硬编码扫描 `SCAN_EXIT=0`（13 文件）；sim-inject `SIM_EXIT=0`（63 行结构化，与实现者归档同形）—— 整套 core 回归均在验证轮独立复跑。

## 3. 验证自有扩展夹具（46/46，超出实现者 test_layout.cpp 范围）

编译: 验证夹具 `verify_layout14_ext.cpp` 链接交付构建出的 `librailhmi_core.a`（g++ -std=c++17 -Wall -Wextra，0 警告）。
关键断言组:

- **精确几何身份（g0/g1，24 断言）**: 两档验收分辨率手算常量逐字段 == `compute_layout` 输出
  - 1920x1080: margin=24; top(0,0,1920,48); banner(0,48,1920,64); row(24,72,1872,856); left(24,72,**487**,856); front(511,72,**898**,856); right(1409,72,487,856); front_center(691,243,539,514); alarm bar(0,1016,1920,64); status(0,952,1920,64)
  - 1280x720: margin=16; row(16,64,1248,512); left(16,64,**324**,512); front(340,64,**600**,512); right(940,64,324,512); center(460,166,360,307)
  → 未依赖判据函数的中间层，直接锁死像素几何，防止"判据全真但几何漂移"
- **判据鉴别力（s0 系，7 断言）**: 伪造坏布局（前排顺序错/前方小于左/行超高/横幅盖住中央/比例漂移 0.26→0.10）→ 对应判据必须返回 false/true；真实两档输出全部为 true。防止恒真判据
- **尺寸域（r/w 系，10 断言) 800x480..3840x2160 七档 + 高度扫描 720..2160 step8（row-h 恒等式、used==height、P0 fit、横幅不遮中央逐点成立）+ 宽度扫描 640..3840 step8 两档高度（全部判据 + 与 1920x1080 比例保持）**
- **裕量实测（c0）**: 1920 档横幅底到前方中央顶 **131px**；720 档 **54px**
- **确定性/健壮性（d0/d1）**: 同输入两次计算逐字段相等（1366x768）；0 尺寸不崩溃且不声称可见面板

## 4. QML 接线静态核对（30/30，主机执行）

QML 无法板端执行（E-01b），按既定验证口径以静态结构核对为主：

- 三联 Row 子项顺序 `tileLeft` → `tileFront` → `tileRight`；宽度 = `(row.width - 2*gap) * page.L.{left,front,right}Ratio`
- 页面内无游离比例字面量（仅 typeof 守卫的注入缺失回退块，注入失效才用）——`main.cpp` 由 `compute_layout(1920,1080)` 计算注入，**core 为单一来源 → 满足"调整窗口尺寸时比例保持"且不产生双源**
- P0 锚定链：报警条贴底（全宽）→ 状态区在报警条上方 → 三联行底部受状态区上沿约束 → 顶margin 由布局 margin 决定；四者不重叠即"不滚动即可见"的结构前提
- 渲染路径链：`app.yaml render_path: cpu` → `main.cpp` 解析注入 `railhmiRenderPath` → `VideoSurface.qml` 消费并显示 `render:cpu`；gl 开关仅经配置
- 非 LIVE 占位层与实时层互补（`visible: surface.live` / `visible: !surface.live`），旧帧不冒充实时
- 司机 P1 页三块 tile `showBadge: false`（与 ITEM-012 徽标可见范围契约一致）

## 5. 边界（如实）

- **过程轮自查记录**: 主机静态检查首跑曾报 q1e 失败（把 P1Realtime.qml 中 `typeof railhmiLayout` 守卫的注入缺失回退块内的比例字面量误判为双源）；经复核该块仅在注入失效时兜底、正常路径消费 railhmiLayout，属验证侧期望构造错误，自查更正判据（q1e/q1f 归位）后为最终结果，首跑留痕于本文件编辑历史与外包夹具脚本，不作判据

- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；上线呈现、视觉体验与交互需用户核验或在 Qt 就绪后补测。几何与 P0 可见性已由核心几何单测（板端）+ 精确像素几何夹具 + 静态结构核对证明
- **画面内容为静态占位**：三块 tile `live: false`（media 层帧接入在 ITEM-013 数据链路已独立验证；到 QML 运行期的逐帧呈现属 E-01b + 用户视觉确认边界）
- 报警横幅浮层 z-order/内容渲染在报警中心项实现；本项已证几何上不遮挡前方中央
- 渲染路径 `gl` 仅配置可切换（ITEM-002 实测板端 qmlglsink/Qt5Quick 缺失，不作为可用路径宣称）

## 6. 文件清单（本轮验证产出）

- `VERIFY-ITEM-014.md`（本文件）
- `verify_layout14_ext.cpp` / `verify_layout14_fixtures.py` / `run_verify_round.sh`（验证夹具与驱动）
- `VERIFY-FIXTURES-VERIFY-20261016-v1.log` / `.transcript.json`（板端复现全过程：指纹/逐命令/退出码/清理）
- `VERIFY-FIXTURES-VERIFY-20261016-v1.core-evidence_core-tests-VERIFY-20261016-v1.log` / `.xml`（验证轮板上交付入口归档 stdout + JUnit 377/0）
- `VERIFY-FIXTURES-VERIFY-20261016-v1.core-evidence_sim-inject-VERIFY-20261016-v1.log` / `.json`（回归）
- `verify_qml_wiring.py` / `VERIFY-QML-WIRING-VERIFY-20261016-v1.log`（静态接线核对 30/30）

## 7. 复现

```sh
# 验证轮（需 TRAIN_RK3588_SSH_CREDENTIAL 已在 Windows User env）
bash docs/hmi/test_evidence/ITEM-014-layout/run_verify_round.sh
# 静态接线核对（主机）
python docs/hmi/test_evidence/ITEM-014-layout/verify_qml_wiring.py
# 板端交付入口
bash app/tests/run_core_tests.sh
```
