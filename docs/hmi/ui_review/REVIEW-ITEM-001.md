# UI 实现评审证据 — ITEM-001（目标构建依赖基线核验）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（artifact `app/README.md`，commit `09c105f`，2026-10-15 14:38 +0800）
- 被评工作项: `TL-…-tasks-ITEM-001`（来源 `docs/hmi/desktop_app_tasks.yaml` 第 1 项；判据来源 `docs/hmi/DESKTOP_APP_DESIGN.md` §3.4）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md`（commit `4779b04`）、`docs/hmi/VISUAL_DESIGN.md` + `docs/hmi/design_tokens.yaml`（commit `e00df72`）
- 关联上游证据: `n006 software_verification` TEST_PASS（`docs/hmi/test_evidence/ENV-B-dependency/VERIFY-ITEM-001.md`，commit `4885ad4`）
- 评审日期: 2026-10-15；执行环境: Windows 主机（MINGW64 x86_64），项目设备工具禁用，**未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-001 交付物；不构成任何 UI 视觉/交互结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-001**（依赖基线核验交付物）。该工作项是**非 UI 项**，是 UI 实现的前置环境基线；本次 PASS **只**说明 ITEM-001 的交付物质量、证据真实性与“未核验”口径成立。
2. **本次未对任何界面做视觉/交互还原评审**：当前实现（commit `09c105f`）只包含只读依赖探针，**没有任何可运行 UI、没有截图、没有交互记录**。按评审职责，“只有源码/无运行素材时不能宣称视觉运行效果或交互行为通过”，因此本文不对布局、组件、字体、图标、颜色语义、焦点、反馈、空/错/恢复状态做任何 PASS 结论。
3. **目标板依赖基线仍未冻结**（E-01..E-11 板端全部未核验）。这是**依赖缺口（设备授权/平台事实）**，按 `E2E_PLAN.md` §40 与公司规则第 15 条，记入缺口并交平台/嵌入式/授权方，**不计为软件功能失败**；不由本评审判定“无法修复”。
4. 本 PASS 不放宽 ITEM-001 的任何验收判据；下节偏差 D-01..D-05 为**具体修复要求/待办**，其中 D-01、D-05 是范围性缺口，D-02..D-04 是可在本工作项内补齐的交付改进。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `09c105f`（2026-10-15 14:38） | 交付 `app/README.md`、`app/tools/env_probe.py`、`app/tools/run_env_check.sh`、`docs/hmi/build_env_check.md`、`docs/hmi/test_evidence/ENV-B-dependency/*` |
| 交互设计 | `4779b04` | 未被本次评审核验（无运行界面） |
| 视觉设计 / 令牌 | `e00df72` | 未被本次评审核验（无运行界面） |
| 桌面应用设计 | `10fc951` | §3.4 为本工作项判据来源 |
| 测试设计 / E2E | `e7d7484` | §1 环境分层、§40 升降级准则为本判定依据 |

实现自述状态与仓库实际一致：`app/README.md` 明确“尚未交付完整应用”，ITEM-002 起为“未实现”。**未发现夸大或把主机结论冒充板端结论的表述。**

---

## 2. 评审方法与证据基础

按可用性从高到低使用证据；本轮可用的是**源码审阅 + 归档证据核对 + 主机只读复现**：

1. 源码审阅：`app/tools/env_probe.py`（逐检查函数、命令列表、结果记录逻辑）、`app/tools/run_env_check.sh`（入口与输出三件套）。
2. 文档审阅：`docs/hmi/build_env_check.md`（§0 硬声明、§2 状态总表、§3–§12 逐项、§12 结论）、`app/README.md`。
3. 归档证据核对：`docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-20251015.{md,json}` 与独立复现件 `…-verify.{md,json}`；`VERIFY-ITEM-001.md`。
4. 独立复现：本评审在本机直接运行 `python app/tools/env_probe.py`（仅 stdout，不落盘），并与归档结果比对。

**方法限制（如实声明）**：无目标板访问、无图形会话、无 UI 产物 → 不能验证任何板端依赖满足、不能验证任何渲染/交互行为、不能以静态图推断交互。上述均列为未覆盖（§6）。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `python app/tools/env_probe.py`（stdout） | 运行成功；输出与本机环境一致（hostname `DESKTOP-476JGQ4`，machine `AMD64`，python 3.13.9；gcc/g++/cmake/pkg-config/dpkg 均 command not found） |
| R2 | `diff`（忽略“生成时间”行）归档 `env-probe-host-20251015.md` 与验证件 `…-verify.md` | **逐行一致**（除时间戳），确认探针输出确定性、可复现 |
| R3 | JSON 结构核对 `env-probe-host-20251015.json` | 12 段 E-00..E-11 齐备，43 条命令记录，每条含 `cmd/available/exit/output`；`E-11` 状态 `not_run_needs_authorization` |
| R4 | `build_env_check.md` 覆盖与口径统计 | 覆盖 E-00..E-11；“未核验”出现 23 处；“主机”相关表述 17 处；§0/§12 明确“无一项在板端执行” |
| R5 | 入口脚本 `run_env_check.sh` 读检 | 只调用探针、默认关闭相机网络探测、产出 md/raw/json，未安装/未改系统 |

结论：证据真实、可复现、非伪造；主机/板端口径在探针报告与 `build_env_check.md` 中均被强制分离。

---

## 4. 判据逐条核对（ITEM-001 用户可观察点）

| 判据（工作项原文） | 观察 | 判定 |
|---|---|---|
| 新增 `docs/hmi/build_env_check.md`，每条命令与真实结果 | §3–§11 每项列出板端命令、板端状态与主机原始输出摘录；原始结果完整归档；抽查与归档原件一致（R2/R4） | 满足 |
| 未执行的项显式标注未核验 | §0 声明未连板；§2 表板端状态全为“未核验/未执行”；§12 重申“无一项在板端执行” | 满足 |
| 不得把主机可运行当作板端可运行 | 探针内建环境判定规则（machine=aarch64 + rknpu 可读才计板端）；`build_env_check.md` §0/§2/§12 反复声明；E-00/§3 明确主机 Qt 5.15.2 不代表板端 Qt | 满足 |
| 对不可用项给出替代结论 | §3–§11 每项给替代结论（补装路径/回退渲染/版本匹配/NVR 缺口归属），并区分阻塞归属 | 满足 |
| 覆盖 §3.4 所列依赖（Qt5.15+xcb/eglfs、qmlglsink、Mali EGL/GLES、mppvideodec/glupload/glcolorconvert、rgavideoconvert+librga、librknnrt、sqlite3、磁盘） | 均有对应检查函数与命令；运行时协商实测按设计留 ITEM-002（已声明） | 满足（见 D-02 编号偏差） |

---

## 5. 具体偏差与修复要求

> 以下为可复现的具体项；D-01/D-05 属范围/资源缺口，D-02..D-04 建议在本工作项或紧随其后补齐。

**D-01（依赖缺口，阻断“基线冻结”，非软件缺陷）— 板端未执行**
- 观察：E-01..E-11 无一项在 RK3588 执行，`build_env_check.md` §12 自述“依赖基线未冻结”。
- 期望：设备授权后执行 `python3 app/tools/env_probe.py …`，把 machine=aarch64 的原始输出归档为板端证据；在此之前任何“板端依赖已满足”的表述都不成立。
- 归属：设备授权 + 平台/嵌入式；本评审不判定其不可修复。记录依据 `E2E_PLAN.md` §40、公司规则 15。

**D-02（可修复的追溯性偏差）— E-10/E-11 编号与 §3.4 不一致**
- 观察：`DESKTOP_APP_DESIGN` §3.4 中 **E-10 = 相机可达/凭据、E-11 = NVR 回放/导出接口**；而 `env_probe.py`、`build_env_check.md` §2 中 **E-10 = CPU governor/UDP 缓冲、E-11 = 相机/NVR 可达**。
- 期望：二者之一对齐（建议探针/文档按 §3.4 编号，governor/UDP 另立号如 E-12 或明确标注“扩展项”），并在 `build_env_check.md` 增加一列“§3.4 对应项”，避免按编号追溯时错配。
- 修复成本低，不影响现有未核验结论。

**D-03（覆盖缺口）— 设计 §3.4 E-11（NVR 回放/导出接口）未被实质核验**
- 观察：探针仅对 NVR 做 `TCP 80` 端口可达性，未触及回放/导出能力；`build_env_check.md` 以“接口未知/归现场”收口。
- 期望：明确写出“E-11 接口能力属现场/厂商文档，本项仅记录可达性”，避免与“已覆盖 E-11”混淆（与 D-02 同源）。

**D-04（UI 相关依赖缺口，建议纳入基线）— 视觉系统渲染前提未进入核验清单**
- 观察：`VISUAL_DESIGN.md` §4/§9 依赖中文与等宽字体、触控输入与显示分辨率；但探针与 `build_env_check.md` **未检查**：
  - 字体：`fc-list | grep -i 'Noto Sans CJK\|Noto Sans Mono'`（缺失会导致中文回退/等宽数字错位）；
  - 触控输入：`xinput list` / `libinput`（关系触控目标 ≥44px 的可用性，对应待确认 V-01）；
  - 显示分辨率/缩放：`xrandr` 或 `xdpyinfo`（关系 1920×1080 / 1280×720 两档布局验收）；
  - QML 模块：`QtQuick.Controls 2` / `QtQuick.Layouts` 是否随 `qml-module-qtquick*` 可得（仅 E-01 的插件目录不足以证明控件可用）。
- 期望：把上述四项加入依赖探针与 §3.4 清单（作为 UI 渲染前提），使 UI 实现拿到“可渲染”的环境前提，而非等到界面任务才发现字体/触控缺失。
- 归属：本能力提出的评审要求，交桌面应用实现/平台补入。

**D-05（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：无 UI 源码、无可运行界面、无截图/录屏。
- 期望：ITEM-003 起产出可运行 UI 后，UI 实现评审需按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§10 逐状态核对（正常/空/加载/失败/恢复、双分辨率、焦点可见、颜色非唯一编码、司机/调试隔离）；届时需提供运行录屏或交互记录，静态图不足以证明交互行为。

---

## 6. 未覆盖 / 未执行（须显式记录，不得外推）

1. 目标板任何命令（禁止假定已执行）——E-01..E-11 板端结论全部未核验。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 渲染路径协商实测（`mppvideodec→glupload→glcolorconvert→qmlglsink`）——归 ITEM-002。
4. ENV-A 主机预览 —— 本机无 gcc/g++/cmake/pkg-config/GStreamer/Qt 平台插件，当前不可执行（探针已如实记录）。
5. 相机/NVR 网络可达（E-11）—— 需项目授权，本轮未开启。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-001 交付物）。
- 依据：ITEM-001 声明的用户可观察点（命令与真实结果、未执行项显式标注未核验、主机≠板端、不可用项替代结论）经源码审阅、归档证据核对与主机独立复现逐条成立；证据真实、确定、可复现。
- 明确边界：本 PASS **不表示**任何板端依赖已满足，**不表示**任何 UI 视觉或交互行为通过；目标板依赖基线仍未冻结，属依赖缺口，交设备授权与平台/嵌入式职责推进。
- 后续修复要求见 D-01..D-05；其中 D-02/D-03/D-04 可在实现侧低成本补齐。
