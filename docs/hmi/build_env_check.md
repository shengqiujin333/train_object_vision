# railhmi 目标构建 / 运行依赖基线核验（ITEM-001）

- 工作项: `TL-...-tasks-ITEM-001`（冻结目标构建依赖基线）
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §3.4（E-01..E-11）
- 探针（可复现入口）: `app/tools/env_probe.py` / `app/tools/run_env_check.sh`
- 本轮主机原始输出证据: `docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-20251015.md`（+ `.json`）
- 结论状态: **目标板依赖基线未冻结（板端核验未执行）**；见 §0、§12。

---

## 0. 执行范围与硬声明

1. **本轮未连接 RK3588 目标板，未在板端执行任何命令**。项目设备工具已禁用，不得假定物理测试/板端执行已发生。所有 E-01..E-11 的**板端结论均为“未核验”**。
2. 本轮唯一实际执行的是探针在**主机**（Windows x86_64，MINGW64）上的运行；主机结果**不能**作为目标板结论（`DESKTOP_APP_DESIGN` §15、`TEST_DESIGN` §1）。
3. RAW 证据在 `docs/hmi/test_evidence/ENV-B-dependency/`，本文件对其中结果做定向摘录；未执行项明确写“未执行”。
4. “元素/库存在”不等于“协商/运行成功”；`mppvideodec→glupload→glcolorconvert→qmlglsink` 的运行时协商属 ITEM-002（渲染 spike），不在本项结案。

---

## 1. 核验方法

```sh
# 目标板（RK3588，Ubuntu 22.04 aarch64）——待授权后执行
python3 app/tools/env_probe.py \
  --out  docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.md \
  --raw-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.raw.txt \
  --json-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.json
# 或：
sh app/tools/run_env_check.sh docs/hmi/test_evidence/ENV-B-dependency
```

- 探针只读、仅标准库，不安装/不修改系统，不硬编码开发机路径。
- 相机/NVR 网络探测默认关闭，需 `--probe-cameras` 且**项目授权**；本轮未开启。

---

## 2. 状态总表

| 项 | 内容 | 板端状态 | 本轮主机结果（不适用目标板） | 文档基线（来源，未实测） |
|---|---|---|---|---|
| E-00 | 架构 / 工具链（gcc/g++/cmake/pkg-config） | **未核验** | 无 gcc/g++/cmake/pkg-config | `aarch64-linux-gnu-gcc` 11.4、cmake 3.22.1（`README.md` §3.1） |
| E-01 | Qt5.15 + xcb/eglfs 平台插件 | **未核验** | qmake 报 Qt **5.15.2**（Anaconda/x86_64），无平台插件目录 | 目标板 Qt 版本与插件**未见文档**，为关键未知 |
| E-02 | GStreamer + qmlglsink(`gstreamer1.0-qt5`) | **未核验** | 无 gst-inspect / 无 GStreamer | GStreamer 1.20.3 已装（`README.md` §2.1）；qmlglsink **未证实** |
| E-03 | Mali EGL/GLES | **未核验** | 无 eglinfo/glxinfo；`/proc/driver/mali` 不存在 | 目标板 GL 栈**未见文档**，为关键未知 |
| E-04 | `mppvideodec`/`glupload`/`glcolorconvert`/`rgavideoconvert` | **未核验** | 无 GStreamer 元素可查 | 现有 demo 用 `mppvideodec`+`rgaconvert`（`code/main.cc`），其余元素未证实 |
| E-05 | librga | **未核验** | 无 librga | librga **1.10.1**（`README.md` §7），已验证可用于 NV12→BGR |
| E-06 | librknnrt | **未核验** | 无 librknnrt、无 `/sys/kernel/debug/rknpu` | librknnrt **2.3.2**、rknpu 驱动 2.3.0a（`README.md` §2.1） |
| E-07 | SQLite3 | **未核验** | `sqlite3` 3.50.6 / python 模块 3.51.0 | 目标板 libsqlite3-dev **未见文档**，需确认 |
| E-08 | 显示会话（X11 DISPLAY/XAUTHORITY）与 systemd 目标 | **未核验** | 无 X11/systemd（Windows） | 现有 demo 以 `DISPLAY=:0` 运行（`README.md` §3），证明存在图形会话 |
| E-09 | 磁盘容量 | **未核验** | 主机 `/` 118G/115G used（98%，与本项无关） | 板端 `/` 57G 已用 24G（`README.md` §2.1，届时复核） |
| E-10 | 相机可达/凭据（§3.4 E-10） | **未执行**（需授权） | 未执行 | `.162/.163/.165/.167` 凭据/URL 未知 |
| E-11 | NVR 回放/导出接口（§3.4 E-11） | **未核验** | 未执行 | 接口能力未知（Q-06） |
| E-12 | CPU governor / UDP 缓冲基线（**扩展项**） | **未核验** | 不适用（Windows） | `cpu-performance.service` 已 enable；`/etc/sysctl.d/99-udp-buffers.conf` rmem_max=16MB（`README.md` §2.1） |
| E-13 | UI 渲染前提：字体/触控/分辨率/QML 模块（**扩展项, D-04**） | **未核验** | 无 fc-list/xinput/xrandr；无 QML 模块 | `VISUAL_DESIGN` §4/§9 依赖 CJK+等宽字体、触控、两档分辨率、QtQuick.Controls |

