# 单路取流管线工厂 — 实现交付（VideoSystemImplementation, ITEM-002）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-002「单路取流管线工厂」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`（VideoSystemDesignArtifact）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`docs/video/VIDEO_TEST_DESIGN.md`（G2 / TC-V-PIPE-01..02）
- 基线 commit: `c787b70`（本次修改的文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/pipeline_factory.h` | 修改 | `94f64adfafba7c5f` | 新增 `PipelineSource`/`DecodeMode`/完整 `CameraPipelineSpec`、`build_nv12_prefix`、`split_rtsp_userinfo`、`redact_secret` |
| `app/src/media/src/pipeline_factory.cpp` | 修改 | `f3c96e72dc249df9` | 三类源统一构建；rtsp 强制 UDP + RTP 视频过滤 + 认证属性；auto/显式解码；凭据脱敏 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `2cf4b0744d0df5f0` | `CameraStreamConfig` 增 username/password/timeout_us/decode/test_* 字段 |
| `app/src/media/src/camera_stream.cpp` | 修改 | `fc0e2e273f499a94` | `build_pipeline` 改为调用工厂；错误/上次错误整串脱敏；file 源 EOS 循环 |
| `app/src/media/tools/stream_probe.cpp` | 修改 | `c8049c5a4e7db54d` | 增 `--decode/--user/--password-env/--timeout-us/--selftest` |
| `app/tools/board_pipeline_check.py` | 新增 | `2cd0580da5b1df7e` | 板端执行入口：上传→MediaCheck→selftest→test/file/rtsp（auto+h265）→取证→清理 |

配置来源不变：源地址/协议来自 `app/config/cameras.yaml`（`rtsp_url` 不含凭据、`transport=udp`）；latency/timeout/udp-buffer/解码模式作为工厂显式参数（默认值文档化，可由调用方/CLI 覆盖），未写死设备。

## 2. 行为（与工作项一致）

1. **三类源复用同一构建函数**：`build_pipeline_description(spec)` 统一产出 `源 → 解码 → video/x-raw,format=NV12 → tee → 计数/分析分支`。
   - `test`：`videotestsrc is-live=… ! videoconvert ! video/x-raw,format=NV12,width=…,height=…,framerate=…`。
   - `file`：`filesrc location="…" ! decodebin ! video/x-raw,format=NV12`（源循环见 §2.4）。
   - `rtsp`：`rtspsrc location="…" [! user-id/user-pw] latency=… timeout=… protocols=udp udp-buffer-size=…`。
2. **rtsp 强制 UDP、无 TCP 回退**：固定 `protocols=udp`，无 `protocols=tcp` 分支（selftest 断言）。
3. **认证经属性、不拼接**：`split_rtsp_userinfo` 把 `user:pass@` 拆出，凭据只作 `user-id`/`user-pw` 属性；location 不含用户信息（selftest 断言）。
4. **解码路径可配置**：
   - `Auto`（默认）：`application/x-rtp,media=video ! decodebin`，`decodebin` 自适应 H.264/H.265。
   - `ExplicitH265` / `ExplicitH264`：`rtph265depay ! h265parse ! mppvideodec` / `rtph264depay ! h264parse ! mppvideodec` 固化路径。
5. **只选视频 RTP pad**：rtsp 在 RTP 层加 `application/x-rtp,media=video`，音频/数据 pad 不进入解码链（板端实测：这是 auto 路径能稳定出帧的关键；详见 §4）。
6. **日志整串脱敏**：`CameraStream` 用 `redact_secret` 处理 `gst_parse_launch` 错误与 bus 错误，`last_error`/`start(err)` 不含凭据或 pipeline 原文；CLI pipeline 校验输出用 `redact_secret`。
7. **file 源循环**：文件 EOS 触发 `gst_element_seek_simple(..., FLUSH|KEY_UNIT, 0)` 循环，作为无相机的解码路径源（非实时时钟基准）。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-stream-probe --selftest                         # 纯字符串映射核对
build-media/tools/railhmi-stream-probe --source test  --duration 6
build-media/tools/railhmi-stream-probe --source file  --file /tmp/x.mp4 --duration 6
build-media/tools/railhmi-stream-probe --source rtsp --url 'rtsp://<cam>/…' \
    --user admin --password-env TRAIN_FRONT_RTSP_CREDENTIAL --decode auto --duration 6
build-media/tools/railhmi-stream-probe --source rtsp --url 'rtsp://<cam>/…' \
    --user admin --password-env TRAIN_FRONT_RTSP_CREDENTIAL --decode h265 --duration 6
```

板端一键取证：
```bash
python app/tools/board_pipeline_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-002 --stamp <tag>
```

## 4. 板端验证结果（RK3588，ENV-B/C/D 档）

- 构建 `build.sh MediaCheck` OK；`--selftest` **16/16 PASS**。
- `test` 源：`live`，稳定 ~25.5 fps（默认 25）。
- `file` 源：`live`，循环连续出帧（生成源为板端 ffmpeg H.264 mp4）。
- `rtsp auto`（decodebin）：`live`，稳定 ~25.5 fps。
- `rtsp h265`（显式固化）：`live`，稳定 ~25.0–25.5 fps。
- 证据：`docs/video/test_evidence/ITEM-002/ITEM-002-ENV-B-20261017-r1.md` 与同目录原始日志。

## 5. 调试过程与关键结论（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| `decodebin caps=video/x-raw expose-all-streams=false` 构建后 file/rtsp 均报 “missing a plug-in / Internal data stream error” | `decodebin caps=` 与下游 `video/x-raw,format=NV12` 约束叠加，导致 “no suitable plugins found”；且 `expose-all-streams=false` 不能阻止 decodebin 在类型探测时接触音频流 | 改为在 **RTP 层** 过滤 `application/x-rtp,media=video`，音频 pad 根本不进入 decodebin；auto 与显式路径均加该过滤 |
| 未过滤音频时 rtsp auto 偶发单路失败（与 `VERIFY-ITEM-001` O-1 同源） | 相机带音频（PCMA），decodebin 尝试处理音频 depay | 同上；pipeline 工厂层修复（ITEM-001 探测脚本保持其已验证状态，O-1 为不阻塞观察） |
| file 源 EOS 后进入 reconnecting | 有限文件结束被当作源错误 | file 源 EOS 改为 seek 循环；仍保留 `test`/`rtsp` 的 EOS→源错误语义 |
| 尝试对 file 源设 `sync=true` 做实时节流 | 与循环 seek/leaky 分支交互，实测仅 ~1.5fps（不稳定） | 回退为非阻塞（`sync=false`）；file 源定位为**解码路径源**，不作为实时/容量基准（在证据中标注） |

## 6. 边界与移交

- 本项只实现**单路管线工厂**与运行入口；帧租约/分发（ITEM-003/004）、多路并发（ITEM-006/007）、录像（ITEM-011+）由后续工作项承担。
- `decode`/`latency`/`timeout`/`udp_buffer` 目前为工厂/CLI 参数；若产品需运行期 YAML 配置，建议由配置/core 职责新增可选字段（`cameras.yaml`），本项不改 core schema。
- `VERIFY-ITEM-001` 的 O-1（单路探测瞬时音频路径）在本项工厂层通过 RTP 视频过滤消除其类别；ITEM-001 探测脚本按已验证状态保留。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名。
