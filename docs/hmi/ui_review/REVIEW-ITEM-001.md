# UI 实现评审证据 — ITEM-001（目标构建/运行依赖基线核验）第二轮

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `06b6f9f`，2026-10-15 21:13:59 +0800；主体 `docs/hmi/build_env_check.md`，工具 `app/tools/board_env_probe.py`、`app/tools/env_probe.py`，入口 `app/README.md`）
- 被评工作项: `ITEM-001`（冻结构建/运行依赖基线与授权缺口登记；判据来源 `docs/hmi/DESKTOP_APP_DESIGN.md` §3.3 E-00..E-13）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 + `docs/hmi/design_tokens.yaml` v2（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` v2（`e691118`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（commit `a5b6c2d`；`docs/hmi/test_evidence/ENV-B-dependency/VERIFY-ITEM-001.md` ITER2）
- 评审日期/环境: 2026-10-15；Windows 主机 `DESKTOP-476JGQ4`，MINGW64 x86_64，Python 3.13.9，paramiko 5.0.0；**本轮未连接 RK3588 目标板**（说明见 §0.3、§6）
- 判定: **TEST_PASS（范围严格限定为 ITEM-001 交付物；不构成任何 UI 视觉/交互结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-001**（依赖基线核验交付物）。该工作项是**非 UI 项**，是 UI 实现的前置环境基线；本次 PASS **只**说明 ITEM-001 交付物的证据真实性、可复现性与“三态/未核验”口径成立。
2. **本次未对任何界面做视觉/交互还原评审**：当前实现（commit `06b6f9f`）只含只读依赖探针与板端运行器，`app/README.md` 自述“尚未交付完整应用”，无 UI 源码、无可运行界面、无截图/录屏。按评审职责，“只有源码时不能宣称视觉运行效果或交互行为通过”，因此本文不对布局、组件、字体、图标、颜色语义、焦点、反馈、空/错/恢复状态做任何 PASS 结论（§5 D-04）。
3. **本轮评审未重新连接目标板**：板端 E-00..E-13 的**独立复跑已由 `software_verification` ITER2 完成**（`board-env-probe-…-verify-20261015-v1.*`，与实现者 r2 除易变项外一致）。本 capability 的授权方法为 `read/ui_evidence/design_assets`，为避免重复连接与在目标 `/tmp` 产生新残留（正是本项 D-01 的问题），本轮以**主机独立复跑 + 归档证据完整性核对 + 源码审阅**为独立证据，并以软件验证的板端复跑作为板端事实来源，明确标注引用关系，不冒充自跑。
4. 待评审的**依赖缺口**（Qt QML 控件/开发包、`qmlglsink`、`rgavideoconvert`、rknpu root 可读性、NVR 协议/通道映射、主机工具链）按职责交平台/rootfs、现场/文档与实现侧；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未使用图像生成工具**（未注册/未授权）；未创建或修改任何实现代码，只写本评审证据文件与清单登记。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `06b6f9f`（2026-10-15 21:13） | 交付 `app/README.md`(±18)、`app/tools/board_env_probe.py`（新 193 行）、`app/tools/env_probe.py`(±23)、`docs/hmi/build_env_check.md`（281 行重写）、板端 r1/r2 证据三件套、主机 `210821` 证据、`file_manifest.txt`；无范围外改动 |
| 交互设计 | `ef47841` | v2（6 路可见光、不含红外、NVR 登记、检测职责）；本轮不核验界面行为 |
| 视觉设计 / 令牌 | `40ccf4e` | v2（仅可见光模式位、设备集成状态、NVR 卡、检测职责徽标）；本轮不核验界面还原 |
| 桌面应用设计 | `e691118` | §3.3 E-00..E-13 为本工作项判据来源 |
| 软件验证 | `a5b6c2d` | ITER2 TEST_PASS；板端独立复跑与归档比对成立 |
| 探针脚本一致性 | `app/tools/env_probe.py` @ `06b6f9f` | 主机归档 `210821` 早于脚本最终 QML 增补（§3 R1），板端 r2 用最终脚本 |

实现自述状态与仓库实际一致：`app/README.md` 明确“尚未交付完整应用”，ITEM-002 渲染路径 `UNMEASURED`，ITEM-003 未实现。**未发现把主机结论冒充板端结论、或把端口可达写成取流可用的表述。**

---

## 2. 评审方法与证据基础

按可用性从高到低使用证据；本轮实际可用：**主机只读独立复跑 + 归档证据完整性核对 + 源码审阅 + 判据逐条对账**。

1. 源码审阅：`app/tools/board_env_probe.py`（连接/超时/known_hosts/凭据/清理）、`app/tools/env_probe.py`（检查项与编号、环境判定、`--skip-slow`、UI 前提 E-13）、`app/tools/run_env_check.sh`。
2. 文档审阅：`docs/hmi/build_env_check.md`（§0 硬声明、§2 三态总表、§3 缺口与替代结论、§4/§5/§6）。
3. 归档证据核对：板端 r2 `.md/.raw.txt/.json/.commands.json` 与软件验证复跑件；主机 `210821` 与验证复跑件。
4. 独立复现：本评审在本机直接运行 `python app/tools/env_probe.py --skip-slow`（落临时目录，核对后删除），并与归档结果逐行比对。

**方法限制（如实声明）**：本轮无板端连接、无图形会话、无 UI 产物 → 不能独立确认板端依赖现状、不能验证任何渲染/交互行为、不能以静态图推断交互。板端事实采用软件验证 ITER2 的独立复跑证据（已注明引用）；主机与 UI 缺口列于 §6。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `python app/tools/env_probe.py --skip-slow --out … --raw-out … --json-out …`（本机，输出到临时目录后清除） | 退出码 **0**；生成 Markdown/raw/json。与软件验证主机复跑件 `env-probe-host-verify-20261015.md` 去“生成时间”行后**逐行一致（379 行）**；与实现者主机归档 `env-probe-host-20261015-210821.md` **不一致（365 行）**，差异仅 E-01 新增 2 条 QML 检查 → **独立复现确认 R-3（实现者主机归档早于脚本最终版）** |
| R2 | 比对板端 `board-env-probe-…-board-r2.md` 与 `…-r2.raw.txt` 字节 | **逐字节一致（76268 字节）** → `md` 是原始输出的忠实归档，非二次编辑 |
| R3 | 比对板端 r2 与软件验证复跑 `…-verify-20261015-v1.md`（去“生成时间”行） | 仅 2 行易变项差异：`/tmp/.X11-unix` 父目录 mtime（`21:09`→`21:17`）与 `loginctl` 会话号（`c44`→`c46`，即验证 SSH 会话）；**其余全部一致** → 探针结果确定性成立 |
| R4 | 源码+transcript 核对板端 `/tmp` 清理声明 | `board_env_probe.py` **无 cleanup 步骤**；r2 `.commands.json` 步骤序列为 `4 条预检命令 → upload → probe → download`，无 `rm`/清理命令；而 `build_env_check.md` §0.2 声明“运行结束后清除” → **声明无 runner 证据**（见 §5 D-01） |
| R5 | 对账 `build_env_check.md` §2 与板端 r2 原始输出（定向检索） | Qt `5.15.3`（/usr/lib/aarch64-linux-gnu）；`gst-inspect-1.0 qmlglsink`=`No such element`；`rgavideoconvert`=`No such element`；E-10 六路 `open`；E-11 NVR `open`（80/554）；Noto Sans CJK 存在；`xinput`=`Unable to connect to X server`；`xrandr`=`Can't open display`；`QtQuick/Controls.2`=`No such file or directory` — **与 §2 表逐条相符** |
| R6 | 安全边界源码核对 | `load_system_host_keys()` + 默认 `RejectPolicy`（**无 `AutoAddPolicy`**）；`connect/auth/banner` 与 `sftp channel` 均显式超时；凭据仅从环境变量读取（默认 `TRAIN_RK3588_SSH_CREDENTIAL`），失败时不回显；主机指纹 SHA256 打印并落 `.commands.json` — **与 §0.1 声明一致** |

