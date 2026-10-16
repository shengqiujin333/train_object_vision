# railhmi 桌面应用发布包（DesktopReleasePackage）— 构建资源与支持范围清单

- Capability: `embedded_linux_engineer.desktop_release`（Restlessdream Runtime，RESUME_SYNC）
- 发布对象（被封装版本）: `n005 embedded_linux_engineer.desktop_app_implementation`
  - 实现提交: `ee0a741`（fix 轮 stamp `20261017-ITEM025-fix-r1`）
  - 软件功能验证: `b5aee62` → `docs/hmi/test_evidence/ITEM-025-packaging/VERIFY-ITEM-025.md` = **TEST_PASS**
  - UI 实现评审: `3f2dc66` → `docs/hmi/ui_review/REVIEW-ITEM-025.md` = **TEST_PASS**
- 版本号: `0.1.0`（`packaging/VERSION`，target `rk3588-aarch64`）
- 发布轮 stamp: `20261017-release-r1`
- 执行环境: Windows 主机（本机无 gcc/cmake/Qt）+ RK3588 `192.168.0.103`（登记 SSH 入口）

---

## 0. 发布判定

**WORK_FAILED（不可发布）** —— 当前已验证版本**无法产出可运行的真实程序**：

1. 目标 `Release`（Qt）构建在 `app/src/core/include/railhmi/core/stream_state.h` 处**编译失败**（该版本被 core-only 单测/源码级评审通过，但从未编译过 Qt 应用）。
2. 在目标临时副本修复该编译错误后，程序**能构建、能启动、能读取配置（6 路）**，但 **QML 装载失败**（`qml/main.qml` 与 `Theme.qml`），进程以 exit 3 退出。
3. 因此不存在可交付的“真实程序”；按本能力契约「发布包含真实交付内容，不用截图或文档代替程序」「没有目标运行证据不得声明目标验收完成」，不得声明 `RELEASE_READY`。

已在本能力授权内完成**环境修复**（见 §6），使上述阻塞**可复现、可验证**，为下一轮实现与验证扫清依赖障碍。

---

## 1. 版本与证据一致性核查（通过）

| 项 | 版本/提交 | 证据 | 判定 |
|---|---|---|---|
| 被发布实现 | `ee0a741`（stamp `20261017-ITEM025-fix-r1`） | `app/README.md` | — |
| 软件功能验证 | `b5aee62` | `docs/hmi/test_evidence/ITEM-025-packaging/VERIFY-ITEM-025.md` | **TEST_PASS** |
| UI 实现评审 | `3f2dc66` | `docs/hmi/ui_review/REVIEW-ITEM-025.md` | **TEST_PASS** |
| 版本号 | `0.1.0` | `packaging/VERSION` | 一致 |

结论：两项通过证据**同版本、同范围**（ITEM-025 目标运行包/systemd 自启/故障自检）。**但两项证据的验证范围都不含“目标 Qt Release 构建 + QML 装载”**——core 单测不链接 Qt（`CoreTests` 档）、UI 评审为主机源码级（主机无 Qt）。这正是本轮发布封装暴露的盲区（§5）。

---

## 2. 发布内容清单（构建资源 / 交付内容）

| 类别 | 内容 | 位置 |
|---|---|---|
| 源代码 | core/platform/media/ui + 单测 + 探针工具 | `app/src/{core,platform,media,ui}`、`app/tests`、`app/src/*/tools` |
| 构建配置 | 分层 CMake、四档构建入口 | `app/CMakeLists.txt`、`app/src/*/CMakeLists.txt`、`app/build.sh` |
| 设计令牌 | 令牌 → 生成 Theme/Tokens | `docs/hmi/design_tokens.yaml` → `app/src/ui/{qml/Theme.qml,qml/Tokens.qml,resources/tokens.json}`（`app/tools/gen_tokens.py`） |
| 图形资源 | QML 外壳/页面/组件 + 资源清单 | `app/src/ui/qml/**`、`app/src/ui/qml.qrc` |
| 配置 | 应用/相机/阈值/ROI（无凭据） | `app/config/{app,cameras,thresholds,roi}.yaml` |
| 部署/运行包 | systemd 单元、安装脚本、版本、升级回滚、资源许可 | `packaging/{railhmi.service,install.sh,VERSION,UPGRADE.md,RESOURCES.md}` |
| 模型（外部依赖） | 分割/检测模型 + 标签 | 由模型职责投放至 `/opt/railhmi/models`；仓库 `models/{yolov8s-seg.rknn,yolov5s.rknn,rail_labels.txt}` |
| 发布工具 | 资源探针 / 目标构建打包器 / QML 静态检查 | `app/tools/{board_release_probe.py,board_release_build.py,board_qmllint.py}` |
| 正式程序二进制 | **未产出**（目标 Qt 构建失败，见 §5） | — |

