# 异常恢复控制器 — 实现交付（VideoSystemImplementation, ITEM-014）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-014「异常恢复控制器」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §6（一致性/恢复）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §6/§7.5、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-STATE-01..03）
- 基线 commit: `9f0e784`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/recovery_policy.h` | 新增 | `eb0e5788280ac6fa` | 错误分类 + 退避重试/停止/能力门控策略（纯逻辑） |
| `app/src/core/src/recovery_policy.cpp` | 新增 | `5202a98c210ef246` | `classify_source_error`、`RecoveryPolicy`、`describe_recovery_state` |
| `app/src/media/include/railhmi/media/recovery_controller.h` | 新增 | `e380c7f142266878` | `RecoveryController`/状态接口（公开头无 GStreamer） |
| `app/src/media/src/recovery_controller.cpp` | 新增 | `39df7de327017109` | 轮询错误/退避重建/真帧确认/ONVIF 钩子 |
| `app/src/media/tools/recovery_probe.cpp` | 新增 | `60047a5b7d1242a7` | 运行入口 `railhmi-recovery`（配置驱动 + `--selftest`） |
| `app/tools/board_recovery_check.py` | 新增 | `62fc6fa0cd35b4d3` | 板端执行入口 |
| `app/src/core/CMakeLists.txt` | 修改 | `0cf64be1f7b796c5` | 编译 `recovery_policy.cpp` |
| `app/src/media/CMakeLists.txt` | 修改 | `79e58d679608cf53` | 编译 `recovery_controller.cpp` + 注册探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `7444678107bda183` / `467f7884a6c07f9a` | 注册 `recovery_policy` 测试组 |
| `app/tests/core/test_recovery_policy.cpp` | 新增 | `2cfc85dea557147f` | 分类/退避/鉴权/能力门控/上限/恢复纯逻辑测试 |

## 2. 契约与行为

1. **错误分类**：`classify_source_error(domain,message)` → `AUTH_FAILED`/`CREDENTIAL_MISSING`/`UNSUPPORTED`/`FORMAT`/`NETWORK`/`TIMEOUT`/`OTHER`/`NONE`（大小写不敏感关键字：401/unauthorized/authentication、credential、unsupported/no such element、not-negotiated/format/caps、timeout、connection/cut/eos/reset…）。
2. **退避重试**：网络/超时/其他 → 指数退避（`base_backoff_ms` ×2^（失败链−1），上限 `max_backoff_ms`）；**每次实际重建尝试只计一次**（`total_attempts`），失败链 `failure_chain` 每次错误递增并保留峰值 `max_failure_chain`；超过 `max_attempts` → `StopMaxAttempts`。
3. **鉴权不重试**：`AUTH_FAILED`/`CREDENTIAL_MISSING` → `StopAuth`，`stopped=true`、`total_attempts=0`，即使退避到期也不重建。
4. **能力门控**：`UNSUPPORTED`/`FORMAT` → `WaitCapabilityChange`；仅当输入或模型能力变化（`notify_capability_changed`）后才允许一次重建。
5. **真实重建**：退避到期 → `CameraWorker::request_restart()`（worker 线程执行 `stream->stop()`（置 NULL/释放）→ `stream->start()`（重建），generation+1、seq 归零）。
6. **真实新帧确认**：恢复仅在 `generation > 重建前 generation && last_seq>0 && presentable` 时确认，**不采信 PLAYING 返回值**；记录 `recovered_generation` 与 `recovery_latency_ms`。
7. **单路隔离**：控制器只作用于其绑定的 worker；重建不影响其他路（由 multi-stream 架构保证）。
8. **ONVIF 看门狗**：受控可选，**默认关闭**；启用但未配置地址 → `unavailable:UNCONFIGURED`，不猜通道、不动作；已配置但协议未登记 → `unsupported:no_protocol`。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-recovery --selftest
build-media/tools/railhmi-recovery --config config --duration 8 --out rec.log --json rec.json
```

板端一键：
```bash
python app/tools/board_recovery_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-014 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item014-r2`）

- `railhmi-core-tests`：**857 passed / 0 failed**（新增 `recovery_policy` 组）。
- `railhmi-recovery --selftest`：**10/10 PASS**（分类五项 + 退避/重建一次/鉴权停止/能力等待/能力变化后重建）。
- 三路并发（test 源，`reconnect_backoff_ms=1000`）：
  - **CAM-01 断流→退避→重建→真帧确认**：`kind=NETWORK attempts=1 recovered=true generation 1→2 recovery_latency_ms=201`；`t_cut=50ms`、`rebuild=1278ms` → 退避 ≥ 1000ms 满足。
  - **CAM-02 鉴权失败不重试**：`kind=AUTH_FAILED stopped=true attempts=0 generation 1→1`（仍在出帧）。
  - **CAM-03 不支持能力门控**：`kind=UNSUPPORTED`，能力变化前不重建；`change=2500ms`、`rebuild=2532ms` → `attempts=1 recovered generation 1→2`。
  - **单路隔离**：CAM-01 重建期间 CAM-02 generation 保持 1。
  - **ONVIF**：默认 `disabled`；启用未配置 → `unavailable:UNCONFIGURED`，`onvif_tick=false`。
  - acceptance：`rebuild_confirmed_by_new_frame=true attempts_counted_once=true backoff_respected=true auth_no_retry=true capability_gated=true single_channel_isolation=true onvif_default_off=true onvif_unconfigured=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-014/ITEM-014-ENV-A0-C-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 core 1 项失败 `classify unsupported` | 错误文本 "No such element" 不含子串 "no element" | 分类增加 `no such element` 关键字（r2 通过） |
| r1 media 编译失败 `RecoveryState has no member describe` | `RecoveryControllerStatus` 持有 `RecoveryState`（无 `describe`） | 新增 `core::describe_recovery_state(const RecoveryState&)`，控制器改用它（r2 通过） |

## 6. 边界与移交

- 本项交付**运行时异常恢复控制器**（分类/退避/真实重建/真帧确认/能力门控/ONVIF 钩子）；应用启动自恢复序列归桌面 ITEM-024（`core/recovery.h`，不同职责）。
- ONVIF 看门狗仅提供受控开关与显式状态；ONVIF 协议/鉴权未登记，不实现、不猜通道（待环境登记与授权）。
- 真实相机断流（ENV-D 拔线/停流）与鉴权失败需现场取证；本项以板端 test 源 + 显式错误注入证明控制器语义，不冒充真实相机。
- 未改 core/wire 契约、SQLite DDL、`CameraWorker` 既有重建入口签名、生产 UI。