---

## 4. 判据逐条核对（ITEM-001 用户可观察点）

| 判据（工作项原文） | 实际观察 | 判定 |
|---|---|---|
| 按 §3.3 逐条核验 E-00..E-13 | 板端 r2 `.md/.json/raw` 含 E-00..E-13 全部分节，每命令有真实输出与退出码（§3 R5 抽查相符）；板端执行经登记 SSH 入口 | 满足 |
| 每条命令保留真实输出与退出码 | `.commands.json`（7 步，含逐条远程命令与退出码）；`.md` 与 `.raw.txt` 逐字节一致（R2）；复跑一致（R3） | 满足 |
| 板端可用时经 `rk3588_inspect` 有限等待执行 | 连接/命令/SFTP 全超时（R6）；实现与验证两端连接均为秒级成功（软件验证 ITER2） | 满足 |
| 三态登记（已核实可用/未核验/有证据的缺口） | §2 三态口径明确：已核实项有成功命令证据；未核验项（NPU 版本/负载、相机鉴权编码、触控/分辨率、NVR 协议映射）保持“未核验”；缺口项（qmlglsink、rgavideoconvert、Qt QML 控件/开发包、主机工具链）点名 | 满足 |
| 缺口有替代结论 | §3.1..§3.7 逐项给替代动作与责任方 | 满足 |
| 不阻塞主机/core 增量 | §4.5 明确 `railhmi-core` 只需 gcc+sqlite，板端工具链已实测；主机 ENV-A 工具链缺口另记 | 满足 |
| 主机/板端结论分列 | 探针内建环境判定说明、报告自含 hostname/machine；§0.4 反复声明；板端件均带 aarch64 环境头 | 满足 |
| 覆盖 UI 渲染前提（前轮评审 D-04） | E-01/E-01b/ E-13 覆盖字体、QML 模块、Qt Quick 库；探针新增 `--skip-slow` 约束板端时长（R1/R5） | 满足（触控/分辨率受 SSH 无 XAUTHORITY 限制，见 D-02） |

