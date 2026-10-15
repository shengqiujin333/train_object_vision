# VERIFY-ITEM-006 — railhmi-core 检测职责与报警规则/事件层（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-006`（commit **cb76fef**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/include/railhmi/core/rules.h`、`app/src/core/src/rules.cpp`、`app/tests/core/test_rules.cpp`、`app/tests/core/main.cpp`、`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-04 / TC-CORE-05**（ENV-A0）；DESKTOP_APP_DESIGN §8（检测职责/规则/级别映射）；PRD §8.2；INTERACTION_DESIGN v2 §6.1/§6.4（ack 只改状态）
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（独立复跑 124/124 与实现者 r1 规则段逐条一致；验证自有扩展夹具 65/65；config-check 好/坏一致；指纹链三轮一致）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**：逐行审阅 `rules.h`/`rules.cpp` 对照 PRD §8.2 与工作项——`map_level`：PersonOnRail/FrontObstacle/WheelDerail→L1，PantographAbnormal/DoorAjar/FrontCameraDevice/Device→L2，SideCameraDevice→L3；`map_device_level`：FrontPrimary→2、Side→3、Other→2（PRD 未列举的通用设备显式走 L2，见 §3 O-1）；ai_profile 分派：Front 需 `in_roi && conf≥min`（label=="person"→PersonOnRail，否则 FrontObstacle）、Pantograph 仅 `Abnormal` 态、WheelRail 需 `wheel_abnormal`、CabDoor 仅 `Ajar` 态、None 直接返回不产事件；确认/抑制键 `camera|type|track_id`；确认重置双路径（非候选帧按相机全清、跨窗口帧按 key 清）；抑制窗口 `< suppress_window_ms` 更新既有事件不新增；`ack(id)` 只置 `ack_state` 不删除，`find`/`unacked_count` 可检索。
2. **板端独立复跑**（验证自有运行器 `verify_rules_fixtures.py` 上传 app/ 快照到板上独立目录 `/tmp/railhmi_verify006_board`，`bash build.sh CoreTests` 全超时 1200s）：构建 `BUILDEXIT=0`；`railhmi-core-tests` **124 passed / 0 failed（TEST_EXIT=0）**；断言行与实现者 r1 log 逐行 diff **完全一致**（124 条 [PASS] 行相同，含 42 条规则段：级别映射 9、确认/门控 7、跟踪/抑制 8、profile 分派 9、设备事件 3、ack 6、ROI 5，另 82 条平台/配置/流段同轮复现）；`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`（与 r1 逐字一致）。
3. **验证自有扩展夹具**（**65/65**，非实现用例；源码 `verify_rules_ext.cpp` 单文件 g++ 直接联编 `rules.cpp`，命令与输出在 `VERIFY-FIXTURES-...-v3.transcript.json`）——对实现未覆盖的边界独立验证：
   - `to_string` 三枚举身份（device/side_camera_device/unacked/acked）；`map_level(Device)==L2` 通用回退。
   - ROI 数学边界：`(x,y)`、`(x+w,y+h)` 角点含端点命中；x>x+w、y>y+h、x<x 拒绝；`find_roi` camera/name 双缺失。
   - 设备事件：空 `detail` 回退类型名（front_camera_device/side_camera_device）、自定义 detail 逐字保留、`ts_ms==last_update_ms` 设置；设备事件纳入 `unacked_count` 与 `ack`（ack 后 unacked 3→2）。
   - **键隔离（跨类型）**：同相机同 track，wheel 3 帧产 L1 WheelDerail，front 3 帧独立产 L1 PersonOnRail，互不加速/干扰；wheel 抑制不挡 front。
   - **设备与检测通道互不干扰**：设备抑制窗口独立于检测确认/抑制（t=1011/2200 两次设备再发，检测事件正常产生）；检测抑制也不影响设备通道（t=10/1011/2200 三条设备事件 + 1 条检测事件计 4 条，与夹具算式逐项对账）。
   - **非候选帧整相机重置**：1 帧低置信（0.3）或 ROI 外，同相机全部确认计数（跨类型/跨 track）清零；后续 2 帧不达 3 帧，需再连续 3 帧候选命中才确认。
   - **emit 后确认周期重置**：3 帧产事件后，窗口内更新不产事件；跨抑制边界（差 1000 恰不抑制）单帧/两帧不达，需完整 3 帧新周期——证明 emit 后 `Confirm{0,0,0}` 重置语义。
   - **抑制窗口边界（严格 `<`）**：`t_emit + suppress_ms` 恰好不再抑制产新事件；差 1ms 仍抑制；边界产出恰 2 事件。
   - **抑制更新语义**：窗口内更新刷新 `last_update_ms`（=800）但不新增事件、不清窗口（连续 4 次更新均在窗口内全部沉默）；跨窗口新事件元数据来自新帧（ts/conf/dist=1050/0.9/10.0）、id 递增。
   - **ack 幂等与隔离**：ack 两次状态不变；未 ack 事件不受影响；已被抑制窗口更新的 ack 事件状态不被改写。