---

## 3. 构建资源与支持范围

### 3.1 主机侧（仅源码/静态检查，无编译器）
- Windows AMD64；Python 3.13 + PyYAML + paramiko；**无 gcc/cmake/pkg-config/GStreamer**；Qt 未纳入主机链路。
- 主机可做：源码审阅、`check_*` 静态核对、令牌生成；**主机编译不代表目标板可运行**。

### 3.2 目标侧（RK3588，本轮实测）
- OS `Ubuntu 22.04.5 LTS`；内核 `6.1.141`；`aarch64`；8 核；`/` 可用 31G。
- 工具链：gcc `11.4.0`；cmake `3.22.1`。
- 图形/媒体：Qt **5.15.3**（Core/Gui/Quick/Qml）；X11 `xcb`，`/tmp/.X11-unix/X0` 存在，`graphical.target` active，`DISPLAY` 环境变量为空（单元显式设 `DISPLAY=:0`）。
- GStreamer `1.20.3`（`rtspsrc`/`mppvideodec`/`glupload`/`appsink` 均可用）。
- librga `2.1.0`（pkg-config）+ `/usr/include/rga/im2d.h`；`/usr/lib/librknnrt.so`；`libsqlite3.so.0`（无 `sqlite3.h`，走项目 shim）。
- 系统服务：`cpu-performance.service` enabled+active（**本次未修改**）；`railhmi.service` 未安装；`/opt/railhmi` 不存在。
- 原始证据：`board-release-probe-192_168_0_103-20261017-release-r1.{md,json}`

### 3.3 构建档位与支持范围
| 档位 | 内容 | 目标依赖 | 目标运行 | 本轮实测 |
|---|---|---|---|---|
| `Release`(target) | Qt/QML 应用 + core/platform/media | Qt5 Quick/Qml + GStreamer + librga + rknn | aarch64 板端 | **构建失败**（§5 B-01） |
| `CoreTests` | core+platform 单测 | 仅 C++/cmake | 板端 | 670/670 通过（上游 `b5aee62`） |
| `MediaCheck` | media 层 | GStreamer + librga | 板端 | 上游通过 |
| `Preview`(host) | 主机预览 | 宿主 Qt | x86_64 | 主机无 Qt，未执行 |

- 图形后端：默认渲染路径 `cpu`（librga NV12→BGR，`main.cpp` 注入 `railhmiRenderPath=cpu`）；`gl` 路径保留但未选定。
- 支持范围：`OS=Ubuntu 22.04.x aarch64 (RK3588)`；`kernel>=6.1`；`graphics=X11(xcb)`；`Qt=5.15.x`；`GStreamer=1.20.x`。
- **主机版本与目标版本分别标识**：主机=源码级（无编译器）；目标=RK3588 aarch64 本机构建（无交叉工具链）。

---

## 4. 可复现构建/安装入口与操作说明

### 4.1 构建
```sh
# 目标板（aarch64 本机构建）
cd app && bash build.sh check          # 只做依赖自检（缺什么明确列出）
cd app && bash build.sh Release        # Qt/QML 应用（本能力实测：当前版本编译失败）
cd app && bash build.sh CoreTests      # core 单测（不依赖 Qt）
cd app && bash build.sh MediaCheck     # media 层
```
### 4.2 安装 / 启动 / 配置 / 日志
```sh
bash packaging/install.sh              # 建 /opt/railhmi/{bin,config,models,resources,data,logs}；装并 enable 单元
systemctl start railhmi                # 现场决定启动时机；不改 cpu-performance.service
# 单元 ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config（--config 由 core parse_config_dir 消费）
# 日志：应用内结构化日志（logs/）+ systemd append 的 railhmi.{out,err}.log
```
### 4.3 故障恢复 / 升级 / 回滚
- 故障自检：`build-core/tools/railhmi-fault-probe`（FI-01..05/10 显式提示 + 恢复路径；SC-BOOT 重启自恢复）；界面侧为 P6「故障自检卡」。
- 升级/回滚：`packaging/UPGRADE.md`（板端本机构建 → 停 → 装 → 起；`bin.bak.<tag>` 回滚；`data`/`logs` 不删）。
- 资源/图标来源许可：`packaging/RESOURCES.md`。

---

## 5. 阻塞缺陷（本能力目标实测，附已验证修复方向）

