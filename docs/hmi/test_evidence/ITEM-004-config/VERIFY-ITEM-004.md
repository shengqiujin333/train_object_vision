# VERIFY-ITEM-004 — railhmi-core 配置与状态契约层（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-004`（commit **a2ade63**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/{yaml,config}.{h,cpp}`、`app/config/*.yaml`（4 件）、`app/src/core/tools/config_check.cpp`、`app/tests/core/test_config.cpp`、`app/src/ui/src/main.cpp`（启动校验接入）
- 判据来源: TEST_DESIGN **TC-CORE-01**（ENV-A0）；CA 描述"对合法/非法配置逐项断言，非法配置启动报出具体字段"；DESKTOP_APP_DESIGN §7/§8/§12；INTERACTION_DESIGN §7.2
- 验证端: RK3588 板端本机（192.168.0.103，aarch64；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（独立复跑与独立负例夹具全部与预期一致）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**：逐行审阅 `config.cpp`（必填字段缺报 `required field missing`，路径形如 `cameras[i].field` / `thresholds.x` / `roi[i].x`；跨字段一致性 `ai_enabled↔ai_profile`；重复 id；ROI 引用/越界）、`yaml.cpp`（子集解析器，错误含行号）、基线 4 个 yaml 与设计 §12 表逐字段对照、交互 §7.2 静态字段覆盖、`src/ui/src/main.cpp:27-34` 与 `config_check.cpp` 同源调用 `load_config_dir`。
2. **板端独立复跑**：验证端用实现交付的 `app/tools/board_build.py` 以 `--mode CoreTests --run-tests` 独立上传/构建/运行（传输/构建/测试全程有限等待；`--keep-remote` 后手动有界清理）。结果: 构建 `BUILD_EXIT=0`，测试 **40 passed / 0 failed**（`TEST_EXIT=0`），`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 且报 `thresholds.confirm_frames: required field missing`。sqlite 用例 `[SKIP]`（板端无 libsqlite3-dev，与 ITEM-003 登记一致，属外围缺口非本层判据）。
3. **验证自有负例夹具**（非实现用例，独立构造）：以 sed 对基线配置做最小修改后逐条调用板上 `railhmi-config-check`，每条单独有限等待（60s），共 5 负例 + 1 合法参照：

| 夹具 | 修改 | 期望路径 | 实际输出 | 退出 |
|---|---|---|---|---|
| good_reference | 未修改参照 | OK | `config OK: 6 cameras, 4 roi, 1 integrations; mode=visible render_path=cpu recording_source=both` | 0 |
| display_fps_out_of_range | `display_fps: 25→120` | cameras[0].display_fps | `cameras[0].display_fps: out of range 1..60` | 1 |
| duplicate_camera_id | `CAM-03→CAM-01` | cameras[2].id | `cameras[2].id: duplicate camera id 'CAM-01'` | 1 |
| invalid_spectrum | `spectrum: visible→thermal` | cameras[0].spectrum | `cameras[0].spectrum: invalid value 'thermal'` | 1 |
| bad_url_prefix | rtsp_url→`http://example.invalid/x` | cameras[0].rtsp_url | `cameras[0].rtsp_url: must start with rtsp://` | 1 |
| roi_x_w_overflow | `w: 0.84→0.94`（x+w>1） | roi[0] | `roi[0]: x + w must be <= 1` | 1 |

全部与预期一致；每例退出后 `/tmp/railhmi_fx` 与 `/tmp/railhmi_build` 均按名清理（cleanup 步骤 `fixture_cleanup_done`）。
4. **主机一致性**：实现者权威轮（ITEM004-r2）、验证复跑轮（verify-item004-20261015-v1）、验证夹具会话三者 SSH 主机指纹一致（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），复跑轮 40 断言逐文件比对 r2 同名 PASS。

## 2. 判据逐条核对（TC-CORE-01 / CA）