4. **验证工具配置矩阵三轮遵从**：夹具运行器按证已登记 SSH 入口实现：受信任指纹不匹配即中止；`connect/auth/banner/exec/SFTP` 全显式超时；凭据只读环境变量不落日志；全部 `/tmp` 目录按名清理并记录终态。
5. **指纹链**：实现者 r1（20261015-ITEM006-r1）、验证 v1/v2/v3 三轮均 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与 ITEM-002/003/004/005 历史一致。

### 1.1 方法修正记录（验证侧自查，不含实现缺陷）

- **v1 轮**：本机 MSYS 路径转换把 `--workdir /tmp/...` 传参在进程外改写为 `C:/Users/.../Temp/...`，板上全部命令按无效路径失败且未触达任何板面状态（与 ITEM-005 v1 同型教训，本轮在运行前即以 `MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'` 前缀规避，v1 transcript 为工具配置过程留痕）。
- **v2 轮**：`sftp.putfo` 未先 `seek(0)` 上传了空 tar（`gzip: stdin: unexpected end of file`）；板端独立目录未建立，`build.sh core-tests` 模式名拼写错误（实际为 `CoreTests`）。修复 seek、修正模式名后重跑。
- **夹具断言修正**：v3 首次 run-ext 61/63 后复算发现 2 处夹具算术/时序期望错误——`test_device_vs_detection_independence` 事件总数实际为 **4**（夹具写 5）；`test_emit_resets_confirm_cycle` 的 t=1030 因 `1030-30=1000` 恰好跨出抑制窗口，但确认计数已重置，需再 2 帧才产生事件（夹具 1 帧后断言 size==2）。两处均为夹具期望错误，非实现缺陷；修正后 v3 为 65/65。v1/v2 transcript 与 v3 首轮输出归档为过程留痕，不做判据。
- 三轮有效判据：verify v3（CoreTests 124/124 + 扩展夹具 65/65 + config-check 好/坏 + 清理确认）。

## 2. 判据逐条核对（工作项描述 / TC-CORE-04、05）

| 判据 | 结果 | 证据 |
|---|---|---|
| 按 ai_profile(front/pantograph/wheel_rail/cab_door/none) 分派判定 | ✅ | r1/verify-v3 规则段 `front person level = L1`、`front person type`、`front obstacle -> L1 FrontObstacle`、`pantograph normal -> no event`、`pantograph abnormal -> L2`、`wheel_rail abnormal -> L1`、`cab_door ajar -> L2`、`ai_profile=none -> no event`（实现 9 断言 + 夹具键隔离测试独立复现） |
| 前方危险区 ROI 判定 | ✅ | `front outside ROI -> no event`；`roi contains center point`/`roi rejects outside point`/`roi rejects beyond x+w`；夹具角点含端点三维独立核验（in/out RoiConfig 矩形边界） |
| 受电弓三态持续确认 | ✅ | 四态枚举（Unknown/NotRaised/Normal/Abnormal）静态核对；`pantograph normal -> no event` + 持续 3 帧 Abnormal 才产事件（`pantograph abnormal -> L2`）；夹具跨类型键隔离确认不共享计数 |
| 轮轨标定基准 | ✅ | `wheel_abnormal` 布尔门控 + `confidence_min`；`wheel_rail abnormal -> L1`；夹具 wheel 3 帧周期核验 |
| 车门 ROI | ✅ | `DoorState::Ajar` 门控 + `confidence_min`；`cab_door ajar -> L2`；ROI 命中辅助（`roi_contains`/`find_roi`）由夹具数学边界独立核验 |
| 多帧确认（窗口内累计、超窗重置） | ✅ | `front person: event after confirm_frames`、`front frames beyond confirm window -> no event`；夹具独立复现边界重置（跨窗/低置信/ROI 外三类重置路径全覆盖） |
| 跟踪 ID + 抑制窗口 | ✅ | `same track within suppress window -> no new event`、`different track -> new event`、`same track after suppress window -> new event`、`suppressed event distance updated`；夹具独立复现严格 `<` 边界（恰 1000ms 不抑制/差 1ms 抑制）+ 同 track 跨类型未串扰 |
| 级别严格 PRD §8.2 映射（人/物=1、轮轨=1、受电弓=2、车门=2、前方主摄离线/冻结=2、侧视=3） | ✅ | 9 条映射断言逐项一致（§2.1 原文的 person_on_rail=L1 .. device side=L3）；`map_level` 静态核对 switch 全覆盖，UI 无改写接口 |
| 同 track_id 窗口内不新增事件 | ✅ | 实现断言 + 夹具窗口内 4 次更新历次不新增 + `events size 2 after second track`（跨 track） |
| 确认只改 ack_state 不删除事件 | ✅ | `ack does NOT delete event`、`ack_state becomes acked`、`unacked count 0 after ack`、`ack unknown id returns false`；夹具额外核验 ack 幂等、跨事件隔离、被抑制更新不改写 ack 状态 |
| CoreTests 注入每类事件断言（用户可观察） | ✅ | 板端独立复跑 124/124，规则段 42 条与实现者 r1 [PASS] 行逐行相同 |

