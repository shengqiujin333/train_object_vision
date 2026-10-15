# ITEM-004 证据 — railhmi-core 配置与状态契约层

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-004`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §7（状态契约）/§12（配置）；`docs/hmi/INTERACTION_DESIGN.md` v2 §7.2/§7.3；`docs/hmi/TEST_DESIGN.md` TC-CORE-01
- 源码: `app/src/core/{yaml,config}.{h,cpp}`、`app/config/*.yaml`、`app/src/core/tools/config_check.cpp`
- 运行器: `app/tools/board_build.py`（经登记 SSH 入口构建并运行 CoreTests）

## 1. 交付内容

1. **无依赖 YAML 子集解析器**（`railhmi/core/yaml.h`）：块映射/块序列/内联流序列 `[a,b]`/标量/注释/引号字符串；不支持的结构返回含行号的明确错误。
2. **配置与状态契约模型**（`railhmi/core/config.h`）：`CameraConfig/AppConfig/ThresholdsConfig/RoiConfig/IntegrationConfig` 与枚举
   `CameraPosition/AiProfile/Spectrum/RenderPath/ImagingMode/RecordingSource/IntegrationState/Role/LogLevel`。
   暴露交互方案 v2 §7.2 的静态契约字段：`ai_enabled`、`ai_profile`、`integration_state`
   （probing/reachable/unreachable/unconfigured/unverified）、`mode`（visible / ir 未接入）、`recording_source`（local/nvr/both）。
3. **校验器**（`config.cpp`）：必填字段缺失/类型错误/值域越界/枚举非法/跨字段一致性/重复 id/ROI 引用未知相机/ROI 越界，
   均报出**具体字段路径**（如 `cameras[1].rtsp_url`、`thresholds.confirm_frames`、`roi[0].camera`）；
   必填字段缺失**绝不静默用默认值**；可选字段默认值记入 `applied_defaults`（可观察）。
4. **配置基线**（`app/config/`）：`cameras.yaml`（6 路，仅可见光；CAM-01 front / CAM-04 pantograph / CAM-05 wheel_rail /
   CAM-06 cab_door / CAM-02·03 none）、`app.yaml`（mode=visible、render_path=cpu、recording_source=both、NVR 登记未配置）、
   `thresholds.yaml`、`roi.yaml`。
5. **启动校验入口**（`railhmi-config-check`，无 Qt）：应用启动路径同源校验；合法打印摘要、非法逐条打印字段并非零退出。
   `src/ui/src/main.cpp` 已接入同一校验（Qt 档启动即校验，非法拒绝启动）。

## 2. 实测（板端本机，经登记 SSH 入口）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `./build-core/tests/railhmi-core-tests` | **40 passed / 0 failed**（含配置域 TC-CORE-01 断言） | `...-ITEM004-r2.transcript.json` → `tests` |
| `railhmi-config-check ./config` | `config OK: 6 cameras, 4 roi, 1 integrations; mode=visible render_path=cpu recording_source=both`，退出 0 | 同上 → `config-check-good` |
| `railhmi-config-check <坏配置>`（删 `thresholds.confirm_frames`） | `config INVALID (1 error(s))`，`thresholds.confirm_frames: required field missing`，退出 1 | 同上 → `config-check-bad` |

### 2.1 配置域断言（节选，r2 原文）

合法：
`baseline config loads without errors`、`baseline has 6 cameras`、`CAM-0x ai_profile=...`（front/none/pantograph/wheel_rail/cab_door）、
`app.render_path=cpu`、`app.mode=visible`、`IR is not connected this phase`、`app.recording_source=both`、
`NVR initial integration_state=unconfigured`、`missing optional tile_aspect recorded in applied_defaults`。

非法（**均报出具体字段**且 `ok=false`）：
`missing required field reports cameras[0].username`、`invalid transport reports cameras[0].transport`、
`invalid ai_profile reports cameras[0].ai_profile`、`ai_enabled/profile mismatch reports cameras[0].ai_profile`、
`missing thresholds field reports thresholds.confirm_frames`、`invalid render_path reports app.render_path`、
`invalid mode reports app.mode`、`unknown roi camera reports roi[0].camera`、`out-of-range roi w reports roi[0].w`、
`missing config file reports file name`。

## 3. 边界与后续

- 本层只做**配置与静态状态契约**；运行时状态（`state/fps/bitrate/last_frame_ts/quality_flags/reconnect_count`、
  AI 运行状态、报警事件、录像段）归 ITEM-005/007/… 实现。
- 相机 IP/URL 仅来自配置；`credential_env` 只存环境变量名，URL 不含凭据。
- 本期 `mode=visible`，`ir` 未接入（`mode_ir_connected()==false`）。
- 构建环境缺口（Qt5 Quick/Qml、SQLite3 dev）见 `../ITEM-003-build/README.md`，不影响本层（core 仅标准库）。

## 4. 证据文件

- `build-CoreTests-192_168_0_103-20261015-ITEM004-r1.{transcript.json,log}`（首轮：40/40）
- `build-CoreTests-192_168_0_103-20261015-ITEM004-r2.{transcript.json,log}`（权威：40/40 + config-check good/bad）

## 5. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-004-config --stamp <tag> --run-tests
# 板上直接：
#   ./build-core/tests/railhmi-core-tests
#   ./build-core/tools/railhmi-config-check ./config
```
