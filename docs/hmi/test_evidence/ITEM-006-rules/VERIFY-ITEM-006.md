# VERIFY-ITEM-006 — railhmi-core 检测职责与报警规则/事件层（软件验证 TestEvidence，v2 修复轮）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-006`（commit **e79e2ae**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED，v2 修复轮）
- 被测对象: `app/src/core/include/railhmi/core/rules.h`、`app/src/core/src/rules.cpp`、`app/tests/core/test_rules.cpp`、`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-04 / TC-CORE-05**（ENV-A0）；DESKTOP_APP_DESIGN §8（检测职责/规则/级别映射）；PRD §8.2；INTERACTION_DESIGN v2 §6.1/§6.4（ack 只改状态）
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（独立板端复跑 176/176 与实现者 r2 log 逐行一致；验证自有扩展夹具 82/82 含 17 条 v2 新增断言；config-check 好/坏一致；指纹链 r1/v1-v4 五轮一致）

## 0. 修复轮承接（本节为何存在）

上一轮（commit cb76fef）本能力证据文件叙述为 TEST_PASS，但结构化结果返回 TEST_FAIL（结果码与叙述不一致，见实现者 r2 README §6）。实现者按可操作项修复并扩大第一方覆盖后重新提交（e79e2ae）。本轮为该 v2 交付的独立重新验证；历史摇滚见 §4。

### 0.1 v2 修复内容核对（r1 → r2，git diff cb76fef..e79e2ae）

1. **O-1 显式化**：新增 `device_category_from_position(CameraPosition)`（Primary→FrontPrimary、Left/Right→Side、Roof/Underfloor/Cab→Other）与 `to_string(DeviceCategory)`；`map_device_level(Other)=L2` 显式分支未变（与上轮 O-1 观察口径一致）。
2. **to_string 补齐**：`PantographState`（unknown/not_raised/normal/abnormal 四态全值）、`DoorState`（unknown/closed/ajar）、`DeviceCategory`（front_primary/side/other）switch 全覆盖。
3. **验证侧边界夹具吸收为第一方回归**：`test_rules.cpp` 规则段 42→94 条（to_string 身份、ROI 角点含端点、跨类型键隔离、设备/检测通道互不干扰、非候选整相机重置、emit 后周期重置、抑制窗口严格 `<`、抑制更新刷新元数据、ack 幂等与隔离）。
4. `rules.h` 仅新增 4 个函数声明与注释，无既有接口变更；`main.cpp`/其余 core 文件未改动。

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**：逐行审阅 r2 diff 与当前 `rules.cpp`/`rules.h`——`map_level` 八型映射（Person/Front/Wheel→L1，Pantograph/Door/FrontCamera/Device→L2，SideCamera→L3）、`map_device_level` 三类、`device_category_from_position` 六位置 switch 全覆盖且无 fall-through 缺口；ai_profile 分派门控（front 需 `in_roi && conf≥min`，person label→PersonOnRail；pantograph 仅 Abnormal；wheel 需 `wheel_abnormal`；cab_door 仅 Ajar；None 直返）；确认/抑制键 `camera|type|track_id`；抑制窗口严格 `< ` 且更新既有事件并重置确认计数；`ack(id)` 只置 `ack_state` 不删除。
2. **板端独立复跑**（验证自有运行器 `verify_rules_fixtures.py` 打包上传 HEAD `app/` 快照 55 文件至板上独立目录 `/tmp/railhmi_verify006_board`，`bash build.sh CoreTests`，全超时 1200s）：构建 `BUILDEXIT=0`；`railhmi-core-tests` **176 passed / 0 failed（TESTEXIT=0）**，[PASS] 行与实现者 r2 log（`build-CoreTests-...-ITEM006-r2.log`）**逐行一致**（规则段 94 条逐项对齐）；`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`（与 r2 逐字一致）。
3. **验证自有扩展夹具**（**82/82**）：源码 `verify_rules_ext.cpp` 单文件 g++ 直编 `rules.cpp`（`-Wall -Wextra` 零告警）。除上轮 65 条边界断言外，新增 **17 条 v2 断言**——`device_category_from_position` 全 6 位置逐值映射 + `map_device_level(Other)=L2`，`to_string(PantographState/DoorState/DeviceCategory)` 全部 12 个枚举值逐值身份。边界断言持续有效：跨类型键隔离、设备/检测通道互不干扰（事件总数 4 与算式逐项对账）、非候选帧整相机重置、emit 后确认周期重置（跨抑制边界后需完整 3 帧）、抑制窗口严格 `<`（恰 1000ms 不抑制/差 1ms 抑制）、抑制更新刷新 `last_update_ms` 不解锁窗口、新事件元数据取新帧且 id 递增、ack 幂等/隔离/被更新不改写状态、ROI 角点三维数学边界、设备空 detail 回退类型名。
4. **运行器安全版式**：受信任 known_hosts 指纹不匹配即中止；connect/auth/banner/SFTP 全显式超时；凭据只读环境变量不落日志；全部 `/tmp` 目录按名清理并记录终态列表。
5. **指纹链**：实现者 r1（ITEM006-r1）、r2（ITEM006-r2）、验证 v1–v4 五轮均 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与 ITEM-002..005 历史一致。

## 2. 判据逐条核对（工作项描述 / TC-CORE-04、05）

| 判据 | 结果 | 证据 |
|---|---|---|
| 按 ai_profile(front/pantograph/wheel_rail/cab_door/none) 分派判定 | ✅ | 176 条断言中规则段 `front person level = L1`、`front person type`、`front obstacle -> L1 FrontObstacle`、`pantograph normal -> no event`、`pantograph abnormal -> L2`、`wheel_rail abnormal -> L1`、`cab_door ajar -> L2`、`ai_profile=none -> no event`；夹具 `cross-type keys count independently` 独立复现 |
| 前方危险区 ROI 判定 | ✅ | `front outside ROI -> no event`；夹具 ROI 角点含端点三维核验 + `roi rejects x beyond x+w` |
| 受电弓三态持续确认 | ✅ | 四态全值静态核对（含 r2 新增 to_string）；`pantograph normal -> no event` + 持续 3 帧 Abnormal 才产事件；夹具跨类型键隔离独立核验 |
| 轮轨标定基准 | ✅ | `wheel_abnormal` 布尔门控 + `confidence_min`；`wheel_rail abnormal -> L1`；夹具 wheel 3 帧周期核验 |
| 车门 ROI | ✅ | `DoorState::Ajar` 门控；`cab_door ajar -> L2`；ROI 命中辅助由夹具数学边界独立核验 |
| 多帧确认（窗口内累计、超窗重置） | ✅ | `front person: event after confirm_frames`、`front frames beyond confirm window -> no event`；夹具三类重置路径（跨窗/低置信/ROI 外）+ emit 后完整新周期 |
| 跟踪 ID + 抑制窗口 | ✅ | `same track within suppress window -> no new event`、`different track -> new event`、`same track after suppress window -> new event`；夹具严格 `<` 边界 + 更新刷新语义 |
| 级别严格 PRD §8.2 映射（人/物=1、轮轨=1、受电弓=2、车门=2、前方主摄离线/冻结=2、侧视=3） | ✅ | 9 条映射断言一致；`map_level` switch 全覆盖；设备侧 r2 显式 `device_category_from_position`（前=2 侧=3 其它=2）由夹具 17 条 v2 断言逐值核验 |
| 同 track_id 窗口内不新增事件 | ✅ | 实现断言 `events size still 1 under suppression` + 夹具窗口内 4 次更新全部沉默 |
| 确认只改 ack_state 不删除事件 | ✅ | `ack does NOT delete event`、`ack_state becomes acked`、`unacked count 0 after ack`、`ack unknown id returns false`；夹具 ack 幂等/隔离/设备事件 ack |
| CoreTests 注入每类事件断言（用户可观察） | ✅ | 板端独立复跑 176/176，规则段 94 条与实现者 r2 [PASS] 行逐行相同 |

## 3. 观察与边界（不改变 PASS 结论）

- **O-1（已在本轮闭环）**: 上一轮 `map_device_level`/`device_category_from_position` 的 PRD 未列举回退（`Other`=L2）已由实现显式函数化并有第一方断言 `device other -> L2 (explicit)` 与验证夹具 v2 断言核验；若用户预期其余四路（roof/underfloor/cab）离线另有级别要求，仍属设计确认事项，不阻塞本层判据（PRD §8.2 未明文，无静默默认）。
- **O-2（边界，不变）**: front 分支 person 以外的 label 全归 `FrontObstacle`， person 成分类别由推理后处理层保障（ITEM-017 边界）。
- **O-3（边界，不变）**: 设备事件 `detail` 字符串承载离线/冻结区分；本层 enum 三类不细分两态。上层文案由 detail 约定保证。
- **环境事实**: 板端无 libsqlite3-dev/Qt5 Quick（历史登记一致）；core 层仅标准库不受影响；收尾 `/tmp/railhmi_verify006`、`/tmp/railhmi_verify006_board`、`/tmp/railhmi_cfg_bad` 全部按名清理（cleanup 段 `fixture_cleanup_done` + `ls` 确认目录不存在；该段 exit=2 为末尾不入 `ls` 对已删目录的确认输出，非清理失败）。
- 本证据不外推到推理/后处理层与相机实取流行为；事件持久化（SQLite）归 ITEM-007/008。

## 4. 结果码/叙述不一致记录（上轮方法教训，验证侧）

上轮证据叙述 TEST_PASS 但结构化结果返回 TEST_FAIL（`CALL-263d0b67`）。本轮以 v4 板端独立复跑（176/176 + 82/82 + config 好/坏 + 指纹一致 全部成立）作为判据唯一来源，**本轮结果码与证据叙述一致返回**；上轮不一致不重新裁决，Flowline 由实现者 r2 文档 §6 已如实承接。

## 5. 复现

```sh
# 验证自有运行器（独立复现 CoreTests 176/176 + config-check 好/坏 + 扩展夹具 82/82）
# 需先加载 TRAIN_RK3588_SSH_CREDENTIAL；MSYS 前缀禁用路径转换
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python \
  docs/hmi/test_evidence/ITEM-006-rules/verify_rules_fixtures.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --app-root app \
  --cpp docs/hmi/test_evidence/ITEM-006-rules/verify_rules_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-006-rules \
  --stamp verify-item006-rules-20261015-v4 \
  --workdir /tmp/railhmi_verify006
