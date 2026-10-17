# 保护片段固化与缺口记录 — 实现交付（VideoSystemImplementation, ITEM-012）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-012「保护片段固化与缺口记录」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §7/§8（IF-06/IF-07：ClipRequest、actual_t0/t1、缺口、可用性三态、写盘顺序）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §7.1、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-CLIP-01/02）、`app/config/thresholds.yaml`
- 基线 commit: `86ae2bc`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/clip_request.h` | 新增 | `ab90cac42a1ad0bc` | `ClipRequest`/幂等注册表/`ClipAvailability`/`ClipResult`（纯逻辑） |
| `app/src/core/src/clip_request.cpp` | 新增 | `3ecb3326865f538e` | 窗口计算、幂等 Added/Duplicate/Conflict、可用性三态 |
| `app/src/media/include/railhmi/media/clip_writer.h` | 新增 | `d4249e3ce66b9826` | `ClipWriter`/`ClipWriterConfig`/`ClipPublisher`（公开头无 GStreamer） |
| `app/src/media/src/clip_writer.cpp` | 新增 | `2d1cfb4e0942b5b3` | 直通封装 → 校验 → fsync → rename → fsync 目录 → 发布；缺口/三态 |
| `app/src/media/src/pipeline_factory.cpp` | 修改 | `5b3f3a2875c52809` | 压缩 tap 归一化 `byte-stream,alignment=au`（片段可直通封装） |
| `app/src/media/tools/clip_solidify_probe.cpp` | 新增 | `5f246010261ce487` | 运行入口 `railhmi-clip-solidify`（配置驱动 + `--selftest`） |
| `app/tools/board_clip_solidify_check.py` | 新增 | `a9c456ea3e0771c2` | 板端执行入口（CoreTests + MediaCheck + 固化三态/幂等/失败路径） |
| `app/src/core/CMakeLists.txt` | 修改 | `7e225cfa86ef8a50` | 编译 `clip_request.cpp` |
| `app/src/media/CMakeLists.txt` | 修改 | `7783760d911c7974` | 编译 `clip_writer.cpp` + 注册探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `6e5b4bc3047790a0` / `e6e6bf3476084dd1` | 注册 `clip_request` 测试组 |
| `app/tests/core/test_clip_request.cpp` | 新增 | `4a62aa6b10a69181` | 窗口/幂等/可用性纯逻辑测试 |

## 2. 契约与行为

1. **幂等 `request_id`**：`ClipRequestRegistry::submit` 首次 `added`、同键同参 `duplicate`、同键不同参 `conflict`；冲突不覆盖已登记作业。
2. **请求窗口**：`make_clip_request(request_id,event_id,camera,trigger,pre,post)` → `t0=trigger−pre×1000`、`t1=trigger+post×1000`（默认 −30s/+60s，来自 `thresholds.clip_pre/post_seconds`）。
3. **关键帧对齐**：调用方用 `RecordingTap::first_keyframe_at_or_after(t0)` 选起点后 `collect(start,t1)`；固化回报 `actual_t0/actual_t1` 与请求窗口，**不用期望 90s 冒充实际覆盖**。
4. **真实媒体（非清单/空文件）**：压缩包直通复制（不转码）封装为 MPEG-TS/Matroska；封装后**重新解析校验**（`tsdemux`/`matroskademux` 到 EOS）证明可解码；空/失败一律不产出正式片段。
5. **原子发布顺序**：同目录临时文件（`<final>.part`）→ 封装 → 校验 → `fsync` 文件 → `rename` → `fsync` 目录 → 事务索引发布（`ClipPublisher` 回调，DB 由事件/持久化层写）。发布在 rename 之后；发布失败保留可恢复孤儿 + 日志（不置 published）。
6. **缺口**：任何无媒体/写失败写 `recording_gaps(cam,t0,t1,reason)`（`NO_MEDIA`/`MUX_FAILED`/`MKDIR_FAILED`/`FSYNC_FAILED`/`RENAME_FAILED`；覆盖不足 `PRE_ROLL_SHORTAGE`/`POST_ROLL_SHORTAGE`/`PRE_POST_ROLL_SHORTAGE`）。
7. **可用性三态**：`complete`（两端在容差内覆盖）/`incomplete`（部分覆盖 + 缺口）/`unavailable`（无媒体/失败）；不用 bool `record_available` 冒充。
8. **容差语义**：关键帧对齐使 `actual_t0` 可略大于 `requested_t0`；容差（默认 1000ms）内视为完整、不记缺口，超出才记缺口并降级为 incomplete。
9. **压缩 tap 归一化**：`pipeline_factory` 显式解码的录像分支加 `video/x-h264|h265,stream-format=byte-stream,alignment=au`，使 `record_sink` 输出 Annex-B，`ClipWriter` 可直通封装（`mppvideodec` 同样接受 byte-stream，不影响显示/AI 分支）。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-clip-solidify --selftest
build-media/tools/railhmi-clip-solidify --config config --pre-seconds 3 --post-seconds 2 \
    --warmup 4 --extra 2 --outdir /tmp/railhmi_clips --out clip.log --json clip.json
```