---

## 3. E-00 架构与工具链

**目标板命令**：`uname -a`、`gcc --version`、`g++ --version`、`cmake --version`、`pkg-config --version`、`dpkg --print-architecture`
**板端状态**：未核验（未执行）。
**本轮主机原始输出**（`env-probe-host-20251015.md`）：`uname` 为 `x86_64 Msys`；`gcc`/`g++`/`cmake`/`pkg-config`/`dpkg` 均 `command not found`。
**替代结论**：主机当前**没有 C/C++ 工具链与构建系统**，因此 `DESKTOP_APP_DESIGN` §15 定义的“ENV-A 主机预览”在**本机当前不可执行**。这是环境依赖缺口（属平台/环境职责或需另配主机），记入 §12；不得据此推断目标板不可构建（目标板是另一环境）。

---

## 4. E-01 Qt 版本与平台插件

**目标板命令**：`qmake -v` / `qmake6 -v`；`dpkg -l | grep -iE 'qtbase5|qtdeclarative5|qml-module-qtquick'`；`ls /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/`
**判据**：存在 Qt5.15 且平台插件含 `libqxcb.so`（首选）或 `libqeglfs.so`（回退）。
**板端状态**：未核验。
**主机原始输出**：`qmake -v` → `Using Qt version 5.15.2 in D:/Users/123/anaconda3/Library/lib`（win32-msvc spec，Anaconda 自带，**非目标板**）；平台插件目录 `/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms` 在主机不存在。
**替代结论**：目标板 Qt 情况**无任何本轮证据**；若板端无 Qt5.15 或缺失 xcb/eglfs 插件，则需平台职责安装 Qt5.15 最小集（`qtbase5-dev qtdeclarative5-dev qml-module-qtquick*` + `gstreamer1.0-qt5`），或改用 EGLFS/回退渲染（与 ITEM-002 联动）。**此项为阻塞目标构建的前置未知项。**

---

## 5. E-02 GStreamer 与 qmlglsink

**目标板命令**：`gst-inspect-1.0 --version`、`gst-inspect-1.0 qmlglsink`、`dpkg -l | grep -iE 'gstreamer1.0-(qt5|qt6)'`
**判据**：`qmlglsink` 元素存在且可 `gst-inspect`。
**板端状态**：未核验。
**主机原始输出**：`gst-inspect-1.0` 不存在（Windows 未装 GStreamer），无结论。
**替代结论**：板端 GStreamer 1.20.3 已在（文档基线），但 **`gstreamer1.0-qt5` 是否已装未证实**。若缺失：首选补装；不可用时按 `DESKTOP_APP_DESIGN` §4.3 回退到 RGA NV12→BGR + Qt 纹理上传路径，且必须在 ITEM-002 用实测数据确定。

---

## 6. E-03 GL/EGL（Mali）

**目标板命令**：`eglinfo`（或 `glxinfo -B`）、`ls -la /proc/driver/mali`、候选库目录含 `libmali`/`libEGL`/`libGLESv2`
**判据**：GLES renderer 可读、EGL 库存在。
**板端状态**：未核验。
**主机原始输出**：`eglinfo`/`glxinfo` 不存在；`/proc/driver/mali` 不存在（主机无 Mali）。
**替代结论**：目标板 GL 栈**无本轮证据**。若 Mali 库/驱动缺失或 GLES 不可用，则 QML/GL 合成路径不可行，须回退渲染（ITEM-002 决策门）。

---

## 7. E-04 解码与 GL 元素 / E-05 RGA / E-06 NPU

**目标板命令**：
```sh
gst-inspect-1.0 mppvideodec; gst-inspect-1.0 glupload; gst-inspect-1.0 glcolorconvert
gst-inspect-1.0 rgavideoconvert; ldconfig -p | grep -i rga
cat /sys/kernel/debug/rknpu/version; cat /sys/kernel/debug/rknpu/load; ldconfig -p | grep -i rknnrt
```
**板端状态**：E-04/E-05/E-06 均**未核验**。
**主机原始输出**：GStreamer 元素全部不可查；`librga`/`librknnrt` 未发现；`/sys/kernel/debug/rknpu/*` 不存在。
**文档基线（未实测）**：`mppvideodec` 与 `rgaconvert` 已在现有 demo 链路中使用（`code/main.cc`），librga 1.10.1、librknnrt 2.3.2 与 rknpu 2.3.0a 已记录（`README.md` §2.1/§7）。
**替代结论**：
- 若 `mppvideodec` 缺失 → 硬件解码不可用，属平台/视频模块依赖缺口（不由桌面应用弥补）。
- 若 `glupload`/`glcolorconvert` 缺失 → 采用回退渲染路径。
- `librga`/`librknnrt` 版本必须以板端实测为准；**推理运行时版本必须与模型转换版本匹配**（`README.md` §3.1），版本不符属模型/平台缺口，不得由桌面应用改换。

