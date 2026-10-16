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

## 4. 证据文件

- `build-TestRun-...-ITEM025-r1.core-evidence_fault-probe-...-r1.log`（FI 目录 + 自检清单 + PASS）
- `...-ITEM025-r1.core-evidence_core-tests-...-r1.log` / `.xml`（663 passed / 0 failed）
- `...-ITEM025-r1.transcript.json` / `.log`（构建 + 单测 + 探针 + 扫描/打包核对 + 取回；含 `upload-packaging`）
- 回归留痕：health/status/ai/alarm/clip/replay/maintenance/permissions 探针 + sim-inject

## 5. 复现

```sh
# 主机：打包/systemd/自检静态核对
python app/tools/check_packaging.py --root .
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-fault-probe
# 经登记 SSH 入口（TestRun 含打包核对 + fault-probe）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-025-packaging --stamp <tag>
# 现场部署（需目标系统授权）：bash packaging/install.sh && systemctl start railhmi
```
