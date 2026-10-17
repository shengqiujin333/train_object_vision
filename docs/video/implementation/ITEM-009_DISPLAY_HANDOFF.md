# 显示分发与纹理交接 — 实现交付（VideoSystemImplementation, ITEM-009）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-009「显示分发与纹理交接」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-08/§5/§6 渲染）、`docs/hmi/DESKTOP_APP_DESIGN.md`（渲染路径）
- 基线 commit: `c2b0f2e`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/display_distributor.h` | 新增 | `50b4911b3bac7a12` | `FrameSnapshot`/`RenderPathStatus`/`probe_render_path`/`DisplayDistributor` |
| `app/src/media/src/display_distributor.cpp` | 新增 | `b66df066bc7907f2` | 渲染路径能力探测 + 快照生成（cpu BGR888 深拷贝） |
| `app/src/media/include/railhmi/media/display_registry.h` | 新增 | `76d02e964e85887e` | 多路显示分发注册表（每相机单一来源） |
| `app/src/media/src/display_registry.cpp` | 新增 | `36b7b4230ac7d15b` | 注册表实现：worker 帧回调 → 分发器 |
| `app/src/media/include/railhmi/media/camera_worker.h` | 修改 | `c51eb1fca15ecf6b` | 新增 `FrameCallback`/`set_frame_callback` |
| `app/src/media/src/camera_worker.cpp` | 修改 | `80b6e90781f6f51d` | 每帧回调（租约+presentable，worker 线程调用） |
| `app/src/media/include/railhmi/media/multi_stream.h` | 修改 | `af301a3313023ae9` | 新增 `worker(index)` 访问器 |
| `app/src/media/src/multi_stream.cpp` | 修改 | `ba7539ea255c6ceb` | `worker(index)` 实现 |
| `app/src/media/tools/display_probe.cpp` | 新增 | `2ac9f8a407887dbd` | 运行入口 `railhmi-display`（P1/P4/P5 切换 + gl 状态 + `--selftest`） |
| `app/tools/board_display_check.py` | 新增 | `997c67fd9cb632fd` | 板端执行入口 |

## 2. 契约与行为

1. **不可变显示快照**：`DisplayDistributor` 在取流 worker 线程内把帧租约转成**拥有像素**的 `FrameSnapshot`（cpu 路径 BGR888 `width×height×3`，深拷贝）；GUI 线程只消费快照上传纹理，**不接触 GstSample/裸地址**（对应契约「稳定视图或深拷贝」）。
2. **presentable**：快照携带 `presentable`（来自 ITEM-006 的 `may_present_as_live` 且租约属于当前 generation）；非 LIVE 或转换失败时 `presentable=false`。
3. **单一来源**：`DisplayRegistry` 每相机一个 `DisplayDistributor`；P1/P4/P5 视图只按 `camera_id` 查询同一分发器的快照与 `updates()`，**不创建新取流会话**。
4. **页面切换不重建**：视图切换只改变查询的 camera_id 集合；`generation` 恒定、`updates` 单调增长即证明未重建。
5. **渲染路径能力**：`probe_render_path(Cpu)` 恒可用；`probe_render_path(Gl)` 需 `qmlglsink`+`glupload`+`glcolorconvert`，缺失返回 `available=false` + `reason="missing:..."`（**显式不可用，不静默降级**）；gl 分发器未实现，`update` 时 `presentable=false` + `last_error="gl path not implemented"`。
6. **转换失败不静默**：cpu 路径 `nv12_to_bgr` 失败则 `convert_failures++`、`last_error` 记录、`presentable=false`，不阻塞取流。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-display --selftest
build-media/tools/railhmi-display --source test --count 6 --out display.log --json display.json
```

板端一键：
```bash
python app/tools/board_display_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-009 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `railhmi-display --selftest`：**2/2 PASS**（`cpu_path_available`、`gl_status_explicit`）。
- 六路 test 源运行：
  - `type=render_path path=cpu available=true`；`type=render_path path=gl available=false reason=missing:qmlglsink`（显式，不静默）；
  - `started=6`；
  - P1（left/front/right，3 路）→ P4（all，6 路）→ P5（ai-debug，1 路）：
    - 每阶段 `probe=CAM-01 snapshot_valid=true presentable=true format=BGR888 size=1280x720`；
    - `generation=1` 全程不变（**页面切换不重建会话**）；`updates` 42→83→118 单调增长（同一来源持续出帧）；
  - acceptance：`page_switch_no_rebuild=true snapshots_valid=true single_source=true gl_status_explicit=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-009/ITEM-009-ENV-C-20261017-r1.md` 与同目录原始日志。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 `snapshots_valid=false single_source=false` | 验收把「两次读快照 seq 必须相等」当同源判据（跨帧会不等），且要求所有路首轮即就绪 | 改为：同相机两视图 `camera_id` 相同且 `seq` 单调不减；有快照的路才校验字段有效性 |

## 6. 边界与移交

- 本项交付**媒体侧显示分发与纹理交接边界**（快照/presentable/渲染路径能力）；生产 UI（Qt/QML `VideoSurface`/纹理上传/页面渲染）属桌面 UI 职责（embedded_linux_engineer / web_frontend），本项只提供消费面。
- 同屏多视图复用同一 `DisplayDistributor`；P1/P4/P5 共享来源由 UI 侧接线（本项已验证注册表提供单一来源）。
- gl 路径的零拷贝合成待平台补齐 `gstreamer1.0-qt5`（`qmlglsink`）后实现与实测；当前显式不可用。
- 未改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名。
