# 修复证据 — 发布阻塞缺陷 B-01/B-02/B-03（desktop_app_implementation 修复轮）

- Capability: `embedded_linux_engineer.desktop_app_implementation`（修复调用 `repair_n008_..._implementation`）
- 修复范围来源: `n008_embedded_linux_engineer_desktop_release` 的 WORK_FAILED 报告
  `docs/hmi/release/RELEASE-PACKAGE.md`（§5 阻塞缺陷 B-01/B-02/B-03）
- 修复轮 stamp: `20261017-repair-r1`（Qt 目标构建+冒烟）、`20261017-repair-r2`（板端全量 TestRun）
- 修复轮 2 stamp: `20261017-repair-r3`（目标 Qt 构建 + Xvfb 六页导航截屏）、`20261017-repair-r4`（板端全量回归）
- 保留既有工作项 ITEM-001..025 产物；本轮只修所属缺陷，不重做任务队列
- 结果: **IMPLEMENTED**（目标 Qt 应用可构建、可装载运行）

---

## 1. 修复内容（变更、原因、文件）

### B-01（严重，编译期）core 方法名与 Qt 关键字 `emit` 冲突
- 原因：`StreamStateMachine::emit()` 在包含 Qt 头文件的 TU 中被 Qt 宏 `emit`（展开为空）破坏 →
  `error: expected ')' before 'k'`；core-only 单测不包含 Qt 头，故此前未暴露。
- 修复：`app/src/core/include/railhmi/core/stream_state.h` 与 `app/src/core/src/stream_state.cpp`
  将该私有方法 `emit` → **`emit_event`**（声明、定义、内部 8 处调用一并改名）。行为不变。

### B-02（严重，QML 装载）`main.qml` 未导入 `pages`，`DeniedView` 不是类型
- 原因：`main.qml` 直接以类型方式使用 `DeniedView`，但只 `import "."` 与 `import "components"`。
- 修复：`app/src/ui/qml/main.qml` 增加 **`import "pages"`**。

### B-03（严重，QML 装载）QML 属性名不允许大写开头
- 原因：生成令牌 `Theme.qml`/`Tokens.qml` 的属性名 `L1/L2/L3`（源自 `design_tokens.yaml` 的
  `alarm.L1..L3`）以及手写页面中的 `property var L/R/G/M` 均以大写开头，装载期报
  `Property names cannot begin with an upper case letter`，导致 `Theme` 单例不可用、整个界面无法装载。
- 修复（通用机制，不改设计与取值）：
  - `docs/hmi/design_tokens.yaml`：`alarm.L1/L2/L3` → **`level1/level2/level3`**（颜色/文案/编码全部不变）。
  - `app/tools/gen_tokens.py`：`check_required` 与 selftest 探针改用 `level*`；**新增生成期校验**
    —— 令牌键经 `ident()` 规范化后若以大写开头即 `raise`（并在 selftest 中断言该非法键被拒），
    使“非法键 → 不可装载 QML”在生成期失败，而非产出坏 QML。
  - 重新生成 `app/src/ui/qml/{Theme,Tokens}.qml`、`app/src/ui/qml/qmldir`、`app/resources/tokens.json`。
  - QML 使用点：`pages/P2AlarmCenter.qml`、`pages/P3Replay.qml` 的 `Theme.alarm.L1..3` → `Theme.alarm.level1..3`
    （报警级别**数据字符串** `"L1"/"L2"/"L3"`、`levelLabel` 与模型契约保持不变）。
  - 手写别名改为小写（同步各自文件内全部引用）：`P1Realtime.qml` `L`→`layout`；
    `P3Replay.qml` `R`→`replay`；`P4AllCameras.qml` `G`→`gridCfg`（避免与已有 `id: grid` 冲突）；
    `P6Maintenance.qml` `M`→`maint`；`components/RecoveryOverlay.qml` `R`→`health`。

### 通用回归守卫（新增）
- `app/tools/check_qml_qt_validity.py`：源码级捕获 B-01（core/platform 头文件中与 Qt 关键字
  `emit/signals/slots/foreach/Q_SIGNALS/Q_SLOTS/Q_EMIT/Q_FOREACH` 同名的标识符）与 B-03
  （QML `property`/`id` 名称大写开头）。已接入 `board_build.py` 的 `ui-scan`（`QML_VALID_EXIT`）。
