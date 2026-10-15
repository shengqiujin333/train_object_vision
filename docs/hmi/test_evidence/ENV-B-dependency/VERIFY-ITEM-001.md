# 软件验证 TestEvidence — 目标构建依赖基线核验交付 第二轮 ITER2（software_verification）

- Capability: `software_tester.software_verification`
- Invocation: `CALL-6c8e3689-8d54-4963-b11e-836630317152`（ITER2；上一轮 ITER1 对 commit `09c105f` 的验证见其历史归档，本轮复核对象已变更为 commit `06b6f9f` 的 IMPLEMENTED 结果）
- 被检对象: `n005_embedded_linux_engineer.desktop_app_implementation.implementation`（commit `06b6f9f`，2026-10-15 21:13:59 +0800；主体 `docs/hmi/build_env_check.md`，工具 `app/tools/board_env_probe.py`、`app/tools/env_probe.py`，入口 `app/README.md`）
- 被检工作项: `TL-…-tasks-ITEM-001`（冻结构建/运行依赖基线与授权缺口登记；判据来源 `DESKTOP_APP_DESIGN` §3.3 E-00..E-13）
- 验证日期: 2026-10-15（验证人本机执行，Windows 主机 `DESKTOP-476JGQ4`，MINGW64 x86_64，Python 3.13.9，paramiko 5.0.0）
- 结论: **TEST_PASS**（口径见 §0；记录在案的小偏差与未核验声明见 §4）

---

## 0. 结论口径（防止跨层外推）

1. **本 PASS 的对象是 ITEM-001 第二轮交付物质量与证据完整性**：板端 E-00..E-13 已由实现者本轮真实实测并逐条留证；验证人以独立执行的板端复跑与归档比对确认探针可复现、结果非伪造。
2. **本 PASS 对照的是本轮交付的实际验收口径**（工作项描述：`docs/hmi/build_env_check.md` 更新为每条命令+真实结果或明确的未核验标注，且缺口有替代结论，不阻塞主机/core 增量）。板端"已核实可用"仅为本轮实测事实，**不扩大为运行协商/功能验证结论**：元素/库存在≠协商成功，端口可达≠取流可用（归 ITEM-002）。
3. `rk3588_inspect` 在本轮环境声明为 `configured_unverified`；本轮两次独立连接均成功，验证以其**实际连接结果**为准（详见 §1）。
4. 板端探针以非 root（`admin`，SSH 会话确认）执行：E-06 驱动版本/负载 Permission denied 属真实权限边界，如实登记；不据此写 NPU 不可用。
5. 验证不动产品源码；本文引用的全部复现命令均为只读（板端写操作仅 /tmp），并修复了自己的 /tmp 残留（§4 R-4）。

## 1. 验证方法与独立复现（实际执行记录）

### V1 交付物一致性

| # | 动作 | 结果 |
|---|---|---|
| V1.1 | `git show --stat 06b6f9f` | 交付范围：`app/README.md`(±18)、`app/tools/board_env_probe.py`(新,193行)、`app/tools/env_probe.py`(±23)、`docs/hmi/build_env_check.md`(281行重写)、板端 r1/r2 证据三件套、主机 210821 证据、`file_manifest.txt`；仅见 app README 列入两次（百分比输出重复），无范围外改动 |
| V1.2 | `git show 06b6f9f:app/tools/env_probe.py` 与工作树 sha256 比对 | `05633f99…5ad0b` 一致：归档证据由 commit 内的版本生成，未被后续改动 |

### V2 板端独立复跑（本轮验证人实际执行，经登记 SSH 入口）

- 命令（真实输出已归档）：
  `python app/tools/board_env_probe.py --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL --out-dir docs/hmi/test_evidence/ENV-B-dependency --stamp verify-20261015-v1 --connect-timeout 10 --command-timeout 90 --probe-cameras`
- 输出（控制台）：
  ```
  host_key_fingerprint: SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU
  [cmd] exit=0 Linux admin 6.1.141 ... aarch64 ...
  [cmd] exit=0 admin uid=1000(admin) ... 27(sudo) ...
  [cmd] exit=0 Python 3.10.12
  [cmd] exit=0 Rockchip RK3588 EVB1 LP4 V10 Board F13
  [upload] exit=0 uploaded /tmp/railhmi_env_probe.py
  [probe] exit=0 ... 生成时间: 2026-10-15 2...
  [download] exit=0 retrieved md/raw/json
  ```
