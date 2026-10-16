# 软件验证证据 v4 — ITEM-020（历史回放页 P3，UI 评审 v2 缺陷修复轮复核）

- Capability: `software_tester.software_verification`
- 被验证对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `cbd0a93`，stamp `20261016-ITEM020-uifix-r2`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/replay.h`、`app/src/core/src/replay.cpp`、`app/src/ui/src/replay_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/core/tools/replay_probe.cpp`、`app/tests/core/test_replay.cpp`、`app/tools/check_replay_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-020-replay/README.md`
- 被验证工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-020`（历史回放页）
- 上一轮（本能力）: `VERIFY-ITEM-020-v3.md` **TEST_PASS**（579/579 + ext 100/100 + 接线扫描 46 项）；v3 结论对核心 `AlarmMark` 语义与 QML 映射成立，但**未覆盖桥接 `timeline()` 是否传递真实级别**——该盲区由 UI 评审 v2（`REVIEW-ITEM-020-v2.md` D-01, severity）抓出。
- 修复轮内容（源码 diff `a718a53..cbd0a93` 独立核对）：core 新增 `alarm_marks_from_rows(rows, camera_id)`（由带真实级别的报警行推导标记）；桥接 `timeline()` 改用 `AlarmMark` 重载并在无行时回退 ts-only（L3）；P3 游标顶部时间随游标 x 移动并标“当前”；三联面板标题恢复中文 `chipName(modelData)`；replay-probe 新增 `type=level-e2e` 行；core-tests 新增 4 断言；`check_replay_wiring.py` 新增 3 判据。
- 上游评审: `web_frontend.ui_implementation_review` v2 **TEST_FAIL**（D-01 级别色未流经桥接；D-02 游标时间固定左端；D-03 面板丢失中文名）
- 验证日期: 2026-10-16；本机拷贝工作目录 `/tmp/railhmi_build`（RK3588 本机，192.168.0.103，Paramiko 入口）；证书指纹核验一致 SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU
- **判定: TEST_PASS**

---

## 0. 结论与口径

1. 依据统一环境和登记授权（Paramiko入口复跑交付入口），全部结论取自**本次独立板端复跑**（stamp `VERIFY-20261016-v4`），与实现者交付自证轮（uifix-r2）分开。历史成功不是当前通过；本轮全部为当前实测。
2. 上一轮 v3 的 TEST_PASS 结论在 core/QML 关键路径范围成立；本 v4 扩大覆盖**桥接级真实级别端到端**（v2 评审 D-01）之后，修复轮交付全路径复核为绿。
3. 三项修复均逐条独立确证（第 3 节）；v1/v2/v3 全部历史防回归断言保留且通过（第 4 节）。
4. 范围边界与历史轮一致：真实回放取流/解码（媒体层）、NVR 可达性（设备维护）、QML 运行期（板端缺 Qt5 QML 开发包）不在本项通过口径内（见第 7 节），不影响本轮判定。

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被验证实现 | `cbd0a93`（uifix-r2） | 修复轮：core `alarm_marks_from_rows` + 桥接改用 marks 重载（无行回退 ts-only L3）+ P3 `cursorTime` 随游标 x + 面板 `chipName(modelData) + " (id)"` + probe `level-e2e` 行 + 4 新断言（583 总数） |
| UI 评审 v2 | `d6851ff` | D-01 级别色未流经桥接（恒 L3）；D-02 游标时间固定左端；D-03 面板丢失中文名 |
| 本能力 v3 | `68d365c` | 579/579 + ext 100/100 + 接线 46 项 mismatch=[] |
| 交互/视觉设计 | `ef47841` / `40ccf4e` | §5.3 / §7.7·§8.3·§3.4（级别色彩语义） |

## 2. 验证方法（本轮实际执行）