---

## 5. 具体偏差与修复要求

**D-01（可复现的事实性偏差，非 UI）— §0.2 清理声明无 runner 证据**
- 观察：`build_env_check.md` §0.2 声明“写入仅限目标 `/tmp`（探针脚本与报告），运行结束后清除”；但 `board_env_probe.py` 无 cleanup 步骤，r2 `.commands.json` 步骤仅为 `预检 → upload → probe → download`，无 `rm`。
- 复现路径：`read app/tools/board_env_probe.py`（`finally` 仅 `client.close()`）；`read docs/hmi/test_evidence/ENV-B-dependency/board-env-probe-…-board-r2.commands.json`（无清理步）。
- 期望：二选一收敛——①在 runner 中把按名清理目标 `/tmp/railhmi_env_probe*` 收为显式一步，并把该命令计入 `.commands.json`；或②修订 §0.2 措辞，改为“运行后由操作者按名清理，runner 不自动清理”。二者都使“真实结果/明确标注”判据成立。与本项上游 R-1/R-4 同源，本评审独立复现。
- 归属：`desktop_app_implementation`（低风险、可本项内补齐）。

**D-02（UI 就绪度缺口，方法与责任待明确）— 触控输入与实际分辨率仅“未核验”，无获取路径**
- 观察：E-13 中 `xinput`/`xrandr` 因 SSH 会话无 `XAUTHORITY` 失败，触控与显示分辨率保持“未核验”。而 `VISUAL_DESIGN.md` §9/§13（V-01）与 `INTERACTION_DESIGN.md` §4.1 的布局验收（1920×1080 / 1280×720、触控目标 ≥44px）**直接依赖**这两项。
- 复现路径：`build_env_check.md` §2 E-13 行；板端 r2 md 中 `xinput`=`Unable to connect to X server`、`xrandr`=`Can't open display`。
- 期望：给出**不依赖 X 会话**的兜底采集并登记，例如读取 `/sys/class/drm/*/modes`（或 `/sys/class/graphics/fb0/virtual_size`）记录显示模式、读取 `/proc/bus/input/devices` 记录输入设备；若确需本地会话，则在证据中明确“只能由现场本地会话确认”及责任方/时机。至少把该缺口从“未核验”细化为“有获取路径/明确归现场”。
- 归属：`desktop_app_implementation` 补探针项；现场确认归用户/现场档（交 `INTERACTION_DESIGN` §12、`VISUAL_DESIGN` §13）。