> 三类缺陷在**同一目标构建/装载链路**上依次暴露；核心原因是此前从未真正构建/装载过 Qt 应用（`CoreTests` 不含 Qt、UI 评审为主机源码级、E-01b 板端缺 Qt 开发包）。本轮补齐依赖后全部可复现。

### B-01（严重，编译期）core 方法名与 Qt 关键字冲突 `emit`
- 位置：`app/src/core/include/railhmi/core/stream_state.h:117`
  ```cpp
  void emit(StreamEventKind k, long long now_ms, const std::string& detail = "");
  ```
  及定义 `app/src/core/src/stream_state.cpp:56`。`emit` 在包含 Qt 头文件的 TU 中被 Qt 宏展开为空 → `void (StreamEventKind k, ...)` → `error: expected ')' before 'k'`。
- 证据：`release-build-192_168_0_103-20261017-release-r1.json`（`bash build.sh Release`：`src/ui/CMakeFiles/railhmi.dir/.../main.cpp.o` 等 4 个 TU 同一错误，`gmake: *** Error 1`）。
- 影响：**目标 Qt 应用无法编译**；core-only 单测因未包含 Qt 头而无此问题。
- 已验证修复（诊断，未改仓库源码）：仅将目标临时副本中 `emit`→`emit_event`（声明/定义/内部调用），`bash build.sh Release` → `[100%] Built target railhmi`、`[build.sh] build OK (mode=target)`（`release-build-...-diag2.json`）。
- 通用修复建议：把该私有方法重命名为非 Qt 关键字（如 `emit_event`）；建议同时为 core 头加一道“与 Qt 关键字同名标识符”静态核对。

### B-02（严重，QML 装载）`main.qml` 未导入 `pages` 目录，`DeniedView` 不是类型
- 位置：`app/src/ui/qml/main.qml:98` 使用 `DeniedView { ... }`，但文件仅 `import "."` 与 `import "components"`，**缺 `import "pages"`**。
- 运行证据（diag2 smoke，offscreen）：`qrc:/qml/main.qml:98:17: DeniedView is not a type` → `QML load failed (qrc:/qml/main.qml)` → exit 3。
- 修复方向：`main.qml` 增加 `import "pages"`（`pages/*.qml` 已各自 `import ".."`/`import "../components"`，导入后即可解析）。

### B-03（严重，QML 装载）QML 属性名以大写字母开头（非法）
- 运行证据（diag3 smoke，已含 B-01/B-02 修复）：`qrc:/qml/Theme.qml:51/63/75 Property names cannot begin with an upper case letter` + `Type Theme unavailable` → `Type StatusItem unavailable` → `Type TopStatusBar unavailable` → `QML load failed`。
- 影响面（静态枚举）：
  - 生成物 `app/src/ui/qml/Theme.qml` 与 `Tokens.qml`：`property QtObject L1/L2/L3`（源自 `docs/hmi/design_tokens.yaml` 的 `alarm.L1/L2/L3`；`app/tools/gen_tokens.py:ident()` 未处理首字母大写）。
  - 页面/组件内断言式属性名：`pages/P1Realtime.qml:16 property var L`、`pages/P3Replay.qml:14 property var R`、`pages/P4AllCameras.qml:13 property var G`、`pages/P6Maintenance.qml:14 property var M`、`components/RecoveryOverlay.qml:8 property var R`。
- 连带问题：令牌 selftest 反而**固化**了非法命名（`app/tools/gen_tokens.py:177` 断言 `QtObject L1`），`check_no_hardcoded_ui` 只查颜色/字号，均无法发现 QML 合法性错误。
- 通用修复方向：
  1. `gen_tokens.py` 对令牌键做**QML 标识符校验/规范化**（拒绝或转换首字母大写），并让 selftest 断言生成物为合法 QML 标识符；
  2. 令牌键 `alarm.L1/L2/L3` 改为合法（如 `level1/l2/l3` 或小写 `l1/l2/l3`），同步 `Theme.qml`/`Tokens.qml` 与使用点 `pages/P2AlarmCenter.qml`、`pages/P3Replay.qml`；
  3. QML 中 `property var L/R/G/M` 等别名改为小写并同步引用点。
- 备注：Qt 5.15 的 `qmllint` **不报告**此类错误（`qmllint-...-r1.log` 全 0 发现），因此**必须以真实 QML 装载冒烟作为验收手段**。

---

## 6. 环境修复记录（授权内，已复核）

