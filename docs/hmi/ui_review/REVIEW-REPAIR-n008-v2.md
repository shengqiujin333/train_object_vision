# UI 实现评审证据 v2 — REPAIR-n008（修复轮 2 复核，complete_delivery）

- Capability: `web_frontend.ui_implementation_review`（节点 `repair_n008_..._check_2`，`verification_scope: complete_delivery`）
- 被评对象: 修复轮 2 `repair_n008_embedded_linux_engineer_desktop_release_implementation`
  - 声明产物 `app/README.md`；实现 commit `a92d008`；评审基于**当前工作树**（HEAD `5bc49a5`）
  - 版本号 `packaging/VERSION` = `0.1.0`（target `rk3588-aarch64`）
- 本能力历史: `docs/hmi/ui_review/REVIEW-REPAIR-n008.md` **TEST_FAIL**（F-01 `P3Replay.qml:185` `chipName` 未定义 TypeError；F-02 缩略图相对基址 → 48× Image 打不开）
- 上游状态: 修复轮 2 → `repair_n008_..._check_1` **TEST_PASS**（`docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2/VERIFY-REPAIR-n008-v2.md`）——**独立结论，不免除本能力观察**
- 参照设计/规范: `docs/hmi/INTERACTION_DESIGN.md` v2、`docs/hmi/VISUAL_DESIGN.md` v2、`docs/hmi/DESKTOP_APP_DESIGN.md`；`docs/hmi/design_tokens.yaml`
- 评审日期/环境: 2026-10-17；Windows 主机（无 Qt）+ RK3588 `192.168.0.103`（登记 Paramiko SSH，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）
- 判定: **TEST_PASS**（**实际加载并观察**修复版本界面：可构建、可装载、六页均渲染且**无 QML 运行期错误**；F-01/F-02 缺席）

---

## 0. 方法与口径（完整交付要求“实际加载观察”）

沿用本能力完整交付要求，本轮再次**实际装载并观察**修复版本（不以源码代替运行观察）：
- 目标板本机构建 Qt `Release`（Qt 5.15.3）→ 在**虚拟显示 Xvfb**（不占用操作员 X 会话）上以
  `QT_QUICK_BACKEND=software` 启动 → `xdotool` 逐页导航（P1..P6）→ `ffmpeg x11grab` 截屏 1920×1080
  → 读取应用运行日志并按关键字检查 QML 错误。
- 未修改任何产品源码/界面/验收判据；仅使用评审工具（`docs/hmi/ui_review/tools/board_ui_capture.py`）与
  `/tmp` 临时观察配置（`simulation=true`、`default_role=admin`，仓库 `app/config` 未改）。
- 与软件验证（check_1 复核）**各自独立**：本轮为我方自有工具/入口的实际导航观察。
- 真实摄像头/模型画面属媒体/取流职责（handoff）。

---

## 1. 版本与装载证据（实际运行）

| 项 | 值 |
|---|---|
| 目标 Qt `Release` 构建 | `bash build.sh Release` 成功（`build_exit=0`） |
| 产物 | `build-target/src/ui/railhmi`，ELF aarch64，944016 B，sha256 `485b8f3f1c3707b776d03b9addaf947a89fd96cc7bae112666052cc79ee7c1c2` |
| 装载 | 窗口出现 `WIN_UP=1`；应用日志 `configuration OK: 6 cameras` / `render path: cpu` |
| **应用日志（权威）** | 仅 **4 行 INFO**；`TypeError`/`is not a type`/`Cannot open`/`chipName`/`doesNotExist`/`QML ` 关键字**各 0 命中** |
| 六页渲染 | 6 张 1920×1080 PNG 互不相同且非空白（8-bit RGB，unique-byte 232..247，nonzero=1.000；字节 52287/91724/86170/54620/96105/154093） |

证据（`docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui/`，stamp `20261017-uiobs-r4`）：
- `20261017-uiobs-r4-{p1_realtime,p2_alarm_center,p3_replay,p4_all_cameras,p5_ai_debug,p6_maintenance}.png`
- `20261017-uiobs-r4-app.log`、`20261017-uiobs-r4-xvfb.log`、`ui-capture-20261017-uiobs-r4.{log,transcript.json}`

---

## 2. F-01 / F-02 复核（源码 + 实际运行）

