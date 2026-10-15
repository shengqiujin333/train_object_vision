# ITEM-012 证据 — 配置驱动摄像头注册表与 UI 接入

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-012`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §7.2/§7.3（注册表/字段/检测职责）；`docs/hmi/VISUAL_DESIGN.md` §7.12（DetectionBadge，仅 P4/P6）、§7.11（NVR DeviceCard）；`docs/hmi/DESKTOP_APP_DESIGN.md` §8.1/§12；`docs/hmi/TEST_DESIGN.md` TC-SMOKE-06、TC-BADGE-01、TC-INTG-01
- 源码: `app/src/core/include/railhmi/core/camera_registry.h`、`app/src/core/src/camera_registry.cpp`、`app/tests/core/test_registry.cpp`、`app/src/ui/qml/components/{DetectionBadge,CameraTile,DeviceCard}.qml`、`app/src/ui/qml/pages/{P1Realtime,P4AllCameras,P6Maintenance}.qml`、`app/src/ui/src/main.cpp`

## 1. 交付内容

1. **注册表模型（railhmi-core，纯逻辑、可单测）** `build_registry(AppSettings)`：
   按 `cameras.yaml` **顺序**派生六路视图槽位（`CameraSlot{id,name,position,ai_enabled,ai_profile,detection_badge,stream_state,order}`）；
   位置名、AI 使能、检测职责徽标均来自配置；`stream_state` 为运行时占位（配置无关）。
2. **检测职责徽标** `detection_badge_for(ai_profile)`：前方障碍/人员 / 受电弓三状态 / 轮轨 / 车门 / 广角·无检测；
   `detection_badge_visible(page)`：**仅 P4 全摄像头 / P6 设备维护**，司机 P1 及 P2/P3/P5 不显示。
3. **NVR 集成项** `build_integrations(AppSettings)`：`id/name/address/registered/state/protocol/channel_mapping`；
   `192.168.1.4` 已登记、状态 `unconfigured`、协议未提供为 `—`、**通道映射未提供显示“未配置”**，不虚构。
4. **UI 接入**：`main.cpp` 注入 `railhmiCameras`/`railhmiIntegrations`（由配置派生）；
   `CameraTile`（位置名/连接状态/可选徽标）、`DetectionBadge`（低对比标识“广角·无检测”）、`DeviceCard`（NVR）；
   P4/P6 显示徽标，`P1Realtime` 三联 `showBadge:false`（司机不显示徽标）。全部颜色/字号来自 `Theme`。

## 2. 实测（板端本机，经登记 SSH 入口；TestRun）

| 步骤 | 结果 |
|---|---|
| `railhmi-core-tests` | **348 passed / 0 failed**，`RESULT: PASS`（新增注册表域 25 条断言） |
| 注册表关键断言 | `registry has 6 slots`、`registry order = CAM-01..06`、`registry positions`、`badge front/none/pantograph/wheel rail/cab door`、`badge visible on P4`、`badge visible on P6`、`badge hidden on P1 (driver realtime)` |
| 配置驱动 | `rename reflected in registry`、`remove camera -> 5 slots`、`add camera -> 7 slots`、`registry order follows config order`、`missing required field reports cameras[0].rtsp_url` |
| NVR | `NVR id/address`、`NVR state = unconfigured`、`NVR channel mapping = 未配置`、`NVR protocol placeholder —` |
| `gen_tokens.py --selftest` / `check_no_hardcoded_ui.py` | `TOKENS_EXIT=0` / `SCAN_EXIT=0`（UI 无硬编码） |
| `railhmi-sim-inject` | `SIM_EXIT=0` |

“仅改配置增删/改名一路相机即改变视图槽位与徽标”：由 `test_registry.cpp` 的改名/删除/新增/顺序跟随配置用例复现；
“删除必填字段启动报具体缺失项”由 `cameras[0].rtsp_url` 断言（并与 `railhmi-config-check` 启动校验一致）。

## 3. 边界

- **QML 未在板上编译/运行**：板端缺 Qt5 Quick/Qml 开发包（`build_env_check.md` §3.1）；
  本项已验证（a）注册表/徽标/NVR 模型（板端 348/348）、（b）UI 源码无硬编码（扫描 PASS）、
  （c）QML 消费 C++ 注入的配置派生槽位与集成项。**运行期渲染**（司机 P1 无徽标、P4/P6 有徽标）需平台补齐 Qt 后由 UI/测试验证。
- 连接状态/帧率等运行时字段由取流状态机（ITEM-005）接入；本层只消费配置并给出初始占位。
- 不接入红外；不虚构 NVR 通道号。

## 4. 证据文件

- `build-TestRun-192_168_0_103-20261015-ITEM012-r2.transcript.json` / `.log`
- `...-ITEM012-r2.core-evidence_core-tests-...-r2.log` / `.xml`（348/348）
- `...-ITEM012-r2.core-evidence_sim-inject-...-r2.log` / `.json`

## 5. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-012-registry --stamp <tag>
# 板上直接: ./build-core/tests/railhmi-core-tests ；python3 tools/check_no_hardcoded_ui.py --root src/ui
```