---

## 8. E-07 SQLite

**目标板命令**：`sqlite3 --version`；`dpkg -l | grep libsqlite3`；`python3 -c "import sqlite3,print(sqlite3.sqlite_version)"`
**板端状态**：未核验。
**主机原始输出**：`sqlite3` 3.50.6（32-bit build）；Python `sqlite3` 模块 3.51.0。
**替代结论**：主机 SQLite 可用不能证明目标板。目标板若缺 `libsqlite3-dev`，需平台职责安装；否则持久化层需改用其他嵌入式存储（不建议，会增加与设计偏差）。

---

## 9. E-08 显示会话

**目标板命令**：`printenv DISPLAY`、`printenv XAUTHORITY`、`ls -la /tmp/.X11-unix`、`systemctl get-default`、`loginctl list-sessions`
**判据**：存在可用 X 会话（`DISPLAY` + X socket），且 systemd 默认目标允许图形自启。
**板端状态**：未核验。
**主机原始输出**：Windows 无 X11/systemd（`systemctl`/`loginctl` 不存在）。
**文档基线**：现有 demo 以 `DISPLAY=:0` 运行（`README.md` §3），说明板上存在图形会话，但**当前是否仍有活动会话、自启时序**未核验。
**替代结论**：若无人登录图形会话或 xcb 不可用，systemd 自启需按 EGLFS 或特定 X 会话时序处理（`DESKTOP_APP_DESIGN` §14.3）。

---

## 10. E-09 磁盘

**目标板命令**：`df -h /`、`df -h /home/admin`、`du -sh /home/admin`
**板端状态**：未核验。
**主机原始输出**：`df -h /` → 118G 总、115G 已用、3.4G 可用（98%）；`/home/admin` 在主机不存在。**与目标板无关。**
**文档基线**：板端 `/` 57G、已用 24G（约 33G 可用，`README.md` §2.1）。
**替代结论**：33G 可用不足以支撑 6 路连续录像（约 12GB/小时，见设计 §2.2），必须按设计走 NVR 连续录像 + 本地报警保护片段；运行前须复核实际可用空间并据此设置保留阈值（DATA-003）。

---

## 11. E-10 相机 / E-11 NVR / E-12 平台基线 / E-13 UI 前提

- **E-10**（相机可达/凭据）：**未执行**。默认关闭且需授权；`.162/.163/.165/.167` 四路凭据/URL 仍未知（`TEST_DESIGN` §1 资源缺口）。属 ENV-D 真机档前置未知项，归现场/用户与视频模块。
- **E-11**（NVR 回放/导出接口）：**未核验**。本项只能用端口可达性接近，**回放/导出接口能力需厂商文档或现场核验**，本探针不覆盖该能力（D-03 修复：与“可达性”明确区分，不再合并表述）。
- **E-12**（扩展项，governor/UDP）：板端状态未核验；文档基线为 `cpu-performance.service` 已 enable、UDP 缓冲 16MB 配置已存在（`README.md` §2.1，勿回退/勿删）。主机不适用。
- **E-13**（扩展项，D-04）：UI 渲染前提（CJK/等宽字体、触控输入、xrandr 分辨率、QtQuick.Controls 模块）板端**未核验**。主机无 fc-list/xinput/xrandr 与 QML 模块。字体缺失会导致中文回退/等宽数字错位；触控缺失影响 ≥44px 目标可用性（V-01）；分辨率影响两档布局验收。

> **编号对齐（D-02 修复）**：本文件与 `app/tools/env_probe.py` 现已按 `DESKTOP_APP_DESIGN` §3.4 编号：E-10=相机、E-11=NVR；governor/UDP 与 UI 前提列为扩展项 E-12/E-13。此前 2026-10-15 首次归档的主机报告（`env-probe-host-20251015.md`）使用过渡编号（E-10=governor、E-11=相机/NVR），已被本轮 `env-probe-host-20251015-r2.md` 取代，旧件保留仅作追溯。

---

## 12. 结论与未冻结项

