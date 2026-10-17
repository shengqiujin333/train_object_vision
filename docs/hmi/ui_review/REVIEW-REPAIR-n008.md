# UI 实现评审证据 — REPAIR-n008（完整桌面交付/发布缺陷修复复核，complete_delivery）

- Capability: `web_frontend.ui_implementation_review`（节点 `repair_n008_..._check_2`，`verification_scope: complete_delivery`）
- 被评对象: 修复轮 `repair_n008_embedded_linux_engineer_desktop_release_implementation`
  - 声明产物 `app/README.md`；实现 commit `450c43b`；评审基于**当前工作树**（HEAD `4869453`）
  - 版本号 `packaging/VERSION` = `0.1.0`（target `rk3588-aarch64`）
  - 上游失败: `n008_embedded_linux_engineer_desktop_release` WORK_FAILED（`docs/hmi/release/RELEASE-PACKAGE.md` §5 B-01/B-02/B-03）
  - 上游验证: `repair_n008_..._check_1` TEST_PASS（`docs/hmi/test_evidence/VERIFY-REPAIR-n008/VERIFY-REPAIR-n008.md`）——**独立结论，不免除本能力观察**
- 参照设计/规范: `docs/hmi/INTERACTION_DESIGN.md` v2、`docs/hmi/VISUAL_DESIGN.md` v2、`docs/hmi/DESKTOP_APP_DESIGN.md`；`docs/hmi/design_tokens.yaml`
- 评审日期/环境: 2026-10-17；Windows 主机（无 Qt）+ RK3588 `192.168.0.103`（登记 Paramiko SSH，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）
- 判定: **TEST_FAIL**（**已实际加载并观察修复版本界面**：程序可构建、可装载、六页均可渲染；但 P3 历史回放页存在**真实 QML 运行期错误**，故完整交付 UI 评审不通过）

---

## 0. 方法与口径（完整交付要求“实际加载观察”）

按本能力完整交付/发布修复要求，本轮**不以源码代替运行观察**：在目标板本机构建 `Release`（Qt 5.15.3），
在**虚拟显示 Xvfb**（不占用操作员 X 会话）上以 `QT_QUICK_BACKEND=software` 启动程序，用 `xdotool`
逐页导航（P1..P6），用 `ffmpeg x11grab` 截取 1920×1080 屏幕，并读取应用运行日志。
- 未修改任何产品源码/界面/验收判据；仅新增**评审工具**（截图/UI 检查）并在 `environment_repair(test)`
  授权内非交互安装 `xvfb`（截图工具依赖）。
- 使用**临时配置副本**（`/tmp/railhmi_uireview/config`，`simulation=true`、`default_role=admin`）以驱动
  六页都有代表性数据；仓库内 `app/config` 未改动。
- 真实摄像头/模型画面内容属取流/推理/媒体职责（handoff）；本节结论限于“修复版本实际装载后的界面与
  需求内交互”。

---

## 1. 版本与装载证据（实际运行）

| 项 | 值 |
|---|---|
| 目标 Qt `Release` 构建 | `bash build.sh Release` 成功（`build_exit=0`） |
| 产物 | `build-target/src/ui/railhmi`，ELF aarch64，944016 B，sha256 `1f4e3975640aafe1a22026efaf95c12cb6f836dd7fe767664331d32fb9a0858e` |
| 装载（软件场景图，Xvfb） | 窗口出现 `WIN_UP=1`；应用日志 `configuration OK: 6 cameras` / `render path: cpu` |
| 六页渲染 | 6 张 1920×1080 截图互不相同且非空白（PNG 8-bit RGB，unique-byte 232..247，nonzero=1.000） |
| 应用日志 | P1/P2/P4/P5/P6 无 QML 错误；**P3 见 F-01/F-02** |

证据文件（`docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui/`）：
- `20261017-uiobs-r3-{p1_realtime,p2_alarm_center,p3_replay,p4_all_cameras,p5_ai_debug,p6_maintenance}.png`
- `20261017-uiobs-r3-app.log`（运行日志）、`20261017-uiobs-r3-xvfb.log`
- `ui-capture-20261017-uiobs-r3.{log,transcript.json}`（逐步命令/退出码/产物 sha256）
- 前提探针 `board-ui-probe-20261017-uiobs-r1.{json,md}`；`release-build-192_168_0_103-20261017-uiobs-r1.json` / `smoke-20261017-uiobs-r1.log`（复跑构建+冒烟）
- 过程轮 r1/r2（r1 Xvfb 启动失败=评审工具问题；r2 空截图=未用软件场景图，出现 `Unsupported extension used`）——留痕，非产品缺陷；r3 为权威轮

---

## 2. 判据逐条核对（完整交付）

| 判据 | 观察 | 判定 |
|---|---|---|
| B-01（`emit` 与 Qt 宏冲突） | 目标 Qt 构建成功（4 个 TU 编译通过） | 已修复 |
| B-02（`main.qml` 缺 `import "pages"`） | `DeniedView` 可解析，无 “is not a type” | 已修复 |
| B-03（QML 大写属性名） | 无 “Property names cannot begin with an upper case letter”；`Theme` 可用 | 已修复 |
| 程序可装载/可呈现界面 | 六页均渲染出图（非空白） | 满足 |
| 需求内交互（导航 P1..P6） | 导航切换生效（6 页不同画面） | 满足 |
| 界面无运行期 QML 错误 | **P3 有 TypeError + Image 打不开（F-01/F-02）** | **不满足** |