| 项 | 内容 |
|---|---|
| 问题 | 目标缺 Qt5 Quick/Qml 开发包，阻止目标构建（历史边界 E-01b） |
| 授权 | `environment_repair.enabled=true`，`scope=development`，`allow_target_changes=true`，`allow_system_packages=true` |
| 命令 | `sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends qtdeclarative5-dev qml-module-qtquick2 qml-module-qtquick-controls2 qml-module-qtquick-window2` |
| 结果 | 全部 `5.15.3+dfsg-1` 安装成功；`/usr/lib/aarch64-linux-gnu/cmake/Qt5Quick/Qt5QuickConfig.cmake` 就位；`QT_QML=yes` |
| 复核 | 目标 `Release` 构建推进到 `[100%] Built target railhmi`（诊断轮）；应用可启动并读配置 |
| 未做 | 不重启；不清数据；不重装系统；不改网络/驱动/服务/相机；不改 `cpu-performance.service`；未 `systemctl enable` `railhmi.service` |
| 记录 | 安装前后版本、命令、退出码均存于 `release-build-192_168_0_103-20261017-release-r1.json`（`repairs` 段） |

---

## 7. 边界与未执行（如实记录）

1. 未执行 `systemctl enable` / 整机重启自启（ENV-E 现场操作，需现场授权且涉及重启）。
2. 未在用户 X 会话启动 GUI（避免干扰操作员桌面）；冒烟仅用 `QT_QPA_PLATFORM=offscreen` 有限等待。
3. 6 路真实 RTSP 取流、真实模型检测框/掩码验证属取流/推理工作项，本轮未测。
4. **本能力未修改任何产品源码**；B-01/B-02/B-03 属实现职责（同一角色、不同 capability），以本报告与证据交接。
5. 主机与目标版本分开标识；主机无编译器，不做主机构建声明。

---

## 8. 证据文件（`docs/hmi/release/`）

| 文件 | 说明 |
|---|---|
| `board-release-probe-192_168_0_103-20261017-release-r1.md` / `.json` | 板端构建资源/支持范围只读探针（20 项事实） |
| `release-build-192_168_0_103-20261017-release-r1.json` | 首次目标构建（**B-01 编译失败**）+ 3) 依赖修复记录 |
| `release-build-192_168_0_103-20261017-release-diag.json` | 诊断：仅 `emit` 改名后构建到成功（口径修正前的中间轮） |
| `release-build-192_168_0_103-20261017-release-diag2.json` | 诊断：`emit` 改名 → 构建 OK + 二进制信息 + smoke（**B-02**） |
| `release-build-192_168_0_103-20261017-release-diag3.json` | 诊断：`emit` 改名 + `import "pages"` → smoke（**B-03**） |
| `qmllint-192_168_0_103-20261017-release-r1.log` / `.json` | 板端 `qmllint` 静态检查（0 发现，证明其不覆盖 B-03） |

发布工具（可复现本轮全部步骤）：
- `app/tools/board_release_probe.py`（只读资源探针）
- `app/tools/board_release_build.py`（依赖修复 → 目标构建 → 冒烟 → 打包 tar.gz；含诊断开关 `--diag-rename-emit` / `--diag-add-pages-import`，仅作用于目标 `/tmp` 副本）
- `app/tools/board_qmllint.py`（板端 QML 静态检查）

---

## 9. 复现

```sh
# 目标资源/支持范围（只读）
python app/tools/board_release_probe.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp 20261017-release-r1
# 目标 Release 构建（当前版本应复现 B-01 编译失败）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp 20261017-release-r1
# 诊断：确认三类缺陷相互独立/修复充分（仅目标临时副本）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp 20261017-release-diag3 \
  --no-install-deps --diag-rename-emit --diag-add-pages-import
# QML 静态检查（注意：5.15 qmllint 不覆盖大写属性名，需配合装载冒烟）
python app/tools/board_qmllint.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp 20261017-release-r1
```

---

## 10. 交接（handoff，供实现能力与验证能力）

**对实现能力（`desktop_app_implementation`）**：修复 B-01（`emit` 改名）、B-02（`main.qml` 增 `import "pages"`）、B-03（令牌键与 QML 别名合法化 + `gen_tokens.py`/selftest 校验），保持通用机制、不改设计语义。

**对验证能力（`software_verification` / `ui_implementation_review`）**：修复后必须**同版本重跑两项通过证据**，并在范围为**新增目标 Qt `Release` 构建 + QML 装载冒烟**（依赖已就绪，可用 `board_release_build.py` 的 `--no-install-deps` 复跑；`qmllint` 不足以替代装载冒烟）。

**回归本能力**：上述修复且两项验证 PASS 后，重跑 `board_release_build.py` 产出正式发布包（tar.gz + sha256），届时按 §2/§3 出具 `RELEASE_READY` 清单。