1. **依赖基线未冻结**：E-01..E-11 无一项在 RK3588 目标板执行，全部为“未核验”。本文件不宣称任何板端依赖已满足。
2. **文档基线不能替代实测**：GStreamer 1.20.3、librga 1.10.1、librknnrt 2.3.2 等来自现有交接文档，属“来源可追溯、未本轮实测”，上板前必须用 `env_probe.py` 复测。
3. **主机不可作为目标验证面**：本机（Windows）无编译器/CMake/GStreamer，连 `DESKTOP_APP_DESIGN` §15 的 ENV-A 预览构建当前也不具备；需要另配主机工具链或授权板端构建。**主机可运行绝不等于目标板可运行。**
4. **阻塞/前置未知项（交相应职责，不自行判定）**：
   - 平台/rootfs：板端 Qt5.15 + 平台插件、`gstreamer1.0-qt5`、Mali GL 栈、`libsqlite3-dev`、显示会话与自启时序、磁盘容量复核。
   - 视频模块：`mppvideodec`/`glupload`/`glcolorconvert`/`rgavideoconvert` 与 RGA 色彩转换可用性。
   - 模型职责：librknnrt 版本与模型匹配。
   - 现场/用户：E-11 相机凭据与 NVR 接口。
5. **下一步（本工作项范围内可继续的）**：本项交付可复现的探针与核验流程；板端执行需设备授权，属于未完成缺口而非“已完成”。渲染协商实测归 ITEM-002，工程骨架与构建脚本归 ITEM-003。

---

## 13. 变更与复核记录

| 日期 | 变更 | 检查方式 |
|---|---|---|
| 2026-10-15 | 首次建立探针 `app/tools/env_probe.py` + `run_env_check.sh`；主机执行并留档 | 主机运行探针，原始输出归档至 `docs/hmi/test_evidence/ENV-B-dependency/`；核对了探针对“命令不存在/超时/未执行”的如实记录与 hostname/machine 环境标注 |
| 2026-10-15 | 修复评审 D-02（编号对齐 §3.4）、D-03（E-11 接口能力归属明确）、D-04（新增 E-13 UI 渲染前提）；新增 §14 渲染 spike 状态 | 主机重跑探针生成 `env-probe-host-20251015-r2.{md,json}`；`analyze.py --selftest` 6/6；`run_render_spike.sh --dry-run` 通过 |

---

## 14. 渲染路径选型 spike（ITEM-002）状态与实测数据

> 本节由 ITEM-002 追加。工作项要求“在板端用一路 RTSP 实测 `mppvideodec→glupload→glcolorconvert→qmlglsink`
> 能否协商并显示，失败则实测 `RGA NV12→BGR + Qt 纹理上传` 回退路径，记录 fps/CPU/GPU/画面色彩正确性”。

### 14.1 本轮执行状态

| 项 | 状态 |
|---|---|
| 板端实测（path=gl / path=cpu） | **未执行**（项目设备工具禁用，未连接 RK3588，未访问相机） |
| 选定的主/回退路径 | **未选定**（`UNMEASURED`）——按判据“未实测前不假定主路径成立” |
| 可执行入口 | `app/spikes/render_path/run_render_spike.sh`（板端）+ `analyze.py`（判据） |
| 主机可执行的校验 | `analyze.py --selftest` 6/6 通过；`run_render_spike.sh --dry-run` 通过（见证据） |

**实测数据表（待板端填写；当前为空）**

| path | playing | fps_source | fps_display | CPU%(单核) | GPU load avg | 色彩 spread | 结论 |
|---|---|---|---|---|---|---|---|
| gl | — | — | — | — | — | — | 未实测 |
| cpu | — | — | — | — | — | — | 未实测 |

### 14.2 证据

- 主机自检证据: `docs/hmi/test_evidence/ITEM-002-render-path/host-selftest-20251015.txt`
- 板端测量产物（待授权后生成）: `docs/hmi/test_evidence/ITEM-002-render-path/{gl,cpu,selection}-<stamp>.json`（+ `.png` 快照）
- 判据与程序说明: `docs/hmi/render_path_spike.md`、`app/spikes/render_path/README.md`

### 14.3 说明

- 两条路径存在性依赖 E-02（`qmlglsink`）与 E-05/E-06（librga/librknnrt）；若 E-02 缺失，path=gl 预期在协商阶段失败，应由测量结果而非预判来决定回退。
- 本 spike **不覆盖 6 路并发合成**性能；单路选型完成后，6 路合成与显示帧率分级在后续页面/取流任务验证。
- 主机不含 Qt/GStreamer/librga 工具链，spike **未在主机编译**；编译与运行均为目标板范围。

### 14.4 待授权后执行（交设备授权）

```sh
# 目标板，需项目授权与可用相机
cd app/spikes/render_path
./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot
# 将 selection-<stamp>.json 的 selected 值回填本节 14.1 与 render_path_spike.md
```
