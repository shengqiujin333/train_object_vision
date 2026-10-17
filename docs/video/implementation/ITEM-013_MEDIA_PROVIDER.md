# 录像 provider 与回放媒体链路 — 实现交付（VideoSystemImplementation, ITEM-013）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-013「录像 provider 与回放媒体链路」
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` §7/§8（IF-07：IRecordingProvider 方法族、status/reason、actual decoded position、NVR unconfigured）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §7.2、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-PLY-01/02）
- 基线 commit: `f9c95fb`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/recording_provider.h` | 新增 | `4d264b32462f9088` | provider 状态/原因/请求/覆盖分类/回放·导出结果（纯逻辑） |
| `app/src/core/src/recording_provider.cpp` | 新增 | `ed8b8c4d122e9904` | `classify_segments`、覆盖并集、deadline 判定 |
| `app/src/media/include/railhmi/media/media_provider.h` | 新增 | `b8fc33faf9001834` | `LocalMediaProvider`/`NvrProvider` 接口（公开头无 GStreamer） |
| `app/src/media/src/media_provider.cpp` | 新增 | `9780963353ea981a` | 可解码判定、真实解码回放（实际 position）、导出复制、缩略图、NVR |
| `app/src/media/tools/media_provider_probe.cpp` | 新增 | `54a1c1c1b0b5cf7d` | 运行入口 `railhmi-media-provider`（配置驱动 + `--selftest`） |
| `app/tools/board_media_provider_check.py` | 新增 | `a5dbc79a714e67cd` | 板端执行入口 |
| `app/src/core/CMakeLists.txt` | 修改 | `88eff7d93361cadd` | 编译 `recording_provider.cpp` |
| `app/src/media/CMakeLists.txt` | 修改 | `66f3de74754ccc11` | 编译 `media_provider.cpp` + 注册探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `5db6d0f055f40e05` / `a54684a5e8906091` | 注册 `recording_provider` 测试组 |
| `app/tests/core/test_recording_provider.cpp` | 新增 | `e8ff5d1ccf10f539` | 状态/覆盖/deadline/过滤纯逻辑测试 |

## 2. 契约与行为

1. **方法族**：`querySegments`/`queryGaps`/`playSegment`/`exportSegment`，每次带 `request_id` 与 `deadline_mono_us`，返回 `status=ok|partial|unavailable|error` + 稳定 `reason`。
2. **覆盖分类**：`classify_segments` 以**真实可用片段**的实际区间并集计算覆盖：
   - 未配置 → `unavailable/UNCONFIGURED`（不生成模拟片段）；
   - 无重叠片段 → `ok` 空列表（**查询成功无覆盖≠可播放**）；
   - 有重叠但均不可用（文件缺失/不可解码）→ `unavailable/NOT_FOUND`；
   - 可用覆盖 ≥ 窗口 → `ok`（`coverage_complete`）；
   - 部分覆盖 → `partial`。
3. **真实媒体判定**：`file_usable` 需 `filesrc → decodebin → fakesink` 到达 EOS（存在/非空/可解码），不以路径存在或估算大小代替。
4. **实际解码 position**：`play_segment` 等预滚（ASYNC_DONE）后 seek，回报解码到的**实际 PTS**（`actual_pos_ms`）；请求位置与请求接受不等于播放成功（`reached` 由真实帧确认）。
5. **导出**：真实复制到 `export_dir/<basename>`（临时文件 → fsync → rename），返回实际路径/大小；失败 → `error/STORAGE_ERROR`，源缺失 → `unavailable/NOT_FOUND`。
6. **缩略图**：`decodebin → videoconvert → jpegenc → multifilesink(max-files=1)` 由媒体生成 JPEG，仅返回真实存在且非空的可读引用。
7. **NVR 未配置**：`NvrProvider` 全部返回 `unavailable/UNCONFIGURED`，不探测未知协议、不猜通道号、不生成模拟片段。
8. **同步（TC-V-PLY-02）**：以各路**实际解码播放头**驱动 `core::sync_status` 求 `max−min`；超阈值（默认 300ms）显式提示；`in_sync == (max_deviation<=threshold)` 一致。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-media-provider --selftest
build-media/tools/railhmi-media-provider --config config --pre-seconds 3 --post-seconds 2 \
    --warmup 4 --extra 2 --outdir /tmp/railhmi_prov --out prov.log --json prov.json
```

板端一键：
```bash
python app/tools/board_media_provider_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-013 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item013-r2`）

- `railhmi-core-tests`：**822 passed / 0 failed**（新增 `recording_provider` 组）。
- `railhmi-media-provider --selftest`：**8/8 PASS**（empty-ok/unconfigured/complete/partial/unusable/deadline/status）。
- 板端真实压缩管线生成真实 MPEG-TS 片段，provider 行为：
  - query：complete → `ok coverage_complete=true covered_ms=4960`；empty → `ok segments=0`（不称可播放）；partial → `partial covered_ms=1000`；gaps → `ok gaps=1 reason=SYNTH_GAP`；deadline 过期 → `error/TIMEOUT`。
  - play：`requested=…1400 (rel 400) actual=…1645 reached=true frames=14`；`requested=rel 4500 actual=…3645`——回报**实际解码 position**，非请求接受；落在 `seek_tolerance_ms` 内。
  - export：真实复制 `size_bytes=1328972`；源缺失 → `unavailable/NOT_FOUND`。
  - thumbnail：`thumbs/cam01_00000.jpg`（JPEG `ff d8 ff e0`，60861 B）媒体生成可读引用。
  - NVR：query/play/export 全 `unavailable/UNCONFIGURED`，`segments=0`（不模拟填补）。
  - sync：close `max_deviation=0 in_sync=true`；spread `max_deviation=2000 in_sync=false prompt=同步偏差 2000 ms（阈值 300 ms）`。
  - acceptance：全部 true，`result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-013/ITEM-013-ENV-A0-C-20261017-r1.md`、同目录原始日志/JSON/导出片段/缩略图。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| r1 core 1 项失败 `camera ignored` | `covered_ms_in_window` 不接收相机参数，测试误期望按相机过滤 | 相机过滤改由 `classify_segments` 测试覆盖；该判据改验证「覆盖只看可用性」（r2 通过） |
| r1 play 全部返回同一 position、sync 偏差恒 0 | `gst_element_seek_simple` 在预滚前调用被忽略，始终从 0 解码 | 先等 `ASYNC_DONE`（预滚完成）再 seek；r2 中 rel 4500 得到不同实际 position，spread 偏差 2000ms 触发提示 |

## 6. 边界与移交

- 本项交付**媒体侧 provider 与回放链路**；四跳转/时间轴/事件详情 UI 模型归桌面 ITEM-020（`core/replay.h` 已提供，本项复用其同步计算）。
- SQLite 片段/缺口索引读写归事件/持久化层（平台）；本项消费 `RecordingSegmentRow`/`RecordingGapRow` 并验证真实媒体。
- 真实相机 RTSP 片段回放为 ENV-D 现场取证；本项以板端真实压缩管线 + 真实 MPEG-TS 证明 provider 语义与解码实际 position，不冒充真实相机。
- 未改 core/wire 契约、SQLite DDL、生产 UI。
