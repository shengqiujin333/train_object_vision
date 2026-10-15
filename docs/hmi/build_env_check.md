# railhmi 目标构建 / 运行依赖基线核验（ITEM-001）

- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-001`（冻结构建/运行依赖基线与授权缺口登记）
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §3.3（E-00..E-13）、§3.2/§3.4（环境与授权现状）
- 探针（可复现入口）: `app/tools/env_probe.py`（只读，标准库）
- 板端运行器（经登记 SSH 入口）: `app/tools/board_env_probe.py`
- 本轮证据:
  - 板端（RK3588，本轮实际执行）: `docs/hmi/test_evidence/ENV-B-dependency/board-env-probe-192_168_0_103-20261015-board-r2.md`（+ `.raw.txt` / `.json` / `.commands.json`；r1 为不含相机探测的首次运行）
  - 主机（Windows，本轮实际执行）: `docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-20261015-210821.md`（+ `.raw.txt` / `.json`）
- 结论状态: **板端依赖基线已实测采集**（E-00..E-13 三态登记见 §2）；**目标构建/运行尚未授权范围内验证**（本轮未编译、未取流）；主机结果不替代板端结论。

---

## 0. 执行范围、授权与硬声明

1. 本轮**实际执行**了经项目登记 SSH 入口（`rk3588_inspect`，Paramiko，有限超时）的板端只读探测：目标 `192.168.0.103:22`，用户 `admin`，aarch64 `RK3588 EVB1`。SSH 主机密钥取自受信任 `known_hosts`（默认 RejectPolicy，未使用 AutoAddPolicy），指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，记录于 `.commands.json` 供用户核验。
2. 本轮**未在板端编译、未运行 demo、未读取相机码流、未安装/修改任何系统包**。探针只读；写入仅限目标 `/tmp`（探针脚本与报告），运行结束后清除。
3. 相机/NVR 探测为**端口可达性**（TCP connect），**不发送 RTSP 鉴权、不协商编码**。`E-10` 的“凭据/编码结论”与 `E-11` 的“回放/导出接口能力”**未核验**，属 ITEM-002 渲染 spike 与现场档，不得由本项代替。
4. 主机结果与板端结果**分开记录、不得合并表述**；主机可运行不等于目标可运行；板端“库/元素存在”不等于“运行时协商成功”。
5. 本轮授权较历史轮次变化：`rk3588_inspect`/`rk3588_build`/`rk3588_deploy`/`rk3588_verify` 与各 `*_camera_stream` 在本能力均为 `configured_unverified`（`remote_inspect`/`remote_build`/`remote_deploy`/`remote_verify`/`read_camera_stream` 已授权）。`configured_unverified` 只表示配置与工具要求满足，**不证明在线/安装成功/测试通过**；本轮以实际连接结果为准（结果见 §2）。

---

## 1. 核验方法（可复现）

```sh
# 主机（只读）
python app/tools/env_probe.py --skip-slow \
  --out  docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-<stamp>.md \
  --raw-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-<stamp>.raw.txt \
  --json-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-<stamp>.json

# 目标板（经登记 Paramiko SSH 入口；只读 + /tmp）
python app/tools/board_env_probe.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --out-dir docs/hmi/test_evidence/ENV-B-dependency \
  --stamp <stamp> --connect-timeout 10 --command-timeout 90 --probe-cameras
