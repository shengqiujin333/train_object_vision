# ITEM-022 证据 — 设备维护页

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-022`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.6（摄像头在线/码流/帧率/最近帧/质量；AI 状态/模型版本；录像进程/剩余容量/完整性；网络/温度/NPU/CPU；云台 Pan/Tilt/Zoom 目标值与实际值 + 容差校验 + 一键归位/重调前方预置位；NVR 登记/未配置/可达/不可达且通道映射未提供时显示“未配置”，无自动修改入口；重连；维护日志时间/用户/结果）、§7.4（云台字段 `pan,tilt,zoom,preset,homing_state,tolerance_ok`）、§8.2/异常表（云台归位失败/超差 → 前方 AI NOT_READY）；`docs/hmi/VISUAL_DESIGN.md` v2 §8.6（分区卡片：摄像头/AI/录像存储/网络温度/云台/日志/NVR；云台目标 vs 实际 + 容差；操作二次确认写日志）
- 源码: `app/src/core/include/railhmi/core/maintenance.h`、`app/src/core/src/maintenance.cpp`、`app/src/core/tools/maintenance_probe.cpp`、`app/tests/core/test_maintenance.cpp`、`app/src/ui/src/maintenance_model.{h,cpp}`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/src/main.cpp`、`app/src/ui/CMakeLists.txt`、`app/tools/check_maintenance_wiring.py`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 维护模型**（无 Qt/SQLite）：
   - **PTZ 容差校验** `ptz_check(state, tol)`：Pan/Tilt/Zoom 实际 vs 目标，输出各轴偏差、`Ok/OutOfTolerance/Unverified` 与可读文本（“Pan Δ0.20° Tilt Δ0.30° Zoom Δ0.05 → 容差内/超差”）。
   - **二次确认动作** `ptz_home` / `ptz_recall_front_preset`（`requires_confirm=true`，执行后 `applied`，携带 before/after 与校验）；`build_ptz_log` 写入维护日志（action/target/result/detail）。
   - **前方 AI 就绪** `front_ai_ready(ptz_ok, ai_available)`：超差 → NOT_READY。
   - **摄像头维护行** `build_maintenance_cameras`：在线、状态中文、帧率、最近帧时间、**质量（过暗/过曝/遮挡/模糊）**、重连次数；陈旧帧判非在线。
   - **录像/存储** `recording_status`（运行状态/剩余容量/已用%）；**网络/温度/负载** `health_text`（NPU/CPU/温度/网络错误）。
2. **UI 桥接** `RailhmiMaintenanceModel`：`ptzState/ptzAction/recording/health/recentLogs` 委托 core；`ptzAction` 二次确认后执行并写日志。
3. **QML** `P6Maintenance.qml`：分区卡片 摄像头/AI/录像与存储/网络与温度/云台/日志/NVR；云台显示目标 vs 实际 + 容差结果 + 前方 AI 就绪 + [一键归位][重调前方预置位]（二次确认模态）；NVR 用 `DeviceCard` 显示登记/状态/通道映射（未配置），**无自动修改入口**。
4. **静态取证工具** `app/tools/check_maintenance_wiring.py`（板端可跑）。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun`（stamp `20261016-ITEM022-r1`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `REPLAY_WIRE_EXIT=0` `AIDBG_WIRE_EXIT=0` `MAINT_WIRE_EXIT=0` `MAINT_EXIT=0`。核心单测 **612 passed / 0 failed**（含 ITEM-022 新增 26 断言：PTZ 容差/动作/日志/前方 AI 就绪/维护行/录像/健康）。\n
`railhmi-maintenance-probe`（板端原文）：
```
type=ptz-check result_ok=ok text="Pan Δ0.20° Tilt Δ0.30° Zoom Δ0.05 → 容差内"
type=ptz-check result_bad=out_of_tolerance text="Pan Δ0.20° Tilt Δ4.00° Zoom Δ0.05 → 超差"
type=action kind=home confirm=true applied=true result=ok
type=action kind=preset_front confirm=true applied=true result=ok
type=maint-log action=home user=maintainer result=ok detail="一键归位 Pan Δ0.20° Tilt Δ0.30° Zoom Δ0.05 → 容差内"
type=action kind=preset_front result=out_of_tolerance
type=maint-log action=preset_front result=out_of_tolerance detail="重调前方预置位 Pan Δ0.20° Tilt Δ4.00° Zoom Δ0.05 → 超差"
type=front-ai ptz_ok=READY ptz_over=NOT_READY
type=cam id=CAM-01 online=true state=实时 fps=25.0 last=<1 秒前 quality=正常
type=cam id=CAM-02 online=false state=画面冻结 fps=— last=10 秒前 quality=过暗
type=recording running=true used_pct=75 free=5.0 GB state=录像中
type=health text="NPU 42% / CPU 33% / 温度 58°C / 网络错误 3"
type=acceptance ptz_within_ok=true ptz_over_detected=true home_logged=true over_tolerance_blocks_front_ai=true result=PASS
[check_maintenance_wiring] PASS (ITEM-022 device maintenance page wired)
```

判据核对（用户可观察点）：
- 调用归位/预置位后显示**目标值与实际值及容差结果**（`Pan 实际/目标 …` + `… → 容差内/超差`）✅
- **超差显示失败并令前方 AI 未就绪**（`out_of_tolerance` → `front-ai … NOT_READY`）✅
- **操作写入维护日志**（`maint-log action=home/preset_front result=… detail=…`）✅
- NVR 显示登记/状态/通道映射=未配置，无自动修改入口 ✅
- 质量/在线/帧率/最近帧、AI 模型版本、录像剩余容量、网络/温度/NPU/CPU 均呈现 ✅

## 3. 边界

- **真实云台控制/重连/录像进程/温度采样**：实际设备操作（PTZ CGI/ONVIF、录像进程、传感器）归平台/现场；本项交付维护模型/校验/日志与页面，桥接以确定性样例驱动（非模拟时空数据）。
- **维护日志持久化**：`MaintenanceLogRow` 复用 ITEM-007 `maintenance_log` 表（`SqliteRepository::insert_maintenance`）；应用尚未打开数据库，桥接维护内存按日滚动（cap 500）列表；真实入库归后续集成。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；证据为 core 板端实跑（612/0）+ 维护探针 + 源码/静态扫描。

## 4. 证据文件

- `build-TestRun-...-ITEM022-r1.core-evidence_maintenance-probe-...-r1.log`（PTZ 容差/动作/日志/前方 AI/维护行/录像/健康原文 + PASS）
- `...-ITEM022-r1.core-evidence_core-tests-...-r1.log` / `.xml`（612 passed / 0 failed）
- `...-ITEM022-r1.transcript.json` / `.log`（构建 + 单测 + 探针 + 扫描/接线 + 取回）
- 回归留痕：status/ai/alarm/clip/replay 探针 + sim-inject

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-maintenance-probe
python3 tools/check_maintenance_wiring.py --root src/ui
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-022-maintenance --stamp <tag>
```
