# railhmi 桌面应用发布包（DesktopReleasePackage）— 构建资源与支持范围清单

- Capability: `embedded_linux_engineer.desktop_release`（节点 `repair_n008_..._delivery`）
- 发布对象（被封装版本）: 修复轮 2 `repair_n008_embedded_linux_engineer_desktop_release_implementation`
  - 实现提交: `a92d008`（修复轮 2：F-01/F-02 之后）
  - 软件功能验证: `5bc49a5` → `docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2/VERIFY-REPAIR-n008-v2.md` = **TEST_PASS**
  - UI 实现评审: `4c4d2d5` → `docs/hmi/ui_review/REVIEW-REPAIR-n008-v2.md` = **TEST_PASS**
- 版本号: `0.1.0`（`packaging/VERSION`，target `rk3588-aarch64`）
- 发布轮 stamp: `20261017-release-r2`
- 执行环境: Windows 主机（源码/静态检查）+ RK3588 `192.168.0.103`（登记 SSH 入口，指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）

---

## 0. 发布判定：**RELEASE_READY**

| 发布门 | 结果 | 证据（同一代） |
|---|---|---|
| 软件功能验证 | **TEST_PASS** | `docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2/VERIFY-REPAIR-n008-v2.md` |
| UI 实现评审 | **TEST_PASS** | `docs/hmi/ui_review/REVIEW-REPAIR-n008-v2.md` |
| 版本一致性 | 一致（`a92d008` / `0.1.0`） | 两份证据均针对该提交、同范围 `complete_delivery` |
| 目标正式构建 | 成功 | `bash build.sh Release` → `build-target/src/ui/railhmi`（ELF aarch64） |
| 界面资源装载/启动 | 通过（实际运行） | offscreen 冒烟 `smoke_exit=124`；Xvfb 六页导航**0 条 QML 错误** |
| 真实程序内容 | 已包含 | 发布包内含真实二进制（见 §1） |

历史闭环（本代之前的 WORK_FAILED 及其修复，均在该代完成并复核）：
B-01 `emit`/Qt 宏冲突、B-02 `main.qml` 缺 `import "pages"`、B-03 QML 大写属性名（令牌 `level1..3`）、
F-01 `P3Replay.qml` 未定义 `chipName`、F-02 缩略图相对基址 → 已在当前版本缺席（评审与验证双证据）。

---

## 1. 发布包（真实交付内容）

| 项 | 值 |
|---|---|
| 发布包文件 | `docs/hmi/release/railhmi-0.1.0-rk3588-aarch64.tar.gz`（665377 B） |
| sha256 | `637d82c0801ca3b9e105506eb904530597af2bffc88ac3c6275e1924a343f01d`（见同名 `.sha256`） |
| 程序二进制 | `bin/railhmi`，ELF 64-bit LSB pie, ARM aarch64，944016 B，sha256 `59e4d94c76170882caac7d68ca10e7696dbced4f8b28f93300e4801500b68743` |
| 包内结构 | `bin/railhmi`；`src/railhmi-src.tar.gz`（app 源码，排除 build-*）；`config/*.yaml`；`packaging/*`；`VERSION`；`RELEASE-INFO.txt`；空目录 `resources/models/data/logs` |
| 构建产物来源 | 目标板本机构建（aarch64，无交叉编译），transcript `docs/hmi/release/release-build-192_168_0_103-20261017-release-r2.json` |

> 发布包内为用户可运行的真实程序与源码，不以截图/文档代替程序。

---

## 2. 交付内容清单（源代码/构建配置/图形资源/运行依赖）