# 板上直接: cd /tmp/railhmi_verify006_board/app && ./build-core/tests/railhmi-core-tests
#           g++ -std=c++17 -O0 -Wall -Wextra -Isrc/core/include \
#           /tmp/railhmi_verify006/verify_rules_ext.cpp src/core/src/rules.cpp \
#           -o /tmp/railhmi_verify006/verify_rules_ext && /tmp/railhmi_verify006/verify_rules_ext
# 收尾（运行器已自动按名清理；如残留）
rm -rf /tmp/railhmi_verify006 /tmp/railhmi_verify006_board /tmp/railhmi_cfg_bad
```

## 6. 证据文件

| 文件 | 说明 |
|---|---|
| `VERIFY-ITEM-006.md` | 本文（TestEvidence，TEST_PASS，v2 修复轮） |
| `verify_rules_fixtures.py` | 验证自有板端运行器（指纹校验/全超时/环境变量凭据/按名清理） |
| `verify_rules_ext.cpp` | 验证自有扩展夹具源码（82 断言，含 17 条 v2 新增） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v4.transcript.json` / `.log` | 本轮权威轮 transcript（指纹/逐条命令/退出码/清理确认） |
| `VERIFY-FIXTURES-verify-item006-rules-20261015-v1..v3.transcript.json` / 同名 `.log` | 上轮（cb76fef）过程留痕与权威轮，作为修复承接历史保留 |

## 7. 判据维持说明（不改变验收口径）

- 验证采用与实现者 r2 同版本 HEAD（commit e79e2ae `app/` 快照，上传 55 文件）+ 相同配置层级（板端 aarch64 本机 alpaca build）；级别/判定结论与验收判据未新增也未放宽。
- 主套件 176 条 PASS 行与实现者 r2 逐行一致，本轮作为独立第三方复现记录；与验证自有 82 条扩展断言共同构成本工作项的可复现验证链。