---

## 3. 具体偏差与修复要求（交桌面实现）

**F-01（阻塞，P3 运行期 TypeError）`P3Replay.qml:185` 调用未定义的 `chipName`**
- 观察（实际运行日志，导航到 P3 时）：
  ```
  qrc:/qml/pages/P3Replay.qml:185: TypeError: Property 'chipName' of object P3Replay_QMLTYPE_114(0x5594410540) is not a function
  ```
  共 3 次（三联画面标题各一次）。源码核对：`app/src/ui/qml/pages/P3Replay.qml:185`
  `text: page.chipName(modelData) + " (" + modelData + ")"`，但**该文件内不存在 `function chipName` 或 `property chipName`**（全文仅此一处出现 `chipName`）。
- 影响：P3 三联回放画面（左/前/右）标题标签**渲染失败**，显示缺名；并产生运行期错误。
  （`chipName` 曾存在于 ITEM-020 版本 `a718a53`，疑在发布修复轮的 QML 别名小写化/重命名编辑中丢失 → 回归。）
- 复现路径：目标板 `Release` 构建 → 以 admin/simulation 启动 → 导航到“历史回放”(P3) → 观察 stdout。
- 期望：在 `P3Replay.qml` 恢复/定义 `chipName(camId)`（CAM-01→前方、CAM-02→左侧、CAM-03→右侧，其余回退 id），
  或改用已定义且小写合法的映射函数；P3 加载后 stdout 无 TypeError，三联标题正确显示中文名。

**F-02（中，P3 关键帧缩略图 Image 打不开 + 路径基址错误）**
- 观察：P3 加载时 **48 次**
  `P3Replay.qml:308 QML Image: Cannot open: qrc:/qml/pages/thumb/CAM-01/<ts>.bmp`。
- 影响：关键帧缩略图条为空框，且日志被刷屏。
- 说明/期望：
  1. 缩略图图像由媒体层生成（handoff），当前不存在属预期；但**界面不应在缺失时反复报错**——
     建议 `Image { source: modelData.thumb (仅当可用/存在时) }` 或用占位方框、并避免空 `source` 触发错误；
  2. 相对 `thumb/...` 被解析为 `qrc:/qml/pages/thumb/...`（相对 QML 文件目录），**基址错误**；
     若将来接入真实缩略图，应使用绝对/资源提供者路径（如 `image://` 或 `file:///opt/railhmi/...`）。

### 已确认无问题/正向项

- 三类发布阻塞缺陷 B-01/B-02/B-03 在**当前版本**缺席：目标 Qt 构建成功、QML 全装载、无大写属性名错误。
- 程序可呈现界面：P1/P2/P4/P5/P6 六页中的五页实际渲染且无 QML 错误；六张截图非空白、彼此不同。
- 未修改产品源码/配置/判据；仅新增评审工具与临时观察配置。

---

## 4. 未覆盖 / 未执行

1. 真实 6 路 RTSP 取流、真实模型检测框/掩码、P3 真实媒体播放/缩略图（媒体/取流 handoff）。
2. `systemctl enable` / 断电重启自启（ENV-E 现场，未执行）。
3. 操作员真实 X 会话人工视觉确认（本轮用 Xvfb 虚拟显示，避免干扰操作员桌面）。

---

## 5. 判定

**TEST_FAIL**（范围：REPAIR-n008 完整交付的**实际加载界面**与需求内交互）。
- **失败依据（实际运行观察，非静态推断）**：修复版本可构建/可装载/可渲染六页，但 **P3 历史回放页存在
  真实 QML 运行期错误 F-01（`chipName` 未定义，三联标题渲染失败）**；另有 F-02（关键帧缩略图 Image
  打不开 + 相对路径基址错误）。完整交付 UI 评审要求界面无此类运行期缺陷，故不通过。
- **边界**：本 FAIL 承认并记录 B-01/B-02/B-03 已修复、程序确能呈现界面；失败点为 P3 的运行期缺陷。
  上游 `repair_n008_..._check_1`（软件验证）的 offscreen 冒烟仅加载默认页，未导航到 P3，故未暴露 F-01；
  本能力按完整交付要求实际导航六页并独立得出上述结论。
- 界面修复交桌面实现能力（`embedded_linux_engineer.desktop_app_implementation`）；修复后需以“目标
  Qt 构建 + 六页导航装载冒烟 + 无 QML 运行期错误”复验。

### 复现（评审工具，可复用）

```sh
# 前提只读探针（Qt/Xvfb/ffmpeg/二进制/DISPLAY）
python docs/hmi/ui_review/tools/board_ui_probe.py --host <addr> --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui --stamp <tag>
# 目标构建 + 虚拟显示装载 + 六页导航截屏（评审工具；需 TRAIN_RK3588_SSH_CREDENTIAL）
python docs/hmi/ui_review/tools/board_ui_capture.py --host <addr> --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui --stamp <tag>
```