1. **独立复跑交付入口**：`board_build.py --mode TestRun --keep-remote`（子进程、1200s 超时上限，stamp `VERIFY-20261016-v4`）→ `TESTRUN_EXIT=0`，产物归档至本目录。
2. **逐行 [PASS] 比对**：fresh 核心单测日志 vs 实现者 uifix-r2 归档日志（程序化列表相等）+ 独立 JUnit 解析（583/0）+ 45 个 ITEM-020 判例名域断言。
3. **replay-probe 独立解析断言**：v3 全部行格式 + 新增 `type=level-e2e` 行与 8 标志 `type=acceptance` 行；与 uifix-r2 归档 probe 日志逐行比对（确定性）。
4. **验证侧 ext 夹具 v4**（`verify_replay20_ext_v4.cpp`，115 检查）编译针对**本轮 fresh build-core 静态库**（非交付自测二进制）。
5. **接线扫描 v4**：对 fresh 远端拷贝源码执行 59 项断言（v1/v2/v3 状态全保留 + v4 三修复反向断言）。
6. **CLI 负例与确定性**：`--nope` → exit 2；`--help` → exit 0；二次运行 diff == fresh 归档（IDENTICAL）。
7. **回归留痕**：同 TestRun 轮 sim-inject / status / ai / alarm / clip 探针全在。
8. 名单清理（/tmp/railhmi_build、验证工作目录），尾随 `ls` 确认为空。

## 3. 三项修复的独立确证（本轮实测）

**D-01（严重）真实级别端到端流经桥接** — 三步取证闭环：
1. **夹具 v4 合同层**（板上 ext，115/115 含新增 13 项）：
   - `alarm_marks_from_rows({L1@65000 front, L2@9000 roof, L3@500000 front}, "CAM-01")` → 恰 2 标记，级别 `AlarmLevel::L1`/`L3` 原样（`marks from rows carry real level` 系）；
   - `"CAM-04"` 过滤 → 恰 1 标记 `L2`；`camera_id=""` → 全 3 行；无行 → 空（桥接据此走回退）；未知相机 → 空（不静默放宽）；
   - **端到端 L1/L2/L3 循环**（同桥接路径）：rows → marks → `build_timeline(AlarmMark 重载)` → `segments[0].level == lvl && alarms[0].level == lvl` 对 lvl=1,2,3 全部成立；
   - **按相机隔离**：front 过滤 marks 建 CAM-04 时间轴 → 该相机 critical 段 level=0（不串色），反向同样。
2. **probe 层**（板上 fresh 日志原文）：`type=level-e2e alarm_level=1 segment_level=1 marker_level=1` 且 `type=acceptance ... level_e2e=true ... result=PASS`。
3. **接线扫描层**（fresh 源代码）：桥接调用 `alarm_marks_from_rows(alarm_rows_, camera_id)` + marks 重载（`V4_bridge_marks_*` 全 True）、核心 helper 声明/定义/取 `r.level` 三项全 True、ts-only L3 回退分支保留（`V4_bridge_fallback_kept=True`）。

**D-02（轻）游标顶部时间随游标移动 + “当前”标注**（接线扫描 v4）：
- `cursorTime` 的 `x: Math.max(0, Math.min(parent.width - width, page.xOf(page.cursor) - width / 2))`（`V4_cursor_follow_explicit=True`）；
- **反向断言**：`cursorTime` 块内 `anchors.left: parent.left` 已不存在（`V4_cursor_left_anchor_absent=True`，即不再锚定左端）；
- `text: "当前 " + formatTime(cursor)`（`V4_cursor_labelled_current=True`）。

**D-03（轻）三联面板恢复中文名**（接线扫描 v4）：
- 面板标题 `chipName(modelData) + " (" + modelData + ")"`（`V4_panel_chip_name/V4_panel_id_suffix=True`）；
- **反向断言**：修复前形态 `text: modelData + " "` 不存在（`V4_old_bare_label_absent=True`）。

## 4. 全量回归（本轮 fresh 实测数字）