| 判据 | 结果 | 证据 |
|---|---|---|
| 合法配置逐项装载（cameras/app/thresholds/roi） | ✅ | baseline 16 项 PASS（6 阈值内逐路 ai_profile、4 roi、NVR、storage、resolution 等） |
| 缺失必填/非法 fps/transport/未知 ai_profile 报**具体字段名** | ✅ | 实现 10 断言 PASS + 验证自有 5 负例全部报具体字段路径 |
| 绝不静默使用默认值（必填缺失必报错） | ✅ | `req_*` 系列先报 `required field missing` 再赋值（config.cpp:149-195）；可选字段默认记入 `applied_defaults` 断言 PASS（`missing optional tile_aspect recorded in applied_defaults`） |
| 暴露 §7.2 全部契约字段 | ✅ | `ai_enabled`/`ai_profile`（CameraConfig，逐路断言 front/none/pantograph/wheel_rail/cab_door）；`integration_state`（五枚举 + `initial_integration_state`：NVR registered 但缺 protocol/channel_mapping → **unconfigured** 逐字断言）；`mode`（visible；`mode_ir_connected()==false` 断言 "IR is not connected this phase"）；`recording_source`（local/nvr/both，基线 both 断言） |
| 非法配置启动报出具体字段并非零退出 | ✅ | 板上 config-check-bad `CFG_EXIT=1`（非 Qt 启动路径同源代码）；Qt 入口 main.cpp 同调 `load_config_dir` 非法 `return 4`（Qt 档依赖缺口，启动路径未在板上运行——校验代码同源于 CoreTests/config-check 已实测，风险已被同源证据覆盖） |

## 3. 观察与边界（不改变 PASS 结论；交实现/后续项参考）

- **O-1（观察）**: 静态校验允许 `app.yaml` 写 `mode: ir`（枚举含 both 值），本期"ir 未接入"由 `mode_ir_connected()==false` 显式呈现而非校验拒绝。UI 模式位为固定显示、无切换控件，无虚构数据路径；建议实现侧在 IR 接入前考虑对 `mode: ir` 显式报错或将拒收留待 IR 工作项（不构成本期判据缺口）。
- **边界**: 本层只覆盖**配置与静态状态契约**。§7.2 运行时字段（`state/fps/bitrate/last_frame_ts/quality_flags/reconnect_count`、AI 运行态、录像段）属 ITEM-005..008/取流与推理层，本证据不外推。`integration_state` 的 probing/reachable/unreachable 转换由 ITEM-017（NvrProbe）驱动，本层仅初始 unconfigured/unverified 正确。
- **环境事实**: 板端无 libsqlite3-dev/Qt5 Quick（ITEM-003 登记），不影响仅标准库的 core 层；测试二进制 173848 字节 aarch64。
- **易变项说明**: 首次后台复跑轮（旧 stamp 参数），因 runner 默认清理使夹具会话 `cd` 失败， dissected 输出无效；其 transcript 已被最终有效轮覆盖（现为 keep-remote 有效轮），无效轮未作为判据。

## 4. 复现

```sh
# 独立复跑（CoreTests + config-check good/bad；--keep-remote 供追加夹具，结束手动清理）
python app/tools/board_build.py --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-004-config \
  --stamp verify-item004-20261015-v1 --run-tests --keep-remote
# 板上验证自有负例（逐条；完成清理）
#   夹具命令与逐条输出见 VERIFY-FIXTURES-verify-item004-20261015-v1.transcript.json（含 sed 表达式）
# 收尾清理
#   rm -rf /tmp/railhmi_build /tmp/railhmi_fx
```

## 5. 证据文件

- `build-CoreTests-192_168_0_103-verify-item004-20261015-v1.{log,transcript.json}` — 验证复跑原始日志与逐命令 transcript（指纹一致）
- `VERIFY-FIXTURES-verify-item004-20261015-v1.transcript.json` — 验证自有夹具逐条命令/输出/期望路径/清理
- 实现者被核验交付: `build-CoreTests-192_168_0_103-20261015-ITEM004-r2.{log,transcript.json}`（40/40 + good/bad）