```

- `env_probe.py` 只读、仅标准库；`--skip-slow` 跳过 `du` 以约束板端执行时长。
- `board_env_probe.py` 使用 `load_system_host_keys()` + 默认 `RejectPolicy`；连接/命令/SFTP 全部显式超时；凭据仅从环境变量读取，不落日志。
- 输出保留**每条命令、退出码与原始输出**（`.md`/`.raw.txt`/`.json`/`.commands.json`）。

---

## 2. 状态总表（E-00..E-13，板端实测）

三态口径：**已核实可用**（本轮板端命令有成功证据）/ **未核验**（未执行或权限/工具不足）/ **有证据的缺口**（本轮实测为缺失或失败）。

| 项 | 内容 | 板端实测结果（本轮，真实输出摘录） | 三态 |
|---|---|---|---|
| E-00 | 架构/工具链 | `uname -a`=Linux 6.1.141 aarch64；`gcc/g++` 11.4.0；`cmake` 3.22.1；`pkg-config` 0.29.2；`dpkg --print-architecture`=arm64 | **已核实可用** |
| E-01 | Qt 版本与平台插件 | `qmake -v`=Qt **5.15.3**（/usr/lib/aarch64-linux-gnu）；平台插件目录含 `libqxcb.so` 与 `libqeglfs.so`；`libqt5qml5/libqt5qmlmodels5/libqt5quick5/libqt5quickwidgets5` 已装 | Qt 基础 + 平台插件：**已核实可用** |
| E-01b | Qt QML 模块与开发包 | `/usr/lib/aarch64-linux-gnu/qt5/qml` 仅 `builtins.qmltypes`；无 `QtQuick/Controls.2`；`qtdeclarative5-dev`、`qml-module-qtquick*` 未安装 | **有证据的缺口**（见 §3.1） |
| E-02 | qmlglsink | `gst-inspect-1.0 --version`=1.20.3；`gst-inspect-1.0 qmlglsink` → **No such element**（exit 255）；`gstreamer1.0-qt5` 未安装 | **有证据的缺口**（见 §3.2） |
| E-03 | GL/EGL (Mali) | `eglinfo`/`glxinfo` 命令不存在；`/proc/driver/mali` 不存在；但 `libEGL.so.1`、`libGLESv2.so.2`、`libmali-valhall-g610-g24p0-x11-gbm.so` 存在 | 用户态库：**已核实可用**；renderer 版本：**未核验** |
| E-04 | MPP 解码与 GL 元素 | `mppvideodec`✓(rockchipmpp 1.14.4)、`glupload`✓、`glcolorconvert`✓、`rtspsrc`✓、`h265parse`✓、`h264parse`✓、`appsink`✓、`glimagesink`✓；`rgavideoconvert`✗（No such element） | 主体**已核实可用**；`rgavideoconvert` **有证据的缺口**（见 §3.3） |
| E-05 | RGA (librga) | `librga.so.2` → `/lib/aarch64-linux-gnu/librga.so.2`；`/usr/local/lib/librga.so` 存在 | **已核实可用**（库级） |
| E-06 | NPU 运行时 | `librknnrt.so` → `/lib/librknnrt.so` 存在；`/sys/kernel/debug/rknpu/{version,load}` = **Permission denied**（需 root） | 库：**已核实可用**；驱动版本/负载：**未核验**（权限） |
| E-07 | SQLite | `libsqlite3.so.0` 存在；Python `sqlite3.sqlite_version`=3.37.2；`sqlite3` CLI 未安装 | 库**已核实可用**（CLI 非必需） |
| E-08 | 显示会话与图形目标 | `DISPLAY=:0`；`/tmp/.X11-unix/X0` 存在；`systemctl get-default`=graphical.target；`loginctl` 4 sessions | **已核实可用** |
| E-09 | 磁盘 | `/` 57G、已用 24G、可用 **31G**（44%）；`/home/admin` 同根分区 | **已核实可用**（容量约束见 §3.5） |
| E-10 | 相机可达性 | 六路 RTSP TCP 554 全部 **open**（CAM-01 .65 / CAM-02 .160 / CAM-03 .162 / CAM-04 .163 / CAM-05 .165 / CAM-06 .167） | 端口可达：**已核实可用**；凭据/编码协商：**未核验** |
| E-11 | NVR 接口 | `192.168.1.4` HTTP 80 **open**、RTSP 554 **open** | 端口可达：**已核实可用**；协议/账户/6 路通道映射/回放接口：**未核验**（Q-NVR） |
| E-12 | governor / UDP 基线 | `scaling_governor`=**performance**；`cpu-performance.service`=**enabled**；`/etc/sysctl.d/99-udp-buffers.conf`：rmem_max=16777216、rmem_default=4194304、wmem_max=4194304；`sysctl net.core.rmem_max`=16777216 | **已核实可用**（不得回退） |
| E-13 | UI 渲染前提 | Noto Sans CJK 等字体存在（fc-list 512 项）；`xinput`/`xrandr` 存在但 **SSH 会话无 XAUTHORITY**（“Unable to connect to X server”/“Can't open display”）；`QtQuick/Controls.2` 目录不存在 | CJK 字体：**已核实可用**；触控/分辨率：**未核验**；QML 控件模块：**有证据的缺口** |

> 完整命令与原始输出见证据文件；上表为定向摘录，未改数。

---

## 3. 缺口、替代结论与后续责任（不阻塞主机/core 增量）

### 3.1 Qt QML 控件/开发包缺失（E-01b，阻塞目标运行）
- 现象：`libqt5qml5/libqt5quick5` 在，但 `QtQuick/Controls.2` 等 QML 模块与 `qtdeclarative5-dev` 不在。
- 影响：基于 Qt Quick/QML 的桌面应用**在板端无法启动**（缺控件模块）且**无法在板端编译**（缺 Qt5Quick CMake 配置）。
- 替代/动作：由**平台/rootfs 职责**按授权安装最小集 `qtdeclarative5-dev qml-module-qtquick2 qml-module-qtquick-controls2 qml-module-qtquick-layouts`（安装前 `df` 复核磁盘）。桌面应用不自行安装系统包。
- 不阻塞：主机/core 逻辑增量（`railhmi-core` 不依赖 Qt）与源码编写。

### 3.2 qmlglsink 缺失（E-02，约束渲染选型）
- 现象：`mppvideodec`+`glupload`+`glcolorconvert`+`glimagesink` 在，但 `qmlglsink`（`gstreamer1.0-qt5`）不在。
- 影响：设计 §4.3 的 `gl` 主候选路径中的 `qmlglsink` 环节**当前不可用**。
- 替代/动作：渲染实现**默认走 `cpu` 路径**（`mppvideodec` NV12 → librga `NV12→BGR` → Qt 纹理上传），与 `DESKTOP_APP_DESIGN` §4.3 及 `render_path_spike.md` §7 一致；`gl` 路径保留为可开启候选，待安装 `gstreamer1.0-qt5` 后由 ITEM-002 spike 实测决定。**不得**把未实测的 `gl` 当唯一主路径。

### 3.3 rgavideoconvert GStreamer 元素缺失（E-04）
- 现象：`gst-inspect-1.0 rgavideoconvert` = No such element；但 librga（E-05）存在。
- 影响：若原计划用 GStreamer 元素做 RGA 转换则不可用。
- 替代/动作：`cpu` 路径直接在应用内调用 **librga C API** 做 `NV12→BGR`（沿用现有 `code/main.cc` 的 `nv2_to_bgr_rga` 语义与成功码 `SUCCESS(1)/NOERROR(2)`）；不依赖该 GStreamer 元素。

### 3.4 NPU 驱动版本与负载不可读（E-06）
- 现象：`/sys/kernel/debug/rknpu/version|load` Permission denied（需 root）。
- 替代/动作：librknnrt 库存在且可链接；驱动版本/实时负载核验需 root 或平台协助，登记为未核验；不据此推断 NPU 不可用。

### 3.5 磁盘容量约束（E-09）
- 现象：可用 31G。
- 替代/动作：按设计 §2.2/§10.3，连续录像由 NVR 承担，本机只保留**报警保护片段 + 快照 + 元数据**，并设保留阈值与提前告警（DATA-003）。

### 3.6 相机/NVR 凭据与编码（E-10/E-11，未核验）
- 现象：六路 RTSP 554 与 NVR 80/554 端口从板端**可达**；`.162/.163/.165/.167` 文档凭据与编码仍未知；NVR 协议/通道映射未提供。
- 替代/动作：`TRAIN_FRONT_RTSP_CREDENTIAL`/`TRAIN_SIDE_RTSP_CREDENTIAL` 已在本进程加载（仅确认变量存在，未传输、未落日志）；RTSP 鉴权与编码协商归 **ITEM-002**（`read_camera_stream`，有限等待），NVR 接口归现场/Q-NVR。注册表按配置读取，缺项不静默。

### 3.7 主机工具链缺口（E-00 主机侧，仍存在）
- 主机：无 `gcc/g++/cmake/pkg-config`，无 GStreamer，Qt 仅 Anaconda x86_64 5.15.2 且无平台插件（见主机证据）。ENV-A/ENV-A0 需主机 C/C++ 工具链，属环境/平台缺口；**不阻塞**已在板端可编译的 `railhmi-core`（目标 gcc 11.4/cmake 3.22.1 已核实可用）。

---

## 4. 结论与未冻结项

1. **板端依赖基线已建立（本轮实测）**：目标工具链、Qt 基础/平台插件、GStreamer 解码与 GL 元素（缺 `qmlglsink`/`rgavideoconvert`）、librga、librknnrt、sqlite3 库、显示会话、磁盘、governor/UDP、CJK 字体、六路相机与 NVR 端口可达性，均已按命令与退出码留证。
2. **有证据的缺口（交平台/rootfs、模型、现场）**：Qt QML 控件/开发包、`gstreamer1.0-qt5`(qmlglsink)、`rgavideoconvert`、rknpu root 可读性、NVR 协议/通道映射、主机工具链。
3. **未核验（非“缺失”）**：NPU 驱动版本与负载（需 root）、相机 RTSP 鉴权与编码、触控/显示分辨率（SSH 无 XAUTHORITY）、NVR 回放/导出能力。
4. **不跨层外推**：端口可达 ≠ 取流可用；库/元素存在 ≠ 协商/渲染成功；板端可编译 ≠ 运行正确。运行协商归 ITEM-002，功能验收归测试。
5. **本项不阻塞主机/core 增量**：`railhmi-core` 只需 C++ 编译器 + SQLite，板端工具链已核实存在；QML 缺口只影响 UI 运行/编译，按 §3.1 交平台补齐。

---

## 5. 与历史轮次的变化（变更记录）

| 日期 | 变更 | 检查方式 |
|---|---|---|
| 2026-10-15（历史） | 首次建立探针 `env_probe.py` + `run_env_check.sh`；仅主机执行，板端全部未核验 | 主机运行并归档；后续评审修复 D-02/D-03/D-04 |
| 2026-10-15（本轮 ITEM-001） | 新增 `board_env_probe.py`（经登记 SSH 入口的板端只读运行器，受信任 known_hosts、全超时、/tmp 隔离）；`env_probe.py` 增加 `--skip-slow`、QML/Quick 库与 qml 目录核验；**首次实际执行板端 E-00..E-13**（含六路相机与 NVR 端口可达性）；按三态登记缺口与替代结论 | 板端命令逐条留证（含退出码）见 `board-env-probe-...-r2.*`；主机重跑见 `env-probe-host-20261015-210821.*`；主机探针退出码 0，板端探针退出码 0 |
| 2026-10-15（本轮 ITEM-001） | 修正：`qmlglsink`/`qtdeclarative5` 缺失、`rgavideoconvert` 缺失为实测结论；渲染默认路径锚定 `cpu`/librga C API | 原始输出对照 §2 表；与 `render_path_spike.md` §7 约束一致 |

---

## 6. 证据清单

板端（RK3588，本轮）：
- `docs/hmi/test_evidence/ENV-B-dependency/board-env-probe-192_168_0_103-20261015-board-r2.md`
- `...-r2.raw.txt`、`...-r2.json`（结构化）、`...-r2.commands.json`（含 SSH 主机指纹、每条远程命令与时延）
- `...-board-r1.*`（首次运行，不含相机探测，保留追溯）

主机（Windows，本轮）：
- `docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-20261015-210821.md` / `.raw.txt` / `.json`

工具源码：
- `app/tools/env_probe.py`、`app/tools/run_env_check.sh`、`app/tools/board_env_probe.py`