| 类别 | 内容 | 位置 |
|---|---|---|
| 源代码 | core / platform / media / ui + 单测 + 探针工具 | `app/src/{core,platform,media,ui}`、`app/tests`、`app/src/*/tools` |
| 构建配置 | 分层 CMake；四档入口 | `app/CMakeLists.txt`、`app/src/*/CMakeLists.txt`、`app/build.sh` |
| 设计令牌 | 令牌 → 生成 Theme/Tokens（构建期） | `docs/hmi/design_tokens.yaml` → `app/src/ui/qml/{Theme,Tokens}.qml`、`app/resources/tokens.json`（`app/tools/gen_tokens.py`） |
| 图形资源 | QML 外壳/六页/组件 + 资源清单 | `app/src/ui/qml/**`、`app/src/ui/qml.qrc` |
| 配置 | 应用/相机/阈值/ROI（无凭据） | `app/config/{app,cameras,thresholds,roi}.yaml` |
| 部署/运行包 | systemd 单元、安装脚本、版本、升级回滚、资源许可 | `packaging/{railhmi.service,install.sh,VERSION,UPGRADE.md,RESOURCES.md}` |
| 模型（外部依赖） | 分割/检测模型 + 标签（由模型职责投放） | `/opt/railhmi/models`；仓库 `models/{yolov8s-seg.rknn,yolov5s.rknn,rail_labels.txt}` |
| 发布/验证工具 | 资源探针、目标构建打包器、QML 静态守卫、UI 抓取（评审复用） | `app/tools/{board_release_probe.py,board_release_build.py,board_qmllint.py,check_qml_qt_validity.py}`、`docs/hmi/ui_review/tools/board_ui_capture.py` |

**界面资源/插件/接口版本与验证对象一致性**：QML 资源来自被验证提交 `a92d008` 的同一工作树；Qt/GStreamer/librga 目标版本经本轮探针实测（§3），与验证/评审轮一致。

---

## 3. 构建资源与支持范围

### 3.1 主机侧（仅源码/静态检查；无编译器）
- Windows AMD64；Python 3.13 + PyYAML + paramiko；**无 gcc/cmake/Qt**。主机仅做源码审阅、令牌生成与 `check_*` 静态核对；**主机结论不代表目标运行**。

### 3.2 目标侧（RK3588，本轮实测）
| 项 | 实测值 |
|---|---|
| OS / 内核 / 架构 | Ubuntu 22.04.5 LTS / `6.1.141` / `aarch64` |
| 工具链 | gcc `11.4.0`、cmake `3.22.1` |
| 图形 | Qt `5.15.3`（Core/Gui/Quick/Qml）；X11 `xcb`，`graphical.target` active |
| 媒体 | GStreamer `1.20.3`（`rtspsrc`/`mppvideodec`/`glupload`/`appsink`） |
| NPU/RGA/SQLite | `librknnrt` 2.3.2、librga `2.1.0`（+ `/usr/include/rga/im2d.h`）、`libsqlite3.so.0` |
| 显示/服务 | `DISPLAY` 由单元设 `:0`；`railhmi.service` 未安装；`cpu-performance.service` enabled+active（**未改动**） |

原始探针：`docs/hmi/release/board-release-probe-192_168_0_103-20261017-release-r1.{md,json}`

### 3.3 支持范围与构建档位
| 档位 | 内容 | 目标依赖 | 运行位置 | 本轮结论 |
|---|---|---|---|---|
| `Release`(target) | Qt/QML 应用 + core/platform/media | Qt5 Quick/Qml + GStreamer + librga + rknn | RK3588 aarch64 | **构建成功 + 装载运行通过** |
| `CoreTests` | core+platform 单测 | C++/cmake | 板端 | **670 passed / 0 failed** |
| `MediaCheck` | media 层 | GStreamer + librga | 板端 | 通过（上游） |
| `Preview`(host) | 主机预览 | 宿主 Qt | x86_64 | 主机无 Qt，未执行 |
- 图形后端：默认 `cpu`（librga NV12→BGR，运行时 `render path: cpu`）；`gl` 路径保留。
- 支持范围：`OS=Ubuntu 22.04.x aarch64 (RK3588)`、`kernel>=6.1`、`graphics=X11(xcb)`、`Qt=5.15.x`、`GStreamer=1.20.x`。
- **主机版本与目标版本分开标识**：主机=源码级（无编译器）；目标=RK3588 aarch64 本机构建（无交叉工具链）。

