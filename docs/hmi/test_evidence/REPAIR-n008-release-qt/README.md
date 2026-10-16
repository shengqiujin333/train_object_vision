# 修复证据 — 发布阻塞缺陷 B-01/B-02/B-03（desktop_app_implementation 修复轮）

- Capability: `embedded_linux_engineer.desktop_app_implementation`（修复调用 `repair_n008_..._implementation`）
- 修复范围来源: `n008_embedded_linux_engineer_desktop_release` 的 WORK_FAILED 报告
  `docs/hmi/release/RELEASE-PACKAGE.md`（§5 阻塞缺陷 B-01/B-02/B-03）
- 修复轮 stamp: `20261017-repair-r1`（Qt 目标构建+冒烟）、`20261017-repair-r2`（板端全量 TestRun）
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
