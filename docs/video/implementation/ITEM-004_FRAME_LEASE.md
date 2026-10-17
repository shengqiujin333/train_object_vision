# 不可变帧租约与缓冲区所有权 — 实现交付（VideoSystemImplementation, ITEM-004）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-004「不可变帧租约与缓冲区所有权」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（IF-02/§5 帧描述符与租约）、`docs/interfaces/interface.schema.json`（FrameDescriptor）、`docs/video/VIDEO_TEST_DESIGN.md`（G2/TC-V-LEASE-01..02）
- 基线 commit: `ed95d4e`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/frame_layout.h` | 新增 | `c7655a68ae86b7d7` | 纯逻辑：`PixelFormat`/`PlaneLayout`/`FrameLayout`/`LayoutStatus`；NV12/packed 布局推导与校验 |
| `app/src/core/src/frame_layout.cpp` | 新增 | `8bb5ed11e82b6822` | 布局推导与显式拒绝实现 |
| `app/src/media/include/railhmi/media/frame_lease.h` | 新增 | `1566d3ed71f67672` | `FrameDescriptor`（对齐契约 schema）+ `FrameLease`（共享所有权，GStreamer-free 公开头） |
| `app/src/media/include/railhmi/media/frame_store.h` | 新增 | `6c5a5ddc3ae2ff92` | 每相机最新帧仓库 `FrameStore`（publish/latest/release 回调；GstSample 前向声明） |
| `app/src/media/src/frame_lease.cpp` | 新增 | `51d5f4773909be97` | GStreamer-backed `FrameBuffer`（ref 共享 + 映射）与 `FrameStore` 实现 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `17a333561368dd2e` | 新增 `latest_frame()` / `frame_store()` |
| `app/src/media/src/camera_stream.cpp` | 修改 | `ef2b820ff130e96d` | 每帧发布到 FrameStore（origin=live/file/simulation） |
| `app/src/media/tools/frame_lease_probe.cpp` | 新增 | `71b834ef006cba1a` | 运行入口 `railhmi-frame-lease`（NOT_READY/descriptor/多消费者/release 回调/`--selftest`） |
| `app/tests/core/test_frame_layout.cpp` | 新增 | `c8e2386232ca3711` | core 单测：布局推导与显式拒绝 |
| `app/tools/board_frame_lease_check.py` | 新增 | `892b20706baee8b5` | 板端执行入口（CoreTests + 租约 selftest + test/rtsp 运行） |

## 2. 契约与行为

1. **`FrameDescriptor`（对齐 interface.schema.json）**：`origin/session_id/camera_id/generation/seq/capture_mono_us/capture_wall_ms/pts_ns/width/height/format/colorimetry/planes[{offset,stride_bytes,size_bytes}]/buffer_bytes/buffer_ref/memory_kind`。`memory_kind` 固定 `cpu`；`buffer_ref` 为进程内不透明查找键（`fr-<cam>-<n>`），不可跨进程解引用。
2. **`FrameLease` 共享所有权**：底层 `FrameBuffer` 持有 `GstSample`（`gst_sample_ref`）与映射后的 `GstVideoFrame`；租约可拷贝共享给多个消费者，`use_count` 可观测；**最后一个** shared_ptr 销毁时 `gst_video_frame_unmap` + `gst_sample_unref`，并触发一次 release 回调。
3. **像素所有权留在 media**：core/渲染层只能拿 `FrameDescriptor`（元数据）；像素仅经 `FrameLease::plane_data(i)` 暴露，且仅在租约存活期有效；**绝不**传已 unmap 的裸地址。
4. **平面/stride 计算与校验**（`core::frame_layout`）：由 caps 的 `GstVideoInfo` 推导；仅支持 `NV12`（两平面，偶数宽高，stride≥width，Y≥stride×h，UV≥stride×(h/2)，平面不重叠）与 `BGR888/RGB888`（单平面 stride≥width×3）；`offset+size≤buffer_bytes`；否则返回具体 `LayoutStatus`（`odd_dimensions/stride_too_small/plane_too_small/overlapping_planes/exceeds_buffer/unsupported_format/...`）并拒绝发布（不替换当前帧）。
5. **NOT_READY**：`latest_frame()` 在无帧时返回空租约，`status()=="NOT_READY"`、`plane_count()==0`。
6. **origin**：`rtsp→live`、`file→file`、`test→simulation`（显式选择，不猜测）。
7. **替换语义**：新帧发布时旧帧由 store 释放；若仍有消费者持有该旧帧的租约，则它保持有效直到最后一个消费者退出（release 回调随后触发）。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests     # 含 frame_layout 组
app/build.sh MediaCheck
build-media/tools/railhmi-frame-lease --selftest
build-media/tools/railhmi-frame-lease --source test --duration 3 --consumers 3 --out lease.log
build-media/tools/railhmi-frame-lease --source rtsp --url 'rtsp://<cam>/…' --user admin \
    --password-env TRAIN_FRONT_RTSP_CREDENTIAL --duration 3 --consumers 2
```

板端一键：
```bash
python app/tools/board_frame_lease_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-004 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `railhmi-core-tests`：**726 passed, 0 failed**（含 frame_layout 组：NV12 连续/padding、奇数宽高/stride 不足/UV 重叠/零宽拒绝、BGR888/RGB888 packed、越界/多平面/重叠校验）。
- `railhmi-frame-lease --selftest`：**6/6 PASS**（`nv12_padded_ok/nv12_odd_rejected/nv12_stride_rejected/nv12_overlap_rejected/bgr888_ok/unsupported_format_rejected`）。
- `--source test`（simulation）与 `--source rtsp`（live，真实相机 CAM-01）：
  - `lease_pre not_ready=true status=NOT_READY`；
  - descriptor：`format=NV12 size=1280x720 planes=2 buffer_bytes=1382400 plane0_stride=1280 plane1_offset=921600 plane_ok=true`；
  - 多消费者并发读取：3 消费者 4413 次 / 2 消费者 2929 次，`max_use_count` 2/4，无崩溃；
  - 共享租约 `use_count=3 released_while_shared=0`；释放副本 `release_after_copies=0`；释放最后一个消费者 `release_after_last=1`（回调恰好一次）；
  - `published` 77/78，`rejected=0`。
- 证据：`docs/video/test_evidence/ITEM-004/ITEM-004-ENV-A0-B-20261017-r1.md` 与同目录原始日志。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| `release_after_last=0`（首次 r1） | 演示时 store 仍持有「最新帧」，释放消费者租约不会释放它 | 改为：先持有帧 A，继续泵到 A 被新帧 B 替换（store 不再持有 A），再释放消费者；r2 得到 `release_after_last=1` |
| `GstVideoFrame` 结构体拷贝风险 | 本地 map 后拷贝进 FrameBuffer，语义不清晰 | 改为直接 `gst_video_frame_map(&fb->vf_…)`，先 ref sample 保证映射期存活 |

## 6. 边界与移交

- 本项交付**租约/所有权与描述符**；`latest_frame(camera_id)` 注册表式聚合属 ITEM-007（多路），本项为每路 `CameraStream` 提供 `latest_frame()`/`frame_store()`。
- 格式转换（NV12→BGR）属 ITEM-005；显示/推理/录像消费者（ITEM-008/009/010/011）在此租约之上消费 `FrameDescriptor` 与 `plane_data`。
- `colorimetry` 仅按 caps 字符串归一为 `bt601_* / bt709_* / unknown`；本轮 test 源与 mppvideodec 解码 NV12 caps 未带该字段，诚实报告 `unknown`（设计允许）。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名；未新增配置字段。