## 3. 观察与边界（不改变 PASS 结论；交实现/后续项参考）

- **O-1（观察，交实现参考）**: PRD §8.2 仅列举前方主摄离线/冻结=2、侧视离线=3 两类设备级别；`DeviceCategory::Other`（含车顶/车底/司机室等其余四路设备故障与 NVR 设备事件）按通用 `Device`=L2 处理。该回退是显式 `map_device_level` 分支，非静默默认夹具强制；但 PRD 未明文——若用户预期其余四路离线也属于 L2 或另有要求，需设计/实现确认。ITEM-006 实现者 README §3 已同口径登记，两侧一致。
- **O-2（观察，交实现参考）**: front 分支 `label=="person"` 以外的一切 label（含未知 label、空 label）都归 `FrontObstacle`；person 成分类别完全由推理后处理层保障，本层不做枚举校验（ITEM-017 边界，非本层判据缺口）。
- **O-3（观察，交实现参考）**: `on_device_event` 只产生 `enum` 三类 type；Frozen（前方主摄冻结）与 Offline（前方主摄离线）都归 `FrontCameraDevice` L2——与 PRD §8.2 "离线/冻结=2" 合并口径一致，但本层不做离线/冻结两态区分，详情靠 `detail` 字符串承载。若报警中心 tooltip 需要区分两种文案，由上层/detail 拼写约定保证。
- **边界**: 本层只做规则/事件逻辑；ROI 命中所需检测框中心、置信度、距离、受电弓/车门/轮轨状态由推理/后处理层提供（ITEM-017 边界，实现者 README §3 同口径声明）；事件持久化（SQLite events/event_targets）归 ITEM-007/008。本证据不外推到相机实取流行为。
- **环境事实**: 板端无 libsqlite3-dev/Qt5 Quick（历史登记一致）；仅标准库 core 层不受影响；收尾 `/tmp/railhmi_verify006`、`/tmp/railhmi_verify006_board`、`/tmp/railhmi_cfg_bad` 全部按名清理（cleanup 步骤 `fixture_cleanup_done` + `ls` 确认目录不存在）。
- 扩展夹具在 g++ 直编 `rules.cpp` 下编译零告警（`-Wall -Wextra`），无外部依赖。

## 4. 复现

```sh
# 验证自有运行器（独立复现 CoreTests 124/124 + config-check 好/坏 + 扩展夹具 65/65）
# 需先加载 TRAIN_RK3588_SSH_CREDENTIAL；MSYS 前缀禁用路径转换（见 §1.1）
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python \
  docs/hmi/test_evidence/ITEM-006-rules/verify_rules_fixtures.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --app-root app \
  --cpp docs/hmi/test_evidence/ITEM-006-rules/verify_rules_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-006-rules \
  --stamp verify-item006-rules-20261015-v3 \
  --workdir /tmp/railhmi_verify006
# 板上直接: cd /tmp/railhmi_verify006_board/app && ./build-core/tests/railhmi-core-tests
#           g++ -std=c++17 -O0 -Wall -Wextra -Isrc/core/include \
#           /tmp/railhmi_verify006/verify_rules_ext.cpp src/core/src/rules.cpp \
#           -o /tmp/railhmi_verify006/verify_rules_ext && /tmp/railhmi_verify006/verify_rules_ext
# 收尾（运行器已自动按名清理；如残留）
rm -rf /tmp/railhmi_verify006 /tmp/railhmi_verify006_board /tmp/railhmi_cfg_bad
```

## 5. 证据文件

| 文件 | 说明 |
|---|---|
| `VERIFY-ITEM-006.md` | 本文（TestEvidence，TEST_PASS） |
| `verify_rules_fixtures.py` | 验证自有板端运行器（指纹校验/全超时/环境变量凭据/按名清理） |
| `verify_rules_ext.cpp` | 验证自有扩展夹具源码（65 断言） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v1.transcript.json` / `.log` | 过程留痕（MSYS 路径转换，板上命令未触达板面，非判据） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v2.transcript.json` / `.log` | 过程留痕（putfo 未 seek 空 tar + build.sh 模式名拼写，非判据） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v3.transcript.json` | 权威轮 transcript（指纹/逐条命令/退出码/清理确认） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v3.log` | 权威轮过程 log（CoreTests 124/124 + 扩展夹具 65/65 + config-check + 清理） |

## 6. 判据维持说明（不改变验收口径）

- 验证采用实现者同版本（commit cb76fef app/ 快照，上传校验 55 文件）+ 相同判据层级（板端本机 aarch64 本机构建），与实现者 r1 完全同配置；级别/判定结论与验收判据未新增也未放宽。
- 主套件 124 条 PASS 行与实现者 r1 逐行一致，本轮作为独立第三方复现记录；与验证自有 65 条扩展断言共同构成本工作项的可复现验证链。
