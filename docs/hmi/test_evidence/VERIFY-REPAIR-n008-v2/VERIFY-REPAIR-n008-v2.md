# VERIFY-REPAIR-n008 v2 — 独立软件验证记录（修复轮 2：F-01/F-02 之后，complete_delivery）

- Capability: `software_tester.software_verification`（节点 `repair_n008_..._check_1` 复核，RESUME_SYNC）
- 被验证对象: 修复轮 2 `repair_n008_embedded_linux_engineer_desktop_release_implementation`
  （commit `a92d008`，stamp `20261017-repair-r3`(Xvfb 六页导航截屏) / `20261017-repair-r4`(板端 TestRun)），
  声明工件 `app/README.md`，实现证据 `docs/hmi/test_evidence/REPAIR-n008-release-qt/README.md` §6
- 验证范围: `complete_delivery`（replaces n006；对 check_2 UI 评审 `REVIEW-REPAIR-n008.md` 后的修复版本复核）
- 上游状态: 首轮验证 `VERIFY-REPAIR-n008.md`（对 commit `450c43b`，TEST_PASS）；随后 UI 评审
  (check_2) 以实际导航发现 **F-01**（P3 `chipName` 未定义 → 三联标题 TypeError）与 **F-02**
  （缩略图相对路径基址错误 → 48 次 Image 打不开）→ 修复轮 2 → **本轮**。
- 验证轮: `VERIFY-20261017-v2`
- 结论: **TEST_PASS**（范围见 §6；不改变既有判据，不含媒体层/真实取流工作项）

## 0. 版本锚定

| 项 | 值 |
|---|---|
| 被验证实现 commit | `a92d008`（本轮全部板端复跑读取**当前工作树** HEAD=a92d008） |
| `packaging/VERSION` | 0.1.0 |
| 核心域变更 | 本修复轮仅动 `app/src/ui/qml/pages/P3Replay.qml`（+23/-1）与 `app/tools/check_qml_qt_validity.py`（+14）——core/platform 无任何变更（与 verify-v1 轮 TestRun 基线同源） |
| 实现方归档 | repair-r4 TestRun 归档（14 份）与本轮汇报在 §2 逐字节比对 |

## 1. 修复内容核对（源码级，按 UI 评审修复要求逐条）

| 缺陷 | 修复声明确认（源码核对 + 独立复跑） |
|---|---|
| F-01 `chipName` 未定义 | `P3Replay.qml:65-70` 新增配置驱动 `function chipName(camId)`：遍历 `allCams`（:15 `railhmiCameras` 回退空数组），命中 id 且 name 非空则返回名称，否则 `return "" + camId`（未知 id 回退）。调用点 `:201` 使用本文件定义 → 不存在未定义成员。**数据源独立验证**: registry `build_registry` 对 CAM-01/02/03 透传 cameras.yaml 中文名（本轮 ext 夹具 S1，`板端 16/16` §5） |
| F-02 缩略图相对基址 | `P3Replay.qml:72-80` 新增 `thumbSource(t)`：仅接受 `image://`/`file://`/`qrc:/` 前缀或绝对路径（转 `file://`），其余返回空串；`Image.visible` 绑定非空（缺失时显示 `—` 占位，不报 Image 打不开）。调用点 `:324-327` 不再绑定 `modelData.thumb` 原**值** |
| 同类守卫 | `check_qml_qt_validity.py` 新增"根 id 未定义成员调用"检查（`:67-77`）；反例自检 NEG_F01=1（验证方独立复跑，§3 R3） |

## 2. 独立板端复验（不信任实现方归档）