---

## 4. 可复现构建/安装入口与操作说明

### 4.1 构建
```sh
# 目标（aarch64 本机构建；缺依赖时 build.sh 明确列出）
cd app && bash build.sh check
cd app && bash build.sh Release        # Qt/QML 应用
cd app && bash build.sh CoreTests      # core 单测
cd app && bash build.sh MediaCheck
```
### 4.2 安装 / 启动 / 配置 / 日志
```sh
bash packaging/install.sh              # /opt/railhmi/{bin,config,models,resources,data,logs}；安装并 enable 单元
systemctl start railhmi                # 现场决定启动；不改 cpu-performance.service
# ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config（--config 由 core parse_config_dir 消费）
# 结构化日志：logs/ + systemd append 的 railhmi.{out,err}.log
```
### 4.3 故障恢复 / 升级 / 回滚
- 故障自检：`build-core/tools/railhmi-fault-probe`（FI-01..05/10 显式提示 + 恢复路径；`SC-BOOT` 重启自恢复）；界面侧 P6「故障自检卡」。
- 升级/回滚：`packaging/UPGRADE.md`（板端本机构建 → 停 → 装 → 起；`bin.bak.<tag>` 回滚；`data`/`logs` 不删）。
- 资源/图标来源许可：`packaging/RESOURCES.md`。

---

## 5. 目标运行证据（本轮）

| 验证 | 命令入口 | 结果 |
|---|---|---|
| Qt `Release` 构建 | `app/tools/board_release_build.py` | `build_exit=0`，`[100%] Built target railhmi` |
| 装载冒烟（offscreen） | 同上（`smoke_exit`） | **124**（12s 内持续存活、无 QML 错误）；`release-smoke-20261017-release-r2.log` |
| 六页真实导航观察 | `docs/hmi/ui_review/tools/board_ui_capture.py` | Xvfb + xdotool，6 页截图非空白且互不相同，**`app.log` 0 条 QML 错误** → `docs/hmi/ui_review/REVIEW-REPAIR-n008-v2.md` |
| 板端全量回归 | `app/tools/board_build.py --mode TestRun` | `TESTRUN_EXIT=0`，**670/0**；令牌/接线/合法性扫描与探针全 PASS → `docs/hmi/test_evidence/REPAIR-n008-release-qt/README.md` §6 |
| 独立复验（测试/评审） | — | 见 §0 两份 v2 证据（各自独立入口、同一代） |

---

## 6. 边界（未覆盖，如实记录）

1. **真实 6 路 RTSP 取流、真实模型检测框/掩码、P3 真实媒体播放/缩略图**：属媒体/取流 handoff；本轮未纳入本发布门。
2. **`systemctl enable` / 断电重启自启**：ENV-E 现场操作（需现场授权且涉及重启整机），未执行；`install.sh` 与单元已交付并经静态核对。
3. **操作员真实 X 会话人工视觉确认**：本轮用 Xvfb 虚拟显示（避免干扰操作员桌面）；主观画面/视觉体验属用户确认范畴。
4. 主机无编译器，不作主机构建声明。

**观察项（非阻断）** O-1：应用启动 banner `simulation=off`（编译期宏 `RAILHMI_SIMULATION`）与 YAML `app.simulation` 同名不同源，仅日志语义可能误导排障，无功能缺陷（实现参考）。

---

## 7. 复现

```sh
# 目标资源/支持范围（只读）
python app/tools/board_release_probe.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp <tag>
# 目标发布包（构建+冒烟+tar.gz+sha256）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/release --stamp <tag>
# 目标全量回归
python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin \
  --stamp <tag> --outdir docs/hmi/test_evidence/REPAIR-n008-release-qt
# 目标六页导航观察（评审工具）
python docs/hmi/ui_review/tools/board_ui_capture.py --host 192.168.0.103 --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui --stamp <tag> --repo .
```
```sh
# 现场部署（需目标系统授权）：bash packaging/install.sh && systemctl start railhmi
```
