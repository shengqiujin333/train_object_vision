# AI 帧派发边界 — 实现交付（VideoSystemImplementation, ITEM-010）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-010「AI 帧派发边界」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（FrameDescriptor）、`docs/hmi/DESKTOP_APP_DESIGN.md` §8.2（推理调度）、`app/config/{cameras,app}.yaml`
- 基线 commit: `ca3a9d0`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/ai_dispatcher.h` | 新增 | `b2c91b3734b0c6e7` | `AiFrame`/`AiCameraSpec`/`AiFrameRouter`（媒体侧 AI 派发边界） |
| `app/src/media/src/ai_dispatcher.cpp` | 新增 | `fbf09f48b5c39c2e` | 路由实现：ai_enabled/ai_profile 过滤、降频、有界丢最旧、计数 |
| `app/src/media/tools/ai_dispatch_probe.cpp` | 新增 | `74e52187d9d5d5c2` | 运行入口 `railhmi-ai-dispatch`（配置驱动 + `--selftest`） |
| `app/tools/board_ai_dispatch_check.py` | 新增 | `0a384ddc47f6b6e4` | 板端执行入口 |
| `app/src/media/CMakeLists.txt` | 修改 | `cdad9c6b0db95543` | 编译 `ai_dispatcher.cpp`、注册 `railhmi-ai-dispatch` |

## 2. 契约与行为

1. **选择（ai_enabled/ai_profile）**：`AiFrameRouter` 构造时按 `cameras[].ai_enabled` 与 `cameras[].ai_profile` 生成每相机通道；仅 `ai_enabled==true && ai_profile!="none"` 建队。左右两侧（CAM-02/03，`ai_profile=none`）**永不入队**，其每帧只累加 `rejected_disabled`。
2. **降频（app.ai.inference_interval）**：复用 core 既有约定 `core::frame_due(seq, interval)`（`seq % interval == 0`），与推理侧同一 interval 口径；未命中的帧累加 `skipped_interval`。
3. **关联键**：派发的 `AiFrame` 携带 `session_id / camera_id / generation / seq / capture_mono_us / capture_wall_ms / profile`，**可用于结果回联**（同一 `(session_id,camera_id,generation,seq)` 唯一定位取流帧）。
4. **帧租约**：`AiFrame` 持有 `FrameLease`（不可变、共享所有权，ITEM-004），像素所有权仍在 media 侧；推理适配器消费完毕自动释放，无逐帧无界分配。
5. **有界丢最旧**：每相机独立 `core::BoundedQueue<AiFrame>`（容量 `app.ai.queue_depth`，ITEM-008）；满时丢最旧并累加 `dropped`，队列深度恒 `<= queue_depth`。
6. **仅媒体侧派发**：本项不实现模型推理（无 RKNN/Detector）；向推理侧只提供消费面 `pop(camera_id,&frame)` 与逐路计数。
7. **配置驱动**：探针默认从 `--config <dir>`（`app/config`）读取 `cameras.yaml`/`app.yaml`，不写死设备；配置不可用时明确告警并回退文档化默认。
8. **并发边界**：每相机 worker 线程调用 `on_frame`；通道计数以互斥量保护，计数快照线程安全。

### 与既有推理调度（core ITEM-017 / media `AiWorker`）的关系
- `core::inference`/`core::BoundedFrameQueue`/`media::AiWorker` 是推理调度与模型消费侧（桌面职责，消费精简 `FrameRef`）。
- `AiFrameRouter` 是**媒体侧派发边界**：在媒体层完成 `ai_profile` 过滤、`inference_interval` 降频与 `(session,generation,seq)` 身份绑定后，把持有租约的 `AiFrame` 交给推理适配器。两者通过 `AiFrame`→`FrameRef` 适配衔接，本项不改动 core inference 与 `AiWorker`。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-ai-dispatch --selftest
build-media/tools/railhmi-ai-dispatch --source test --config config \
    --duration 6 --consume-hz 2 --out ai.log --json ai.json
```

板端一键：
```bash
python app/tools/board_ai_dispatch_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-010 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item010-r2`）

- `railhmi-ai-dispatch --selftest`：**4/4 PASS**（`interval_gating`、`interval_always_when_le1`、`enabled_filtering`、`profile_none_disabled`）。
- 六路 test 源（`--config config`，`inference_interval=5`、`queue_depth=2`、`consume_hz=2`）：
  - `type=injection channels=6 started=6 inference_interval=5 queue_depth=2`（间隔/深度取自配置）；
  - 启用路 CAM-01/04/05/06：`published=163 enqueued=32 dropped=18 skipped=131 rejected=0 depth=2`——`enqueued=floor(163/5)=32` 精确命中降频；慢消费者（2Hz < 5Hz）导致 `dropped=18` 且 `depth<=2`；
  - 两侧 CAM-02/03：`enqueued=0 rejected=163 depth=0`（**永不入队**，仅被拒计数）；
  - `type=popped camera=CAM-06 session=s1a1496f4fdc-36c4a9-5 generation=1 seq=145 profile=cab_door`（身份关联键可用于回联）；
  - acceptance：`enabled_only_dispatch=true disabled_never_enqueued=true interval_applied=true bounded=true overflow_counted=true identity_present=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-010/ITEM-010-ENV-C-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 板端编译失败：`'FrameLease' does not name a type` | 探针 `main` 只 `using namespace railhmi;`，lambda 形参未限定 `media` 命名空间 | 形参改为 `const media::FrameLease&`（r2 通过） |
| 首版自定义 `ai_should_dispatch(idx,interval)` | 会与 core 既有 `frame_due(seq,interval)` 形成第二套 interval 口径 | 删除重复函数，`on_frame` 直接复用 `core::frame_due(d.seq,interval)`，判据与推理侧一致 |

## 6. 边界与移交

- 本项交付**媒体侧 AI 派发接口**（选择/降频/关联键/有界丢旧帧/计数）；**模型推理、后处理与结果落库**属视觉/模型与桌面职责，本项不实现。
- 派发消费面 `pop()` 与逐路计数供推理适配器（core `AiWorker`/Detector）接线；`AiFrame` 已含身份关联键与租约，适配时无需再查取流层。
- 未改 core/wire 契约、SQLite DDL、`AiWorker`/`Detector` 签名、生产 UI。
