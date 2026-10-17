# 每路取流 worker 与状态机接入 — 实现交付（VideoSystemImplementation, ITEM-006）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-006「每路取流 worker 与状态机接入」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-03/§5/§6 状态与预算）、`docs/video/VIDEO_TEST_DESIGN.md`（G4/TC-V-STATE-01..03、TC-V-WORKER-*）
- 基线 commit: `e5d412e`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/camera_worker.h` | 新增 | `157844dcae1a0364` | 每相机取流 worker 接口（线程、请求、线程安全快照） |
| `app/src/media/src/camera_worker.cpp` | 新增 | `45e3625900bae54a` | worker 线程实现：有限等待拉帧、drain bus/事件、驱动六态机、快照 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `9f7982efe524a1e7` | `simulate_freeze/resume`、`drain_events`、`presentable_now`、`lease_presentable`、`bus_state_changes`、`quality_flags` |
| `app/src/media/src/camera_stream.cpp` | 修改 | `b94cc8a3ca33a6c3` | drain STATE_CHANGED、冻结休眠不忙等、重建清标志/统计、presentable 代际判定 |
| `app/src/media/tools/camera_worker_probe.cpp` | 新增 | `2a208df398f91475` | 运行入口 `railhmi-camera-worker`（六态/freeze/cut/restart/resume、`--selftest`） |
| `app/tools/board_camera_worker_check.py` | 新增 | `28e052720ba80dbc` | 板端执行入口（MediaCheck + selftest + freeze/cut/resume/rtsp 运行） |

## 2. 契约与行为

1. **每路一个 worker 线程**：`CameraWorker` 持有 `CameraStream` 与专用线程；线程内 `pump(pull_ms)`（`pull_ms` 钳制 ∈ [1,200]）→ `drain_events()`（六态机事件）→ 更新线程安全快照；`CameraStream` 只在 worker 线程被访问。
2. **有限等待拉帧**：单次 `appsink` pull 默认 200ms（`kMaxPullMs=200`）；断流/冻结期间不做 pull 而按有限时长休眠，避免 worker 忙等（r1 实测 9s 内 ~1950 万次迭代 → 修复后 126 次）。
3. **drain GstBus**：`pump()` drain `ERROR | EOS | STATE_CHANGED`；STATE_CHANGED 计数入 `bus_state_changes`；ERROR 触发 `on_source_error`；EOS 对 file 源循环 seek、对 live 源触发源错误。
4. **统计**：`StreamStats` 含 state/fps/`last_frame_ms`（单调）/`capture_wall_ms`（UTC）/reconnect_count/quality_flags/`bus_state_changes`/generation/seq。
5. **六态机接入**：首帧超时、冻结、重连退避、阈值升级由 `tick()` 驱动；事件经 `drain_events()` 由 worker 统计（freezes/reconnect_attempts/escalations/recoveries）。
6. **presentable（渲染前判定）**：`presentable_now()` = `may_present_as_live(now)` **且** 最新租约 `generation == 当前 generation`；`lease_presentable(lease)` 对任意租约做同样判定。非 LIVE 时必为 false（旧帧不当实时）。
7. **真正重建**：`request_restart()` 由 worker 线程 `stop()+start()`；`start()` 重置 `cut/frozen/last_error/frame_times/last_frame_ms/last_quality`，`generation+1`、`seq` 归零、恢复后计数清零。
8. **冻结/解冻**：`request_freeze()` 暂停管线（无源错误，`tick` 判定 FROZEN）；`request_resume()` 恢复 PLAYING，同会话出帧后触发 `Recovered` 且 `reconnect_count=0`。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-camera-worker --selftest
build-media/tools/railhmi-camera-worker --source test --duration 9 \
    --freeze-after 1 --restart-after 6 --freeze-timeout-ms 500 \
    --first-frame-timeout-ms 500 --backoff-ms 200 --reconnect-threshold 3 --out worker.log
build-media/tools/railhmi-camera-worker --source test --duration 6 --cut-after 1 ... 
build-media/tools/railhmi-camera-worker --source rtsp --url 'rtsp://<cam>/…' \
    --user admin --password-env TRAIN_FRONT_RTSP_CREDENTIAL --duration 4
```

板端一键：
```bash
python app/tools/board_camera_worker_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-006 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `--selftest`：**8/8 PASS**（六态 live→frozen→reconnecting→offline(升级)→恢复计数清零；pull 钳制 ≤200/≥1）。
- freeze→restart：`live → frozen(t≈1.8s) → reconnecting(2.5s,count1) → connecting → reconnecting(3.3s,count2) → connecting → offline(4.0s,count3,escalated)`；restart(t=6.0) → `generation 1→2`、`seq` 归零、`live presentable=true reconnect_count=0`；`nonlive_presentable=0`。
- freeze→resume（同会话）：同一路径到 offline 后 `resume(t=6.0)` → `live presentable=true reconnect_count=0 generation=1`；`recoveries=1`；`nonlive_presentable=0`。
- cut(FI-01)：`live → reconnecting(1.3s,count1) → connecting → reconnecting(2.0s,count2) → offline(2.8s,count3,escalated)`；`nonlive_presentable=0`。
- 真实 RTSP CAM-01：`connecting → live`，稳定 ~25–26 fps，`presentable=true`，`bus_state_changes` 累计。
- 证据：`docs/video/test_evidence/ITEM-006/ITEM-006-ENV-C-D-20261017-r1.md` 与同目录原始日志。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1：restart 后不再出帧（generation 2 但 seq=0/offline） | `simulate_freeze/cut` 置位的 `frozen/cut` 未被 `start()` 清除，重建后仍跳过拉帧 | `start()` 重置 `cut/frozen/last_error/frame_times/last_frame_ms/last_quality` |
| r1：9s 内 worker 迭代 ~1950 万次（CPU 空转） | 断流/冻结时跳过 pull 后立即返回，worker 忙等 | `pump()` 在 cut/frozen 时按有限时长休眠（保持 `tick` 节拍） |
| 编辑误删 `if (s)` 守卫 | pump 重构 | 恢复 `if (s)` 守卫 |
| SFTP 上传偶发 60s 超时 | 网络瞬断 | 重跑成功（工具已有限超时，非产品缺陷） |

## 6. 边界与移交

- 本项交付**每相机 worker + 六态机接入 + presentable 判定**；多路并发与单路失败隔离（`MultiStreamManager`/`FrameRegistry`）属 ITEM-007，自动重连控制器属 ITEM-014（本项只提供 `request_restart()` 由控制器调用）。
- `may_present_as_live` 的阈值与事件语义沿用 core `StreamStateMachine`（未改动）。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名；未新增配置字段。