- `gen_tokens.py` 生成期令牌键校验 + selftest 断言（见上）。

---

## 2. 验证（目标实测为主，主机静态为辅）

### 2.1 目标 Qt 构建 + 装载冒烟（决定性）
- 命令：`python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp 20261017-repair-r1 --no-install-deps --no-bundle`
- 结果：`bash build.sh Release` **成功**（`[100%] Built target railhmi`），产物
  `build-target/src/ui/railhmi`（ELF aarch64，944016 B）；
  `QT_QPA_PLATFORM=offscreen` 冒烟 **`smoke_exit=124`**（12 s 内持续存活 = 事件循环运行、界面装载成功）。
- 冒烟日志（`docs/hmi/release/smoke-20261017-repair-r1.log`）无任何 QML 错误：
  ```
  railhmi 0.1.0 [mode=Target, simulation=off]
  config dir: /tmp/railhmi_release/app/config
  configuration OK: 6 cameras
  render path: cpu
  ```
- 对照（修复前，见 `docs/hmi/release/release-build-...-diag3.json`）：同样命令下 QML 装载失败、exit 3。
- 构建 transcript：`docs/hmi/release/release-build-192_168_0_103-20261017-repair-r1.json`。

### 2.2 板端全量回归（`TestRun`，stamp `20261017-repair-r2`）
- `TESTRUN_EXIT=0`：core 单测 **670 passed / 0 failed**（`emit` 改名无回归）。
- `TOKENS_EXIT=0`：令牌 selftest 全 PASS，含新增 `[PASS] rejects upper-case token key`
  与 `[PASS] Theme.qml contains 'QtObject level1'`。
- `SCAN_EXIT=0`（无硬编码 UI）+ 各接线扫描 PASS + 新增
  `[check_qml_qt_validity] PASS`（`QML_VALID_EXIT=0`，扫 21 QML / 28 头文件）。
- `PACK_EXIT=0` / `SIM_EXIT=0` / 各探针（status/ai/alarm/clip/replay/maintenance/permissions/health/fault）PASS。
- 证据：本目录 `build-TestRun-...-repair-r2.log` + `core-evidence_*` + `.transcript.json`。

### 2.3 主机静态 + 反例自检
- `check_no_hardcoded_ui.py` PASS；`gen_tokens.py --selftest` PASS。
- `check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include` PASS。
- 反例自检 `negcheck_qml_qt_validity.py`（证明新守卫有效）：
  ```
  NEG_HEALTHY_EXIT=0 (expect 0)
  NEG_B01_EXIT=1     (expect 1)   # 注入 void emit(...) 被点名
  NEG_B03_EXIT=1     (expect 1)   # 注入 property var L 被点名
  NEGCHECK_RESULT=PASS
  ```

### 2.4 复现
```sh
# 主机
python app/tools/gen_tokens.py --yaml docs/hmi/design_tokens.yaml --selftest
python app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
python docs/hmi/test_evidence/REPAIR-n008-release-qt/negcheck_qml_qt_validity.py
# 目标：Qt 构建 + 装载冒烟（约 1 分钟）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp <tag> --no-install-deps --no-bundle
# 目标：全量回归（约 20 分钟）
python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin \
  --stamp <tag> --outdir docs/hmi/test_evidence/REPAIR-n008-release-qt
```

---

## 3. 环境前提（沿用上一轮授权内的修复，未新增系统改动）
- 目标已具备 Qt5 Quick/Qml 开发包（`qtdeclarative5-dev` 等 `5.15.3+dfsg-1`），系
  `desktop_release` 轮按 `environment_repair`(development) 授权非交互安装；本轮未再改动系统。
- 未重启整机、未改网络/驱动/服务、未启用 `railhmi.service`、未改 `cpu-performance.service`。

---

## 4. 边界与未执行
1. 冒烟为 `offscreen` 平台（不占用操作员 X 会话）；**未在用户 X 会话/真实屏幕做人工视觉确认**。
2. 真机 `systemctl enable` + 断电/重启自启属 ENV-E 现场操作，未执行。
3. 6 路真实 RTSP 取流、真实模型检测框/掩码属取流/推理工作项，未在本轮。
4. 本能力未修改设计语义（令牌取值/文案/级别映射均不变）。

---