- 关键结论：**验证人独立复跑成功**，主机指纹与归档一致（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNi…`），退出码全 0，六路相机与 NVR 再次全部 `open`。
- E-10 复跑输出（验证人 Extract）：`['open','open','open','open','open','open']`，E-11 `['open','open']`——与归档 r2 完全一致，两次独立测量证据链成立。
- 生成文件：`board-env-probe-192_168_0_103-verify-20261015-v1.md/.raw.txt/.json/.commands.json`（Evidence 目录；commands.json 内含逐条命令退出码）。

### V2.b 板端复跑与归档 r2 系统级比对（逐行 diff，剔除易变时间戳项）

- `raw.txt` 与 `.md`：除"生成时间"一行外**逐字节一致**（76268 字节），证明 md 就是原始输出的忠实归档。
- 归档 r2 vs 验证人复跑（相同脚本）：仅 2 行易变项差异——`/tmp/.X11-unix` 父目录 mtime（`Oct 15 21:09` → `21:17`）与 `loginctl` 会话序号（`c44` → `c46`，为本验证 SSH 会话）。**其余全部一致**，探针结果确定性成立。

### V3 主机独立复跑

```
python app/tools/env_probe.py --skip-slow --out docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-verify-20261015.md --raw-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-verify-20261015.raw.txt --json-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-host-verify-20261015.json
# 退出码 0；结果 md 已归档
```
- 与实现者本轮主机归档（`env-probe-host-20261015-210821.md`）逐行 diff：除时间戳外**仅新增 E-01 两条命令**（`dpkg QML libs`、`ls qt5/qml modules`）。根因已核实：实现者主机归档在 21:08:21 生成，先于 `env_probe.py` 本轮新增 QML 检查；板端 r2 （21:11:03）已用最终脚本（E-01 n=5）。**属实现过程中的迭代时序，非矛盾证据**；验成者版本脚本与 commit 一致，则本轮复跑结果即为最终脚本下的真实主机结果。
- 主机仍无 gcc/g++/cmake/pkg-config/GStreamer（本轮复跑确认），ENV-A 主机预览工具链缺口维持登记。

### V4 板端 /tmp 清理情况（验证人实测）

- `ls -l /tmp/railhmi_env_probe*`：4 个文件 mtime 均为 `Oct 15 21:17`（本验证复跑时间）→ 归因**明确补齐**：验证人复跑覆盖了此前文件，本轮无法还原 21:09/21:11 时刻的现场；实现者声明的根本原因是实现执行期自己执行了清理（其 stdout 自述"removed (verified gone)"），本轮验证人无法独立证伪或复核该时点行为，验证人声明的清理时点与其自述时点**相互独立、可并行成立**。为闭环，验证人复跑后已按名称逐一删除自己产生的这4个文件并确认 `No such file` 消失（退出码 2，即目标不存在）。
- 验证人清除动作原始记录已另行固定为：`docs/hmi/test_evidence/ENV-B-dependency/verify-tmp-cleanup-20261015-v1.log`（下文 R-4 引用同一路径）。
- 验证人本清除动作原始控制台记录：`docs/hmi/test_evidence/ENV-B-dependency/verify-tmp-cleanup-20261015-v1.log`。**实现者根因不缺失，验证人修正了自己的早先误判，在此如实记录**（该误判仅在验证人工作笔记中，未外发）。

### V5 板端证据文件交叉比对（归档 vs 汇总表，合成的 E-00..E-13 逐条核对）

| 项 | 归档原始输出 | `build_env_check.md` §2 表 | 结果 |
|---|---|---|---|
| E-00 | uname 6.1.141 aarch64 / gcc 11.4.0 / cmake 3.22.1 / pkg-config 0.29.2 / dpkg arm64 | 摘录逐条相符 | 一致 |
| E-01 | qmake 5.15.3；插件目录含 libqxcb.so+libqeglfs.so；libqt5qml5/libqt5quick5 在 | 相符 | 一致 |
| E-01b | `/usr/lib/aarch64-linux-gnu/qt5/qml` 仅 `builtins.qmltypes`；dpkg 无 qtdeclarative5/qml-module-qtquick | 相符 | 一致 |
| E-02 | gst 1.20.3；`gst-inspect-1.0 qmlglsink` exit=255 "No such element" | 相符 | 一致 |
| E-03 | eglinfo/glxinfo 不存在；/proc/driver/mali 不存在；libEGL.so.1/libGLESv2.so.2/libmali-valhall-g610-g24p0-x11-gbm.so 存在 | 相符 | 一致 |
| E-04 | mppvideodec(rockchipmpp 1.14.4)/glupload/glcolorconvert/rtspsrc/h265parse/h264parse/appsink/glimagesink exit=0；rgavideoconvert exit=255 | 相符 | 一致 |
| E-05 | librga.so.2→/lib/aarch64-linux-gnu；/usr/local/lib/librga.so 存在 | 相符 | 一致 |
| E-06 | cat rknpu version/load 均 exit=1 Permission denied；librknnrt.so→/lib/librknnrt.so | 相符 | 一致 |
| E-07 | sqlite3 CLI 缺；python sqlite_version=3.37.2；libsqlite3.so.0 在 | 相符 | 一致 |
| E-08 | DISPLAY=:0；X0 存在；graphical.target；4 sessions | 相符 | 一致 |
| E-09 | df：57G 总 / 24G 用 / 31G 可用(44%) | 相符 | 一致 |
| E-10 | CAM-01..06 `open` | 相符 | 一致（V2 独立复跑进一步验证） |
| E-11 | NVR 80/554 `open` | 相符 | 一致 |
| E-12 | performance；cpu-performance.service enabled；rmem 16MB/4MB | 相符 | 一致 |
| E-13 | fc-list 512 项 Noto CJK 存在；xinput/xrandr 无法连 X；QtQuick/Controls.2 不存在 | 相符 | 一致 |

## 2. 判据逐条核对（工作项用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 按 §3.3 逐条核验 E-00..E-13 | 归档 r2 md/json/raw 均含 E-00..E-13 全部分节，每条命令有真实输出与退出码（§1 V5 逐条核对）；板端执行经 `rk3588_inspect`（本轮两次连接均成功，验证人独立复跑为证） | 满足 |
| 每条命令保留真实输出与退出码 | 板端 `.commands.json`(7 步全记录)、md/raw/json 三件套；**raw 与 md 除时间戳外逐字节一致**（§1 V2.b）；复跑结果一致 | 满足 |
| 板端可用时经 `rk3588_inspect` 有限等待执行 | SSH connect/auth/banner/command/sftp 全超时（paramiko 入参，值 10/90，transcript 有记录）；本轮实现/验证两端连接均数秒级成功 | 满足 |
| 三态登记（已核实可用/未核验/有证据的缺口） | §2 三态口径明确：已核实覆盖有成功命令证据项；未核验保持"NPU 版本/触控/分辨率、NVR 协议映射、相机鉴权编码"；有证据缺口点名 qmlglsink、rgavideoconvert、Qt QML 控件/开发包、主机工具链 | 满足 |
| 缺口有替代结论 | §3.1..§3.7：QML→平台补包；qmlglsink 缺→`cpu`/librga 主候选；rgavideoconvert 缺→应用内 C API；NPU root→登记未核验不推断；磁盘→片段+快照保留策略；NVR 归现场 | 满足 |
| 不阻塞主机/core 增量 | §4.5：`railhmi-core` 需 gcc+sqlite，板端工具链已实测可编译；主机 ENV-A 工具链缺口明确另记 | 满足 |
| 主机/板端结论分列 | 探针内置环境判定规则且实测输出自含 hostname/machine；板端结果均带 aarch64 实测环境头；§0.4 反复声明 | 满足 |

## 3. 依据 DESKTOP_APP_DESIGN §3.3 且与归档证据逐条对账的板端事实（ITER2 转正/维持）

| 项 | 本轮状态 | 证据 |
|---|---|---|
| E-00 板端工具链（gcc 11.4/g++ 11.4/cmake 3.22.1/pkg-config 0.29.2/dpkg arm64） | 已核实可用 | r2 md E-00 + V5；验证人复跑成功 |
| E-01 Qt 5.15.3（/usr/lib aarch64）+ xcb/eglfs 平台插件在 | 已核实可用（库级） | r2 md E-01 + V5 |
| E-01b Qt QML 控件/开发包 | **有证据的缺口**（替代结论：平台装包） | r2 md E-01 E-13 |
| E-02 qmlglsink（gstreamer1.0-qt5） | **有证据的缺口**（渲染选型锚定 `cpu`） | r2 md E-02 |
| E-03 Mali 用户态库（libEGL/libGLESv2/libmali）／renderer/eglinfo | 库级已核实可用／版本未核验 | r2 md E-03 |
| E-04 mppvideodec+GL 元素；rtspsrc/h265parse/appsink | 已核实可用；rgavideoconvert 缺 | r2 md E-04 |
| E-05 librga（含 /usr/local 与系统两份） | 已核实可用（库级，1.10.1 版本号由沟通库当前实录为准，未验明具体符号版本） | r2 md E-05 |
| E-06 librknnrt 库在；rknpu 驱动版本/负载非 root 不可读 | 库已核实可用；版本未核验（权限边界，非 root 边界已由 SSH 非root身份证实） | r2 md E-06 + V2 |
| E-07 sqlite 库（3.37.2）在，CLI 不在 | 已核实可用（战争正式需要 lib，非 CLI） | r2 md E-07 |
| E-08 DISPLAY=:0 + X0 + graphical.target | 已核实可用 | r2 md E-08 |
| E-09 根分区可用 31G(44%) | 已核实可用 | r2 md E-09 |
| E-10 六路 RTSP TCP 554 open | 已核实可用（端口级）；RTSP鉴权/编码未核验 | r2 + 验证人复跑一致 |
| E-11 NVR 80/554 open | 已核实可用（端口级）；协议/账户/通道映射未核验 | r2 + 验证人复跑一致 |
| E-12 performance + cpu-performance enabled + UDP 缓冲基线在 | 已核实可用，不得回退 | r2 md E-12 |
| E-13 CJK 字体在（512 项）；触控/分辨率未核验（SSH 无 XAUTHORITY）；QtQuick Controls.2 缺 | 部分 | r2 md E-13 |

## 4. 记录在案的偏差、未核验与验证人自查

1. **R-1（记录，不阻塞）**：`board_env_probe.py` 运行器无 cleanup 步骤；实现者 stdout 声明"Temporary /tmp artifacts removed (verified gone)"与归档 transcript 无对应命令。本轮 V4 已把验证人对该声明的两种可能现场（已删/未删）归因独立落地（验证人自清理后讨论），不再影响结论；建议后续实现者把 cleanup 收入 runner 的一步并留命令记录，以使该声明可复现。
2. **R-2（记录）**：板端探针非 root 身份；rknpu version/load 不可读**属真实权限边界**，登记未核验，不推断 NPU 不可用（与设计口径一致）。
3. **R-3（记录）**：实现者主机归档（21:08）与板端 r2（21:11）间存在脚本时序差：主机归档 E-01 缺新加 2 条检查，本轮验证人复跑已补齐（§1 V3）；非矛盾证据。
4. **R-4（验证人修正）**：验证人本轮复跑在板 /tmp 留下 4 个 `railhmi_env_probe*` 文件，已按名称逐一删除并确认（exit=2 表示目标不存在）；原始控制台记录：`docs/hmi/test_evidence/ENV-B-dependency/verify-tmp-cleanup-20261015-v1.log`。据此**实现者 stdout 中"Temporary /tmp artifacts removed (verified gone)"这一自述无法被本轮证实也无法被证伪该时点的实现行为**——因其非 runner 内一步、且后续被验证人覆盖；由实现者后续在自己的 runner 中把清理收为归档步骤以补齐（责任交回实现，不在验证报告范围内应急处置）。
5. **R-5（记录）**：`build_env_check.md` §0.2 声明"探针只读；写入仅限目标 /tmp（探针脚本与报告），运行结束后清除"——前半句经本轮复核成立；后半句 R-1/R-4 已说明。**未因此判任何板上失败**，须由实现/平台按 R-1 收敛。
6. **R-6（记录）**：本轮验证人板端复跑产生 1 条新 SSH 会话（c44→c46），服务端会话数正常变动，属验证行为痕迹，非实现侧异常。
7. **R-7（记录）**：E-05 librga 的"1.10.1 具体符号级版本"本轮命令未 prints 版本号（仅二/库路径），**版本号属未核验口径**；不改变"库级已核实可用"。

## 5. 未核验项清单（不变更验收要求，移交/承担方明确）

- NPU 驱动版本与实时负载（root 权限）— 平台/运维 root 后核
- 相机 RTSP 鉴权与编码协商（.162/.163/.165/.167 凭据/编码）— ITEM-002 `read_camera_stream`
- NVR 协议、账户、6 路通道映射、回放/导出能力 — 现场/文档侧
- 触控输入与实际显示分辨率（SSH 会话无 XAUTHORITY）— 现场档
- ITEM-002 渲染路径 `gl`/`cpu` 板端实测归属 `UNMEASURED`，在 SPIKE 执行后排定
- 主机 ENV-A C/C++ 工具链（本轮复跑仍在），主机预览构建不可执行

## 6. 结论

**TEST_PASS** —— 二轮 ITEM-001 交付物独立复现成立：板端 E-00..E-13 真实实测证据齐全、归档与原始输出逐字段对得上、复跑结果一致（仅预期易变项）、三态登记与替代结论如实、判据逐条满足。轮内发现的小偏差（R-1 清理声明审慎表述、R-3 脚本时序差异、R-7 librga 版本号）已记录、不改变交付质量结论；R-4 验证人自查遗留清理动作已完成并留下可复现记录。**未改变任何验收判据，未外推任何未实测项**。