板端 RK3588 `192.168.0.103`，Paramiko（指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，
Reject/核验策略），凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`。

### R1 目标 Qt Release 构建 + offscreen 装载冒烟（同一入口复现为反差反转）
- `python app/tools/board_release_build.py --host 192.168.0.103 --user admin
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2 --stamp 20261017-verify-v2
  --no-install-deps --no-bundle --build-timeout 900` → `RELEASE_BUILD_EXIT=0`，产物
  `build-target/src/ui/railhmi` ELF aarch64 944016 B；offscreen 冒烟 `smoke_exit_parsed=124`
  （12 s 生存期 = QML 全装载 + 事件循环），日志 `configuration OK: 6 cameras` / `render path: cpu`
  无 QML 错误（`release-build-...verify-v2.json` / `smoke-...verify-v2.log`）。

### R2 板端全量 TestRun 独立复跑 + 与实现方逐字节比对
- `python app/tools/board_build.py --mode TestRun ... --stamp 20261017-verify-v2 --keep-remote`
  → `TESTRUN_EXIT=0`，core **670 passed / 0 failed**，19 项（TOKENS/SCAN/REPLAY_WIRE/AIDBG_WIRE/
  MAINT_WIRE/PERM_WIRE/HEALTH_WIRE/QML_VALID/PACK/SIM/STATUS×2/AI/ALARM/CLIP/REPLAY/MAINT/
  PERM/HEALTH/FAULT）全 EXIT=0。
- 逐字节比对（验证方独立 sha256）：14 份 core-evidence 归档与实现方 repair-r4 对应文件
  **13 份逐字节 IDENTICAL**；`core-tests.log` 差 1 行 = junit 写入 stamp 字符串（可归异）；
  PASS 行 670 行与实现方**完全一致**（identical=True）。

### R3 六页真实装载导航观察（UI 评审同方法修复的 F-01/F-02 反向核验；本轮关键新增验证面）
- 自有夹具 `verify_nav_capture.py`（验证方独立，不依赖评审工具）：
  上传当前 `app/` → 板上 `bash build.sh Release`（exit=0，产物
  sha256 `3ccbb4aa1608d222e7f18b80353fd9507ed8aff5df9aa5306ecd9ae23d14b1bc`）→
  临时配置副本（simulation=true、admin）→ Xvfb :99 软件后端启动 → xdotool 以
  NavBar 真实槽位几何（`Theme.qml:274-275` top_bar_height=48 + nav_width=88，
  槽心 92+88*i，x=44）逐页点击，ffmpeg x11grab 截屏 → 六页全部实际渲染：
  - `WIN_UP=1`，应用日志**仅 4 行 INFO**，`chipName`/`TypeError`/`is not a type`/`Cannot open`/
    `doesNotExist` 五类关键字**全文 0 命中**（对照修复前评审轮 app.log：3×TypeError + 48×Cannot open）。
  - 六张 1920×1080 PNG（52385/91809/86271/54655/96331/154239 B）**两两像素差**（pairwise
    diff > 0.03，p5/p6 亦非同图——v1 过程轮的 p4/p5/p6 因自定义 y 偏移误移至相邻槽位、此
    过程已留痕至 `nav-v1-drifted/` 并在坐标校正后重跑）。
  - P3 人工语境关键点（截图 `p3_replay.png`）：三联画面标题**逐帧文字正确**（"车辆左侧
    (CAM-02)"、"车头前方 (CAM-01)"、"车辆右侧 (CAM-03)"——F-01 修复特征），关键帧行**全是
    `—` 占位方框**（F-02 修复特征，缺失时占位而非报错），右栏事件详情完整。
- 应用日志 banner 仍打印 `simulation=off`（version.cpp:14 `simulation_enabled()` 是**编译期**
  `RAILHMI_SIMULATION` 宏，非 YAML 的 `app.simulation`；页面数据 seeding 用 YAML 值，行为
  与截图一致：seeding 生效）→ 观察项 O-1（banner 语义与配置值同名不同源，不影响功能，
  已登记实现参考）。

### R4 板端 ext 夹具（2 套）
1. rename 直接触碰域（沿用）：`verify_stream_fixtures.py`（ITEM-005，验证方既有夹具未改）
   → `verify_stream_ext` **40 passed / 0 failed** + config-check 负例 fx1/fx2 拒绝、fx3 通过。
   注意：该夹具 cleanup 会 `rm /tmp/railhmi_build`，本轮以此为节点似有竞序（见 §5 教训 2）。
2. 本轮新触碰域（新增自有夹具，本轮补上 P3 域断言）：`verify_p3_ext.cpp` +
   `run_p3_ext_fixtures.py`，连 TestRun/CoreTests `--keep-remote` 保留的板端 `librailhmi_core.a`
   （`/tmp/railhmi_build`）直连编译运行（g++ aarch64 板端）→ **16 passed / 0 failed**：
   - S1（F-01 数据域）: build_registry 对 CAM-01/02/03 透传 cameras.yaml 中文名 + order 保持。
   - S2（F-02 数据域）: keyframe thumb 引用全非空、全非 qrc: 基址；thumbSource 语义镜像
     四条独立边界（page-relative 丢弃 / file:// 前缀 / image:// 保留 / 相对丢弃为占位）。
   - S3: sync_snapshot(CAM-02=0, CAM-01=120, CAM-03=900; threshold 300) → available=true、
     in_sync=false、max=900、prompt 非空（P3 三联同步偏差提示行可达）。

### R5 主机静态与负例（交付链路完整性回归）
- `check_qml_qt_validity.py --root app/src/ui --headers ...` → EXIT=0（21 QML / 28 头文件）。
- `negcheck_qml_qt_validity.py` 验证方独立复跑 →
  `NEG_HEALTHY=0(期望0) / NEG_B01=1 / NEG_B03=1 / **NEG_F01=1** / NEGCHECK_RESULT=PASS`
  （**NEG_F01 证明新增的"根 id 未定义成员调用"检查对 F-01 类缺陷真实有效**，判据不空转）。
- `gen_tokens.py --selftest`、`check_no_hardcoded_ui.py` `CoreTests` 支线在全量 TestRun
  中作为 TOKENS/SCAN/QML_VALID/PACK 各步已含，重复执行未单独复跑。
- `verify_packaging25_scan.py`（P1..P16 全 True + D-01/D-01b/D-02 True）与
  `negcheck_packaging_fix.py`（NEG_HEALTHY=0/NEG_D01=1/NEG_D02=1）→ 本轮复核维持 PASS。

## 3. 判定与边界

- **TEST_PASS 范围**: ① F-01/F-02 在当前版本缺席（独立六页导航实际观察 0 QML 错误 + 截图
  特征匹配 + 数据域/语义镜像 ext 16/16 + 反例证明新守卫有效）；② 修复触碰域（P3Replay 数据
  契约 + stream_state 保留域插件）ext 夹具 56/56；③ 当前版本 complete_delivery 回归
  （TestRun 全绿 670/0 与实现方 repair-r4 归档逐字节一致 + 主机静态/负例/打包全 PASS）。
- **不纳入本轮范围**（沿用历轮边界，不因本项扩展；修复轮自身边界与之一致）:
  - 真实 6 路 RTSP 取流、真实模型推理画面、P3 真实媒体播放/缩略图（媒体/取流 handoff）。
  - 操作员真实 X 会话人工视觉确认（Xvfb + 软件场景图为本轮观察上界，视觉内容属用户核验）。
  - `systemctl enable`/断电重启自启（ENV-E 现场操作）。
- **观察项（非缺陷，交实现参考）**:
  - O-1 应用 banner `simulation=off`（version.cpp 编译期宏）与 YAML `app.simulation`（运行时
    seed 数据源）同名不同源，无功能影响；建议后续轮 biparse banner 语义或改名以免误导读日志。

## 4. 过程教训（验证侧，留档）

1. Xvfb 下无窗口管理器，`xdotool search --only-visible` 恒空 → `WIN_UP` 判定要用不带
   `--only-visible` 的 `--name` 搜索（`--only-visible` 的实现 `_NET_WM_STATE` 在纯 Xvfb 无 WM
   时不置位）。
2. ext 夹具复用序：ITEM-005 runner 的 cleanup 会 `rm /tmp/railhmi_build`；其后想再连
   `librailhmi_core.a` 的夹具需先经 `board_build.py --mode CoreTests --keep-remote` 重建
   （本轮 v2-core 轮重建，CoreTests BUILD_EXIT=0；夹具链才通）。
3. NavBar 点击坐标必须从 `Theme.qml` 读取真实值（top_bar_height=48、nav_width=88；
   槽心 92+88*i，x=44），自定义近似值会在 ≥4 位 slot 后漂移到相邻页（v1 过程轮 p4/p5/p6
   标签漂移，过程留痕后修正重跑）。
4. 两次独立路径 Release 构建的产物 sha 不同（BuildID 含暂存路径）：跨轮产物一致性判据
   仅在**同一路径/同一 stamp** 下可比，对不同 staging 目录的构建只对"尺寸/目标架构/冒烟
   行为"等本质特征比对。

## 5. 本轮验证方证据清单

- 本文件 `VERIFY-REPAIR-n008-v2.md`
- `release-build-192_168_0_103-20261017-verify-v2.json` / `smoke-20261017-verify-v2.log`（R1）
- `build-TestRun-192_168_0_103-20261017-verify-v2.log` + 14 份 `core-evidence_*`（R2）
- `verify_nav_capture.py` + `ui-nav-transcript.json` + `ui-nav-...-r3.log` +
  `nav-app.log`/`nav-xvfb.log` + 六页 PNG + `nav-v1-drifted/`（R3，过程留痕 + 权威轮）
- `board-ui-probe-probe-20261017-v2.{json,md}`（R3 前提探针）
- `verify_p3_ext.cpp` / `run_p3_ext_fixtures.py` / `VERIFY-FIXTURES-20261017-verify-v2-p3ext.log`（R4-2）
- `VERIFY-FIXTURES-20261017-verify-v2-ext.log` / `.transcript.json`（R4-1）
- `build-CoreTests-...-verify-v2-core.{log,transcript.json}`（R4-2 前置重建）

## 6. 复现

```sh
# 主机（离线）
python app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
python docs/hmi/test_evidence/REPAIR-n008-release-qt/negcheck_qml_qt_validity.py   # 含 NEG_F01
python docs/hmi/test_evidence/ITEM-025-packaging/verify_packaging25_scan.py --app app --pkg packaging
python docs/hmi/test_evidence/ITEM-025-packaging/negcheck_packaging_fix.py

# 目标: Qt Release 构建+冒烟（约 1.5 分钟）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin ^
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2 --stamp <tag> ^
  --no-install-deps --no-bundle --build-timeout 900

# 目标: 全量 TestRun（约 1 分钟，--keep-供 ext 用）
python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin ^
  --stamp <tag> --outdir docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2 --keep-remote

# 目标: 六页真实导航观察（自定义夹具，约 1.5 分钟）
python docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2/verify_nav_capture.py

# 目标: ext 夹具（stream 沿用 + p3 新增；stream 的 cleanup 会删板端构建树，
# 先跑 CoreTests 重建再跑 p3 ext）
python docs/hmi/test_evidence/ITEM-005-stream/verify_stream_fixtures.py --host 192.168.0.103 ^
  --user admin --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU ^
  --cpp docs/hmi/test_evidence/ITEM-005-stream/verify_stream_ext.cpp ^
  --outdir docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2 --stamp <tag> ^
  --remote-build-dir /tmp/railhmi_build --workdir /tmp/railhmi_verify_ext_v2
python app/tools/board_build.py --mode CoreTests ... --stamp <tag>-core --keep-remote
python docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2/run_p3_ext_fixtures.py
```