板端一键：
```bash
python app/tools/board_clip_solidify_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-012 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item012-r5`）

- `railhmi-core-tests`：**802 passed / 0 failed**（新增 `clip_request` 组 19 例）。
- `railhmi-clip-solidify --selftest`：**8/8 PASS**（窗口/幂等 Added·Duplicate·Conflict/可用性解析·命名/容器扩展名）。
- `railhmi-recording-tap --selftest`：**9/9 PASS**（回归：`pipeline_factory` tap byte-stream 归一化未破坏 ITEM-011 断言面）。
- 板端真实压缩管线（`videotestsrc → x264enc → h264parse → tee`；`record_sink` byte-stream tap；显示分支慢消费）：
  - `type=clip case=complete requested=[3600000960,3600005960) actual=[3600001000,3600005960] packets=125 keyframe_aligned=true availability=complete size_bytes=1332168 published=true path=.../CAM-01_evt-100-CAM-01.ts reason=`（真实 MPEG-TS，`tsdemux` 校验通过；起点为关键帧）。
  - `type=clip case=incomplete ... availability=incomplete has_gap=true gap_reason=POST_ROLL_SHORTAGE gap=[3600007960,3600035960)`。
  - `type=clip case=unavailable packets=0 availability=unavailable has_gap=true reason=NO_MEDIA`。
  - `type=clip case=mux_failed availability=unavailable has_gap=true reason=MKDIR_FAILED`（不可创建输出目录）。
  - `type=idempotency first=added repeat=duplicate different_params=conflict`。
  - acceptance：`real_media=true coverage_matches=true keyframe_aligned=true gap_recorded=true availability_tristate=true idempotent=true publish_after_rename=true no_stray_temp=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-012/ITEM-012-ENV-A0-C-20261017-r1.md`、同目录原始日志/JSON 与真实片段 `…CAM-01_evt-100-CAM-01.ts`（1332168 B，TS 同步字节 `47 40 00 31`）。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 板端编译失败 `invalid operands const char[40] + const char*` | `emit("..." + to_string(...))` 字面量 + `const char*` | 用 `std::string("...")` 起串（r2 通过） |
| r2 run-clip 超时（无输出） | 压缩 tap 实际为 **AVC（长度前缀）**，`ClipWriter` 按 byte-stream 喂 `h264parse` → 不消费 → `appsrc` 阻塞 | `pipeline_factory`/探针源把 tap 归一化 `byte-stream,alignment=au`；`setvbuf` 无缓冲保留中断观测（r3 通过） |
| r3 acceptance `coverage_matches=false` 且 complete 却带 `PRE_ROLL_SHORTAGE` | 关键帧对齐使 `actual_t0` 略大于 `requested_t0`（40ms），旧逻辑按 >0 记缺口；探针判据要求 `actual_t0<=requested_t0` | 缺口仅当超出容差才记（容差内 complete 无缺口）；探针判据改为容差内覆盖（r4 通过） |
| `pipeline_factory` 为片段封装改了 tap caps | 已交付 ITEM-011 的断言面可能回归 | 运行器加 `railhmi-recording-tap --selftest` 回归步骤；r5 中 9/9 PASS，确认断言面未变 |

## 6. 边界与移交

- 本项交付**媒体侧片段固化与缺口/三态**（真实文件 + 原子发布）；连续录像/NVR 与 provider 查询/回放/导出归 ITEM-013。
- `ClipPublisher` 是事务索引发布边界；实际 SQLite 写入归事件/持久化层（平台），本项只保证「先 rename 后发布」并如实回报 `published`。
- 真实相机 RTSP 片段固化为 ENV-D 现场取证；本项以板端真实压缩管线证明媒体/并发/原子性语义，不冒充真实相机。
- `pipeline_factory` 的 tap 归一化同时服务 ITEM-011 的压缩缓冲（byte-stream 与显示分支兼容）；未改 core/wire 契约、SQLite DDL、生产 UI。
