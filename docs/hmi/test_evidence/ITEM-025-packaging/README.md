# ITEM-025 证据 — 目标运行包、systemd 常驻自启与故障自检

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-025`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §15（部署/自恢复/自检）、§15.4；`docs/hmi/TEST_DESIGN.md` FI 目录；`一些说明和要求.txt`（运行包与自启要求）
- 源码/交付: `packaging/railhmi.service`、`packaging/install.sh`、`packaging/VERSION`、`packaging/UPGRADE.md`、`packaging/RESOURCES.md`；`app/src/core/include/railhmi/core/fault_catalog.h`、`app/src/core/src/fault_catalog.cpp`、`app/src/core/tools/fault_probe.cpp`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/src/main.cpp`、`app/tools/check_packaging.py`、`app/tools/board_build.py`

## 1. 交付内容

1. **板端运行目录**：`packaging/install.sh` 创建 `/opt/railhmi/{bin,config,models,resources,data,logs}`，幂等、非交互、有限步骤；安装配置/资源/版本与二进制（若已构建），安装并 `enable` systemd 单元。**不修改 `cpu-performance.service`，不重启整机、不改网络/驱动/相机设置。**
2. **systemd 单元** `packaging/railhmi.service`：`Restart=always`（`RestartSec=3`）、`Environment=DISPLAY=:0`（+`QT_QPA_PLATFORM=xcb`）、`WorkingDirectory=/opt/railhmi`、`ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config`、stdout/stderr 落 `logs/`。**不引用也不修改 `cpu-performance.service`。**
3. **版本与升级回滚**：`packaging/VERSION`（版本/构建/依赖）；`packaging/UPGRADE.md`（升级步骤：板端本机构建→stop→install.sh→start；回滚：还原 `bin.bak.<tag>`；`data/logs` 不删除）。
4. **资源/图标来源许可** `packaging/RESOURCES.md`：颜色/字号均来自生成 Theme；状态字形为 Unicode；字体只引用系统字体族；模型来源与许可登记（当前未引入任何未授权素材）。
5. **故障自检目录** `fault_catalog.{h,cpp}`：FI-01 断流 / FI-02 冻结 / FI-03 AI 退出 / FI-04 磁盘不足 / FI-05 云台超差 / FI-10 录像中断，每项给出**显式提示**与**恢复路径**；`self_check_list()` 给出 7 步自检清单（含 `SC-BOOT` 重启自恢复）。维护页 `P6Maintenance.qml` 新增“故障自检卡”展示。
6. **静态取证** `app/tools/check_packaging.py`（systemd 单元/目录布局/版本·升级·资源/自检卡/注入）。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun`（stamp `20261016-ITEM025-r1`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `HEALTH_WIRE_EXIT=0` `PACK_EXIT=0` `FAULT_EXIT=0`。核心单测 **663 passed / 0 failed**。\n
`railhmi-fault-probe`（板端原文节选）：
```
type=fault-catalog count=6
fault id=FI-01 label=断流 prompt="画面占位：重连中（显示已重连次数与最近帧时间）..." recovery="等待自动重连（退避）或维护页手动重连..."
fault id=FI-05 label=云台超差 prompt="云台卡显示目标值 vs 实际值 + 容差校验失败（超差），前方 AI 进入 NOT_READY" recovery="一键归位 / 重调前方预置位（二次确认）后复校..."
fault id=FI-10 label=录像中断 prompt="时间轴出现录像缺口标记，事件标注“录像不完整/不可用”..." recovery="恢复录像进程/存储；缺口记录保留可检索"
type=self-check count=7
step id=SC-01 inject="railhmi-sim-inject --fi 01（断流）" prompt="该路画面占位 + 重连中 + 重连次数/最近帧时间" recovery="自动重连或手动重连；恢复出帧转在线"
step id=SC-BOOT inject="systemctl restart railhmi（或断电重启）" prompt="显示启动恢复进度（8 步），完成后进入可用状态" recovery="systemd Restart=always 自动拉起；失败查日志"
type=acceptance fi_covered=true boot_step=true prompts_nonempty=true result=PASS
[check_packaging] PASS (ITEM-025 packaging + systemd + fault self-check)
```

判据核对：
- 每种故障有**显式提示 + 恢复路径**（FI-01..05/10；探针 `fi_covered=true prompts_nonempty=true`）✅
- 自检清单含重启自恢复步骤（`SC-BOOT`），对应 §15.4 八步进度 ✅
- systemd 单元 `Restart=always` + `DISPLAY`，且**不含** `cpu-performance`（`check_packaging` 断言）✅
- 运行目录/版本/升级回滚/资源许可齐备 ✅

## 3. 边界

- **未在目标系统 `enable`/重启验证自启**：`install.sh`/单元已交付并在主机静态核对（`PACK_EXIT=0`）；实际 `systemctl enable` + 断电/重启自启属目标系统级（ENV-E，需现场授权且涉及重启整机），本轮未执行，按要求不修改目标系统服务、不重启整机。
- **故障注入证据**：FI 目录与提示/恢复文案来自 `railhmi-fault-probe`（core 层实跑）；界面显式提示由 P6 自检卡 + ITEM-004/005/009/016/019/022 的状态/占位/缺口/告警语义承载（各该项已板端验证），本轮 QML 运行期未执行（板端缺 Qt5 Quick/Qml 开发包 E-01b）。
- 真实模型/图标等外部素材：模型由模型职责提供到 `models/`；`install.sh` 缺失时 WARN 不阻断。

## 3bis. 修复轮 fix-r2（响应 VERIFY-ITEM-025 D-01/D-02）

独立验证（`VERIFY-ITEM-025.md`）判定 TEST_FAIL：两处**静态可证伪的部署契约缺陷**会导致真实部署 `Restart=always` 无限 Crash-Loop。修复如下（不改变已验证通过的故障域/目录/版本本体）：

**D-01（ExecStartPre 引用不存在且无人构建/安装的 `railhmi-version-check`）→ 采验证方建议 (b)：移除该行**
- `packaging/railhmi.service`：删除 `ExecStartPre=/opt/railhmi/bin/railhmi-version-check`，仅保留 `ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config`。
- `packaging/UPGRADE.md`：第 7 行第 1 条由“启动前自检入口 `railhmi-version-check`”改为“应用启动时校验配置与版本并写入 `logs/`”，与真实实现一致（版本自检保留为应用内启动步骤，`main.cpp` 已校验配置并落日志，见选项 (c)）。
- 交付检查器 `app/tools/check_packaging.py` 增补断言：**单元引用的每个 `/opt/railhmi/bin/<name>` 必须在 `install.sh` 中确有安装**（否则 FAIL）。

**D-02（单元传 `--config` 但二进制不解析；配置目录为编译期源码树路径）→ 真正解析该参数（core 单一来源）**
- 新增 GUI 无关核心函数 `railhmi::core::parse_config_dir(argc, argv, fallback)`（`config.h`/`config.cpp`）：解析 `--config <dir>` 与 `--config=<dir>`；无参数退回编译期缺省；**显式空值/无值也退回缺省，绝不静默使用空路径**。
- `app/src/ui/src/main.cpp`：`main()` 改为 `const std::string configDir = railhmi::core::parse_config_dir(argc, argv, RAILHMI_CONFIG_DIR);`，并以 `load_config_dir(configDir)` 载入；同时把解析后的目录写入日志（`config dir: ...`）便于现场核对。单元传入的 `/opt/railhmi/config` 因此被真正消费；无参数时行为与旧版一致（主机/板端 TestRun 不受影响）。
- `app/tests/core/test_config.cpp`：新增 7 条 `parse_config_dir` 用例（空格/等号形式、缺省回退、无值/空值回退、夹在其他参数之间、`argc=0/nullptr`）。
- 交付检查器增补断言：单元若含 `--config`，则 `main.cpp` 必须出现 `parse_config_dir` 且 `load_config_dir(configDir)`。

**反例自检（证明交付检查器现在能检出这两类缺陷）**：`negcheck_packaging_fix.py` 在临时副本上分别重新注入 D-01/D-02 →

```
NEG_HEALTHY_EXIT=0 (expect 0)
NEG_D01_EXIT=1     (expect 1)
NEG_D02_EXIT=1     (expect 1)
NEGCHECK_RESULT=PASS
```

**板端复跑（stamp `20261017-ITEM025-fix-r1`，RK3588）**：`TESTRUN_EXIT=0` / `PACK_EXIT=0` / `FAULT_EXIT=0`；core 单测由 663 → **670 passed / 0 failed**（+7 = `parse_config_dir` 用例，板上编译运行）；`railhmi-fault-probe` 仍 `fi_covered=true boot_step=true prompts_nonempty=true result=PASS`（故障域未回退）。板端 `ui-scan` 中的 `check_packaging.py`（含两条新断言）在板端源码上 PASS。

**未执行项（与验证方边界一致）**：仍未在目标系统执行 `systemctl enable` 或整机重启（ENV-E，需现场授权且涉及重启），故“重启后自动拉起进入可用状态”仍为目标系统级未核验项；D-01/D-02 的修复在 Referenced-binary / Parsed-args 层面为可静态证伪的契约，现已被交付检查器（含反例对照）与板上单测覆盖。

## 4. 证据文件

- `build-TestRun-...-ITEM025-fix-r1.core-evidence_fault-probe-...-fix-r1.log`（FI 目录 + 自检清单 + PASS）
- `...-ITEM025-fix-r1.core-evidence_core-tests-...-fix-r1.log` / `.xml`（**670 passed / 0 failed**）
- `...-ITEM025-fix-r1.transcript.json` / `.log`（修复轮构建 + 单测 + 探针 + 扫描/打包核对（D-01/D-02 断言）+ 取回；含 `upload-packaging`）
- `negcheck_packaging_fix.py`（主机反例自检：健康 0 / D-01 1 / D-02 1）
- 回归留痕：health/status/ai/alarm/clip/replay/maintenance/permissions 探针 + sim-inject
- 首轮（修复前）证据保留：`...-ITEM025-r1.*`

## 5. 复现

```sh
# 主机：打包/systemd/自检静态核对
python app/tools/check_packaging.py --root .
# 主机：反例自检（D-01/D-02 可被检出）
python docs/hmi/test_evidence/ITEM-025-packaging/negcheck_packaging_fix.py
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-fault-probe
# 经登记 SSH 入口（TestRun 含打包核对 + fault-probe）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-025-packaging --stamp <tag>
# 现场部署（需目标系统授权）：bash packaging/install.sh && systemctl start railhmi
```