**F-01（上轮阻塞）`chipName` 未定义 → 已修复**
- 源码：`P3Replay.qml:65-69` 新增配置驱动 `function chipName(camId)`（遍历 `allCams`= `railhmiCameras`，命中且 name 非空返回名称，否则回退 `""+camId`）；调用点 `:201` 使用本文件定义。
- 运行：导航到 P3 时应用日志 **0 次** `chipName`/`TypeError`（对照上轮 3 次 TypeError）。
- 判定：**已修复**。

**F-02（上轮中）缩略图相对基址 → 已修复**
- 源码：`P3Replay.qml:72-80` 新增 `thumbSource(t)`（仅接受 `image://`/`file://`/`qrc:/` 前缀或绝对路径→`file://`，其余返回空串）；`:324-329` `Image.source=thumbSource(...)`、`visible` 绑定非空，缺失时显示 `—` 占位。
- 运行：P3 加载 **0 次** `QML Image: Cannot open`（对照上轮 48 次）。
- 判定：**已修复**（缩略图图像本身由媒体层生成，属 handoff；界面在缺失时以占位呈现，不再报错）。

---

## 3. 判据逐条核对（完整交付）

| 判据 | 观察 | 判定 |
|---|---|---|
| B-01（`emit` 与 Qt 宏冲突） | 目标 Qt 构建成功 | 已修复 |
| B-02（缺 `import "pages"`） | `DeniedView` 可解析 | 已修复 |
| B-03（大写属性名） | 无大写属性名错误；`Theme` 可用 | 已修复 |
| 程序可装载/可呈现界面 | 六页均渲染出图（非空白、互不相同） | 满足 |
| 需求内交互（导航 P1..P6） | 导航生效，6 页不同画面 | 满足 |
| 界面无运行期 QML 错误 | 应用日志 4 行 INFO，错误关键字 0 命中 | 满足 |

---

## 4. 观察项（非阻断）

**O-1 应用 banner `simulation=off` 与 YAML `app.simulation` 同名不同源**
- 观察：应用启动日志打印 `simulation=off`，但页面数据 seeding 生效（P2 列表、P3 时间轴等均按
  `app.simulation=true` 呈现）。经核对，banner 取自编译期宏 `RAILHMI_SIMULATION`（`version.cpp`），
  与 YAML `app.simulation`（运行时 seed 数据源）同名不同源。
- 影响：仅日志语义可能误导现场排障，无功能/界面缺陷。
- 期望：后续将 banner 与运行时配置值对齐或改名（实现参考）。

### 已确认无问题/正向项

- 三类发布阻塞缺陷 B-01/B-02/B-03 与上轮 F-01/F-02 在**当前版本**均缺席：目标 Qt 构建成功、QML 全装载、
  六页实际渲染且运行日志无 QML 错误。
- 六页截图非空白、彼此不同；未修改产品源码/配置/判据。
- 目标板留获清理核验：无残留 `Xvfb`/`railhmi` 进程，`/tmp/railhmi_uireview` 已删除。

---

## 5. 未覆盖 / 未执行

1. 真实 6 路 RTSP 取流、真实模型检测框/掩码、P3 真实媒体播放/缩略图（媒体/取流 handoff）。
2. `systemctl enable` / 断电重启自启（ENV-E 现场，未执行）。
3. 操作员真实 X 会话人工视觉确认（本轮用 Xvfb 虚拟显示，避免干扰操作员桌面）。

---

## 6. 判定

**TEST_PASS**（范围：REPAIR-n008 完整交付的**实际加载界面**与需求内交互）。
- 依据：修复版本可构建/可装载/可渲染六页；**实际导航观察**下应用日志无 QML 运行期错误
  （`TypeError`/`is not a type`/`Cannot open` 等 0 命中）；上轮 F-01（`chipName` 未定义）与 F-02
  （缩略图相对基址）在源码与运行两侧均闭环；B-01/B-02/B-03 维持修复。
- 边界：本 PASS **不表示**真实媒体播放/缩略图/取流可用（媒体 handoff），**不表示**真机自启通过
  （ENV-E 现场）；操作员真实屏幕人工视觉确认仍属用户范畴。观察项 O-1（banner 语义）非阻断。

### 复现（评审工具，可复用）

```sh
# 目标构建 + 虚拟显示装载 + 六页导航截屏 + 日志错误关键字检查（需 TRAIN_RK3588_SSH_CREDENTIAL）
python docs/hmi/ui_review/tools/board_ui_capture.py --host <addr> --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui --stamp <tag>
```
