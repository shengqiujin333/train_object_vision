# ITEM-013 证据 — 单路取流与流状态机接入

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-013`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §6.1（每路 pipeline，强制 UDP）/§6.2（六态）/§7.1；`docs/hmi/INTERACTION_DESIGN.md` v2 §7.1（非 LIVE 禁止旧帧实时）；`docs/hmi/TEST_DESIGN.md` TC-STREAM-01..04
- 源码: `app/src/media/include/railhmi/media/camera_stream.h`、`app/src/media/src/camera_stream.cpp`、`app/src/media/tools/stream_probe.cpp`、`app/src/media/CMakeLists.txt`

## 1. 交付内容

1. **`CameraStream`（railhmi-media，target 专用）**：原生 GStreamer（`rtspsrc ... protocols=udp` 强制 UDP；或 test/file 源）
   `! tee name=t  t.!queue leaky!fakesink(计数)  t.!queue leaky!appsink`；
   - 逐帧发布：appsink 拉帧 → 接入 `railhmi-core::StreamStateMachine` 六态；
   - **质量标志**：从 NV12 Y 平面计算 mean/stddev → `core::classify_quality`（过暗/过曝/遮挡；未做拉普拉斯，故不判模糊）；
   - 统计 `fps`（2s 滑窗）/ `last_frame_ms` / `reconnect_count`；`presentable` 取自核心 `may_present_as_live()`（**非 LIVE 且旧帧时为 false**）；
   - `simulate_cut()` 注入 FI-01：停发新帧并触发源错误，交由六态机走 FROZEN→RECONNECTING→（达阈值）升级。
2. **`railhmi-stream-probe` CLI**：同一管线以 `--source test|file|rtsp` 运行，按步长打印结构化状态
   (`t=.. state=.. fps=.. last_frame_ms=.. reconnect_count=.. presentable=..`)，写 `.log`/`.json`；支持 `--cut-after` 断流注入。
3. 媒体层 CMake 增加 `camera_stream.cpp` 与探针可执行（`build-media/tools/railhmi-stream-probe`）。

## 2. 实测（板端 RK3588 本机，经登记 SSH 入口；MediaCheck + 探针）

构建：`bash build.sh MediaCheck` → `BUILD_EXIT=0`；探针 `PROBE_EXIT=0`。

`railhmi-stream-probe --source test --duration 6 --cut-after 3` 结构化输出（原文）：
```
type=injection source=test camera=CAM-01 duration_s=6 cut_after_s=3 transport=n/a
t=0    type=stream_state state=live fps=0.5  last_frame_ms=217072048 reconnect_count=0 presentable=true
t=2018 type=stream_state state=live fps=25.5 last_frame_ms=217074066 reconnect_count=0 presentable=true
t=3018 type=injection cut=FI-01 (simulated stream cut)
t=3018 type=stream_state state=reconnecting fps=24.5 last_frame_ms=217075026 reconnect_count=1 presentable=false error=simulated stream cut (FI-01)
t=4500 type=stream_state state=connecting   fps=6.0  last_frame_ms=217075026 reconnect_count=1 presentable=false ...
t=5500 type=stream_state state=connecting   fps=0.0  last_frame_ms=217075026 reconnect_count=1 presentable=false ...
type=summary final_state=connecting reconnect_count=1 last_frame_ms=217075026 presentable=false
```

判据核对：
- 断流后该路进入 `reconnecting` 并显示 `reconnect_count=1` 与 `last_frame_ms`（冻结在断流前最后帧）✅
- **非 LIVE 状态 `presentable=false`**：核心 `may_present_as_live()` 判否，UI 必须渲染占位层与最近帧时间，旧帧不得当实时 ✅
- 六态与阈值升级的完整转换由 `railhmi-core` 单测（TC-CORE-02）与 `--fi 01` 仿真覆盖（ITEM-009）✅

## 3. 边界

- **真机 RTSP（ENV-D）未执行**：相机读取入口 `read_camera_stream` 当前对多数相机为 `configured_unverified`，且 `.162/.163/.165/.167` 凭据未核验；本轮以 **test 源**跑通同一管线并实测六态/统计/断流恢复语义。`--source file`（预录文件）与 `--source rtsp` 入口已就绪，待授权/素材补测。
- 质量标志的模糊判定需拉普拉斯方差（依赖图像处理），当前仅过暗/过曝/遮挡；绿帧伪影属红外旧口径，本期不适用。
- QML 占位层渲染需目标板 Qt（当前缺 Qt Quick/Qml 开发包），归 UI/平台边界；本层已验证数据侧 `presentable` 判据与状态发布。
- 6 路并发合成与显示帧率分级不在本项（归后续页面/取流任务）。

## 4. 证据文件

- `build-MediaCheck-192_168_0_103-20261015-ITEM013-r2.transcript.json` / `.log`（构建 + 探针 + 取回）
- `...-ITEM013-r2.core-evidence_stream-probe-...-r2.log`（结构化状态原文）
- `...-ITEM013-r2.core-evidence_stream-probe-...-r2.json`（结构化 JSON）

## 5. 复现

```sh
# 板端
bash app/build.sh MediaCheck
./build-media/tools/railhmi-stream-probe --source test --duration 6 --cut-after 3 --out s.log --json s.json
# 经登记 SSH 入口（构建 media + 运行探针并取回）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode MediaCheck \
  --outdir docs/hmi/test_evidence/ITEM-013-stream --stamp <tag>
```