| 步骤 | 结果 |
|---|---|
| 交付入口 TestRun | `TESTRUN_EXIT=0`（构建+单测+探针+tokens-selftest+ui-scan+wiring 全链） |
| 核心单测 | **583 passed / 0 failed**；逐行与 uifix-r2 归档 **identical=True**；JUnit tests=583 failures=0 |
| ITEM-020 判例域 | 45/45 named cases 全部命中（v1 29 + fix 7 + uifix-r1 5 + uifix-r2 4），missing=[] |
| replay-probe | 独立解析 fails=[]；15 行与 uifix-r2 归档完全一致；二次运行 diff=**IDENTICAL** |
| ext v4（验证侧夹具） | **115 passed / 0 failed**（v3 的 100 项全保留回归 + 15 项 alarm_marks_from_rows 合同/端到端/隔离断言） |
| 接线扫描 v4 | 59 项 wants 全部命中，**mismatch=[]** |
| CLI 负例 | `--nope`→2，`--help`→0 |
| 回归留痕 | sim-inject / status-probe / status-probe-driver / ai-probe / alarm-probe / clip-probe 全在（同轮 core-evidence） |

## 5. 关键证据文件

- `build-TestRun-192_168_0_103-VERIFY-20261016-v4.log`（入口全链原文）
- `build-TestRun-192_168_0_103-VERIFY-20261016-v4.core-evidence_core-tests-VERIFY-20261016-v4.log` / `.xml`（583/0 + JUnit）
- `build-TestRun-192_168_0_103-VERIFY-20261016-v4.core-evidence_replay-probe-VERIFY-20261016-v4.log`（含 `level-e2e` 行）
- `VERIFY-FIXTURES-VERIFY-20261016-v4.log` / `.transcript.json`（夹具轮全部步骤与断言原文）
- 验证工具（验证侧资产，非产品代码）：`verify_replay20_fixtures_v4.py`、`verify_replay20_ext_v4.cpp`、`run_verify_round_v4.sh`、`offline_selfcheck_v4.py`
- 实现者归档对照源：`build-TestRun-...-ITEM020-uifix-r2.*`（uifix-r2 全套）

## 6. 独立复现

```sh
bash docs/hmi/test_evidence/ITEM-020-replay/run_verify_round_v4.sh
# 离线先行（不占板）: python docs/hmi/test_evidence/ITEM-020-replay/offline_selfcheck_v4.py
```

主机指纹核验（RejectPolicy + 显式期望指纹）与凭据仅注入进程环境（不打印）沿用登记入口契约。

## 7. 范围与未覆盖（不改变判定）

1. **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（历史已确证，本轮未复测该环境项）；UI 接线级用户可观察点以“对 fresh 副本源码的 59 项扫描”取证（v3 已确立的方法），运行期视觉手感留给 Qt 环境。
2. **真实回放取流/解码/关键帧图像**：属媒体层（GStreamer 播放链），本项验证模型/时间轴/同步/来源/详情与 QML 接线。
3. **NVR 可达性**：`nvr_available=false`（通道映射/账号未提供），界面“NVR 录像不可用，回放本机片段”显示正确；NVR 探测归设备维护项。
4. **数据接入**：应用未打开数据库，样例数据注入（`app.simulation=true`）；真实持久化接线归后续集成。

## 8. 判定

**TEST_PASS**（范围：ITEM-020 历史回放页 vs 任务描述与 `INTERACTION §5.3` / `VISUAL §8.3·§3.4`）。

- v2 评审三项缺陷（D-01 级别色未流经桥接、D-02 游标时间、D-03 面板中文名）在修复轮 `cbd0a93` 全部闭环，并有本轮独立的板端复跑、加密指纹核验、核心单测 583 + ext 115 + 接线 59 项检查、逐行 JUnit/probe 比对与 mismatch=[] 支撑。
- 全部历史防回归断言（v1/v2/v3 轮所有缺陷修复状态）在本轮 fresh 复跑中保持成立。