**D-03（可低成本补齐的追溯性偏差）— 实现者主机归档早于已提交脚本**
- 观察：`env-probe-host-20261015-210821.md` 为 365 行，缺少 E-01 两条 QML 检查；本评审用提交版脚本复跑为 379 行，与软件验证复跑件一致（§3 R1）。
- 复现路径：`python app/tools/env_probe.py --skip-slow` 与两份主机归档逐行比对。
- 期望：用提交版脚本重新生成实现者主机归档，或在归档头部记录生成时的脚本版本/哈希，避免“归档不由当前脚本生成”的追溯歧义。属时序/标注问题，不改变板端结论（上游 R-3 同源）。

**D-04（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：无 UI 源码、无可运行界面、无截图/录屏；ITEM-003 起才有 UI。
- 期望：ITEM-003 落地后，UI 实现评审需按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对（正常/空/加载/失败/恢复、双分辨率、焦点可见、颜色非唯一编码、司机/调试隔离、NVR“未配置”态、仅可见光模式位）；届时须提供运行录屏或交互记录，**静态图不足以证明交互行为**。
- 归属：交 `desktop_app_implementation` 产出可运行 UI；本评审不自建 UI、不代实现。

### 已确认的缺口登记（无需在本项修复，仅确认已如实登记并交接责任）

- **Q-01 Qt QML 控件/开发包缺失（E-01b）**：`QtQuick/Controls.2` 与 `qtdeclarative5-dev`/`qml-module-qtquick*` 不在 → 基于 Qt Quick/QML 的界面**在板端无法启动/编译**。`build_env_check.md` §3.1 已登记并交平台/rootfs 安装最小集。**这是 ITEM-003 及 UI 实现的硬前置**，须在 UI 运行验证前闭环（本评审确认登记准确，非本项缺陷）。
- **Q-02 `qmlglsink` 缺失（E-02）**：`gl` 主候选不可用，渲染默认锚定 `cpu`/librga，与 `render_path_spike.md` §7 一致；登记准确。
- **Q-03 `rgavideoconvert` 缺失（E-04）**：以应用内 librga C API 替代；登记准确。
- **Q-04 rknpu 版本/负载非 root 不可读（E-06）**：真实权限边界，如实登记为未核验，未推断 NPU 不可用；一致。
- **Q-05 NVR 协议/账户/通道映射未提供（E-11 / Q-NVR）**：仅端口可达，未外推回放/导出能力；与 `INTERACTION_DESIGN` §0.4/§5.6、`VISUAL_DESIGN` §7.11 的“未配置/未核验”口径一致。

---

## 6. 未覆盖 / 未执行（须显式记录，不得外推）

1. 目标板任何命令本轮**未执行**（评审授权方法为只读源码/证据审阅）；板端现状以上游软件验证 ITER2 独立复跑为准。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 渲染路径协商实测（`mppvideodec→glupload→glcolorconvert→qmlglsink`）— 归 ITEM-002，当前 `UNMEASURED`。
4. ENV-A 主机预览 — 本机无 gcc/g++/cmake/pkg-config/GStreamer/Qt 平台插件（本轮复跑确认），当前不可执行。
5. 触控输入与显示分辨率 — SSH 会话无 XAUTHORITY（D-02）。
6. 相机 RTSP 鉴权与编码协商、NVR 回放/导出能力 — 现场/文档与 ITEM-002。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-001 交付物）。
- 依据：ITEM-001 声明的用户可观察点（§3.3 逐条命令+真实结果、未执行项显式标注、主机≠板端分列、缺口有替代结论、不阻塞 core 增量）经源码审阅、归档证据完整性核对（`md==raw`、r2 与复跑一致）与主机独立复跑逐条成立；证据真实、确定、可复现。
- 明确边界：本 PASS **不表示**任何板端 UI 运行或依赖已满足，**不表示**任何 UI 视觉/交互行为通过；QML 控件缺失（Q-01）仍是 UI 运行/编译的硬前置。
- 后续修复要求见 §5 D-01..D-04；其中 D-01（清理声明）与 D-03（主机归档追溯）可在实现侧低成本补齐，D-02 需补显示/输入兜底采集，D-04 待 ITEM-003 产生可运行 UI 后再评审。