## 5. 交接
- 建议 `software_verification` / `ui_implementation_review` 按**同一版本**重跑，并把
  “目标 Qt `Release` 构建 + QML 装载冒烟”纳入验证范围（依赖已就绪；`qmllint` 5.15 不足，
  需真实装载冒烟，见 `docs/hmi/release/RELEASE-PACKAGE.md` §5 B-03）。
- 两项 PASS 后由 `desktop_release` 重跑 `board_release_build.py`（不加 `--no-bundle`）产出正式发布包。

---

## 6. 修复轮 2（响应 UI 实现评审 `REVIEW-REPAIR-n008.md` F-01/F-02）

UI 评审以 Xvfb + xdotool **实际导航六页**，判定 TEST_FAIL：B-01/B-02/B-03 已修复、六页均可渲染，
但 P3 历史回放页存在两处运行期缺陷（前述 offscreen 冒烟只加载默认页，未导航到 P3，故未暴露）。

### F-01（阻塞）`P3Replay.qml` 调用未定义的 `chipName`（运行期 TypeError，三联标题渲染失败）
- 根因：`cbd0a93` 把标题改为 `page.chipName(modelData)`，但**从未定义该函数**（`git log -S` 确认）；
  因 QML 从未运行而长期未暴露。
- 修复（`app/src/ui/qml/pages/P3Replay.qml`）：新增**配置驱动**的 `function chipName(camId)`——
  按 `railhmiCameras`（注册表 id→name）返回名称，未知回退 `camId`（不硬编码地址/名称）。

### F-02（中）关键帧缩略图 `Image` 打不开且相对基址错误（48 次刷屏）
- 根因：`Image { source: modelData.thumb }`，`thumb` 为媒体层相对引用，被解析为 `qrc:/qml/pages/thumb/...`。
- 修复：新增 `function thumbSource(t)`——仅在绝对路径（转 `file://`）或 `image://`/`file://`/`qrc:/` 前缀时返回；
  否则返回空串（`Image.visible=false`，显示占位方框 `—`），**媒体缺失时不再报错**；将来接入真实缩略图时基址正确。

### 通用守卫（防同类回归）
- `app/tools/check_qml_qt_validity.py` 新增：**根元素 id 上调用未定义成员**（`page.X(` 而 X 未在本文件以
  `function/property/signal/id` 定义）即判失败——正是 F-01 类。反例 `NEG_F01_EXIT=1`。

### 验证（目标实测，评审同方法）
- **Xvfb 六页导航截屏**（`docs/hmi/ui_review/tools/board_ui_capture.py`，stamp `20261017-repair-r3`，
  `--repo .`）：`WIN_UP=1`，六页截图均生成（52389/91773/86192/54564/96188/154033 B）；
  **`app.log` 仅 4 行、0 条 QML 错误**（对照：修复前 P3 有 3×`TypeError chipName` + 48×`Image Cannot open`）。
  证据：本目录 `20261017-repair-r3-{app.log,p1..p6 png}`、`ui-capture-20261017-repair-r3.{log,transcript.json}`。
- **板端全量回归**（stamp `20261017-repair-r4`）：`TESTRUN_EXIT=0`，core **670 passed / 0 failed**；
  `TOKENS_EXIT=0`；`[check_qml_qt_validity] PASS`（含新的未定义成员检查）`QML_VALID_EXIT=0`；其余接线扫描/探针 PASS。
- **主机**：`check_qml_qt_validity` PASS；反例 `NEG_HEALTHY=0 / NEG_B01=1 / NEG_B03=1 / NEG_F01=1`。

### 边界
- 仍为 Xvfb 虚拟显示（未占用操作员 X 会话、未做人工视觉确认）；真实 6 路 RTSP/模型/回放缩略图属媒体/取流职责。
- 启用 `railhmi.service`/断电重启自启属 ENV-E，未执行。

### 复现
```sh
# 目标：构建 + 虚拟显示 + 六页导航截屏（约 1 分钟）
python docs/hmi/ui_review/tools/board_ui_capture.py --host 192.168.0.103 --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/REPAIR-n008-release-qt --stamp <tag> --repo .
# 目标：全量回归
python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin \
  --stamp <tag> --outdir docs/hmi/test_evidence/REPAIR-n008-release-qt
# 主机：静态守卫 + 反例
python app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
python docs/hmi/test_evidence/REPAIR-n008-release-qt/negcheck_qml_qt_validity.py
```
