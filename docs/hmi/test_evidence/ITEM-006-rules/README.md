# ITEM-006 证据 — railhmi-core 检测职责与报警规则/事件层

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-006`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §8（检测职责/规则/级别映射）/§9；`docs/hmi/INTERACTION_DESIGN.md` v2 §6.1/§6.4/§7.3；PRD §8.2；`docs/hmi/TEST_DESIGN.md` TC-CORE-04、TC-CORE-05
- 源码: `app/src/core/include/railhmi/core/rules.h`、`app/src/core/src/rules.cpp`
- 测试: `app/tests/core/test_rules.cpp`；运行器 `app/tools/board_build.py`（板端 CoreTests）

## 1. 交付内容

1. **按 `ai_profile` 分派判定**（`RuleEngine::on_detection`）：
   - `front`：`in_roi`（危险区 ROI 命中）+ 置信度≥阈值 → 人员 `PersonOnRail` / 障碍物 `FrontObstacle`（**L1**）；
   - `pantograph`：三态持续确认，仅 `Abnormal`（升弓不到位）→ `PantographAbnormal`（**L2**）；
   - `wheel_rail`：轮轨明显异常/疑似脱轨 → `WheelDerail`（**L1**）；
   - `cab_door`：车门未关严 → `DoorAjar`（**L2**）；
   - `none`（左右侧广角）：**不产生任何事件**。
2. **多帧确认**：在 `confirm_window_ms` 内累计命中帧数，达 `confirm_frames` 才产生事件；帧间隔超窗口则计数重置。
3. **跟踪 ID 与抑制窗口**：以 `camera|type|track_id` 为键；同一 track 在 `suppress_window_ms` 内不新增事件，仅更新已有事件的距离/置信度/最后时间（ALM-005）；超窗口后可再产生新事件。
4. **级别映射严格 PRD §8.2**（`map_level`/`map_device_level`，UI 不得改写）：
   前方人/物=1、轮轨=1、受电弓异常=2、车门未关严=2、前方主摄离线/冻结=2、侧视离线=3。
5. **设备事件**：`on_device_event`（前方主摄=2、侧视=3、其它=2），窗口内重复事件抑制。
6. **确认只改状态不删除**：`ack(id)` 只把 `ack_state` 置为 `Acked`，事件保留可检索；`find`/`unacked_count` 可查。
7. **ROI 命中辅助**：`roi_contains` / `find_roi`（归一化矩形 [0,1]，配合 `roi.yaml`）。

## 2. 实测（板端本机，经登记 SSH 入口）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `./build-core/tests/railhmi-core-tests` | **176 passed / 0 failed**（平台 10 + 配置 34 + 流 38 + 规则 94） | `...-ITEM006-r2.transcript.json` → `tests` |
| `railhmi-config-check ./config` | OK（6 cameras / 4 roi / 1 integrations）退出 0 | 同上 → `config-check-good` |
| 坏配置 | 报 `thresholds.confirm_frames: required field missing` 退出 1 | 同上 → `config-check-bad` |

### 2.1 级别与职责（r1 原文摘录）

`person_on_rail=L1`、`front_obstacle=L1`、`wheel_derail=L1`、`pantograph_abnormal=L2`、`door_ajar=L2`、
`front_camera_device=L2`、`side_camera_device=L3`、`device front primary=L2`、`device side=L3`；
`pantograph normal -> no event`、`ai_profile=none -> no event`。

### 2.2 多帧/抑制/确认（r1 原文摘录）

`front person: event after confirm_frames`、`front outside ROI -> no event`、`front low confidence -> no event`、
`front frames beyond confirm window -> no event`；`same track within suppress window -> no new event`、
`suppressed event distance updated`、`different track -> new event`、`same track after suppress window -> new event`；
`ack does NOT delete event`、`ack_state becomes acked`、`unacked count 0 after ack`。

## 3. 边界与后续

- 本层做**规则/事件逻辑**；ROI 命中所需的检测框中心、置信度、距离、受电弓/车门/轮轨状态由推理/后处理层（ITEM-017）提供。
- 事件持久化（SQLite `events`/`event_targets`）归 ITEM-007/008；本层提供事件数据与 ack 语义。
- 快照/保护片段/声音/横幅呈现归后续工作项；本层事件字段已含 `snapshot_ref`/`record_available` 占位。
- 设备事件级别只按 PRD §8.2 列举的三类（前方主摄=2、侧视=3）；其它相机设备故障按通用 `Device`=2，未在 PRD 中单独规定。

## 4. 证据文件

- `build-CoreTests-192_168_0_103-20261015-ITEM006-r2.{transcript.json,log}`（**权威(v2 修复后)**：176/176 + config-check）
- `build-CoreTests-192_168_0_103-20261015-ITEM006-r1.{transcript.json,log}`（v1：124/124，保留追溯）
- `VERIFY-ITEM-006.md`、`verify_rules_ext.cpp`、`verify_rules_fixtures.py`、`VERIFY-FIXTURES-*`（软件验证交付）

## 6. v2 修复记录（响应软件验证 TEST_FAIL）

软件验证 `result_code=TEST_FAIL`，但其自身证据与 stdout 的结论为 **TEST_PASS**（板端独立复跑 124/124、自有扩展夹具 65/65、
config-check 好/坏一致），未指出任何实现缺陷；可操作项为观察 O-1/O-2/O-3。本轮修复：

1. **O-1 设备类别映射显式化**：新增 `device_category_from_position(CameraPosition)`（primary→FrontPrimary、left/right→Side、
   roof/underfloor/cab→Other）与 `to_string(DeviceCategory)`，使设备级别不依赖调用方隐式选择；`Other` 仍按 PRD 未列举的通用设备 = L2（与验证 O-1 同口径）。
2. **吸收验证侧边界夹具为第一方回归**：将验证 `verify_rules_ext.cpp` 的边界用例落为 `test_rules.cpp` 断言（不再仅依赖外部夹具）：
   枚举 to_string 身份、跨类型键隔离、设备/检测通道互不干扰、非候选帧整相机重置、emit 后确认周期重置、抑制窗口严格 `<` 边界、
   抑制更新刷新 `last_update_ms`/距离、ack 幂等与跨事件隔离、ROI 角点含端点。
3. **重新板端实测**：`ITEM006-r2` → **176 passed / 0 failed**（规则段 42→94，新增 52 条第一方断言）。

> 说明：本能力不自行判定验证结论对错；对“结果码 TEST_FAIL / 证据叙述 TEST_PASS”的不一致，按可操作观察修复并扩大第一方覆盖后重新提交，供验证与 Runtime 重新判定。

## 7. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-006-rules --stamp <tag> --run-tests
# 板上直接: ./build-core/tests/railhmi-core-tests
```
