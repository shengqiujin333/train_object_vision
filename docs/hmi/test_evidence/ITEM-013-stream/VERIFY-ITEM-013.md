# ITEM-013 软件验证 TestEvidence — 单路取流与状态机接入（VERIFY-20261016-v6 判定）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-013`
- Capability: `software_tester.software_verification`
- Outcome: **TEST_PASS**
- 被测版本: commit `d96b0d2`（manifest 中 ITEM-013 交付文件：`app/src/media/include/railhmi/media/camera_stream.h`、`app/src/media/src/camera_stream.cpp`、`app/src/media/tools/stream_probe.cpp`、`app/src/media/CMakeLists.txt`）
- 上游实现/构建证据: `docs/hmi/test_evidence/ITEM-013-stream/README.md`（BuildEvidence, candidate→本轮核验）、板端归档 `build-MediaCheck-192_168_0_103-20261015-ITEM013-r2.*`
- 环境: ENV-C 板端本机（RK3588 Ubuntu 22.04 / GStreamer 1.20.3 / librga）；经登记 Paramiko SSH 入口（environment.yaml → access.rk3588_verify/configured_unverified → 本轮实测在线）；**真机 RTSP（ENV-D）未执行**，见 §5 边界。

## 1. 独立复现范围（不是转述实现者证据）

全部判据由验证自有运行器 `verify_stream13_fixtures.py` 在板上经独立上传的 app/ 快照执行：

1. **交付入口独立构建**：`bash build.sh MediaCheck` → `BUILD_EXIT=0`（v6 transcript [build-media]），由实现声明的构建脚本自己产出 `build-media/src/media/librailhmi_media.a` 与 `build-media/tools/railhmi-stream-probe`；产物存在性 `test -x` 实测（[probe-bin] exit=0）。
2. **实现者权威探针轮逐字段复现**：`railhmi-stream-probe --source test --duration 6 --cut-after 3`（与板端归档 r2 完全相同的管线、注入参数与时序），v6 复现 t=0..5500 序列（见 §2）；`type=injection` 头与实现者归档 r2 头一致（[probe-log-compare] impl_header=1 verify_header=1 identical=True）。
3. **断流语义程序化断言**（[assert-live-before-cut] / [assert-cut-effect] / [assert-no-revival] / [assert-recount-monotonic]，全部 exit=0）：
   - cut 前 6 个采样节点全部 `state=live presentable=true reconnect_count=0`；
   - cut 后首节点 `reconnecting reconnect_count=1 presentable=false`（无中间"仍 live"节点）；
   - cut 节点携带非零 `last_frame_ms`（218079606）且此后所有节点冻结不变（frame_frozen_onward=True）——旧帧时间保留展示、但 `presentable=false` 禁止当实时；
   - 整个观测窗内无任何节点回到 `live`（断开后不复活，恢复语义由六态机退避驱动，未出帧）；
   - `reconnect_count` 全序列单调不减。
4. **0 秒断流空态轮**（[run-probe-cutonly] / [assert-cutonly]，exit=0）：`--cut-after 0` 首个节点即 `reconnecting`（t=0、`last_frame_ms=0`、presentable=false），最终 `connecting`（退避后），全程无 live——**无相机/无首帧也同管线可用**（工作项"无相机时可用"判据的探针档）。
5. **验证自有扩展夹具 `verify_stream13_ext.cpp`**（链接交付自建 `librailhmi_media.a + librailhmi_core.a + librailhmi_platform.a`，[compile-ext] GXX_EXIT=0）：EXT_EXIT=0，`RESULT: PASS passed=26 failed=0`。超出实现者用例的断言：
   - TC-V13-01：test 源从真实 appsink 帧驱动六态机进入 LIVE（`wait_state` 真实帧路径）；`last_frame_ms` 为单调毫秒且 ≤2s 新鲜；`fps` 2s 滑窗填满后 >5（test 源 25fps）且间隔再采样仍 ≥5；`last_frame_ms` 随新帧推进（pump 驱动）。
   - TC-V13-02：自定义 camera_id 启动（机器身份透传）。
   - TC-V13-03：媒体层主动 `simulate_cut()` 后 `reconnect_count≥1`、`presentable=false`、`pump()` 不再出帧、冻结的 `last_frame_ms` 保留——FI-01 注入在媒体层实际接线有效。
   - TC-V13-04：独立探针时间线（同一运行器镜像）cut→reconnecting、无 live 复活、presentable 与状态一致。
   - TC-V13-05：`reconnect_count` 时间线单调；不断流持续运行 7s 计数 ≥2（迭代退避链）。
   - TC-V13-06：无效 file 源启动调用不产生帧、`stop()` 干净收尾（无相机不可用路径不崩溃）。

## 2. v6 复现探针结构化输出（板上原文，下载归档 `VERIFY-FIXTURES-VERIFY-20261016-v6.core-evidence_stream-probe-VERIFY-20261016-v6.log`）

```
t=0      live        fps=0.5  last_frame_ms=218076628 reconnect_count=0 presentable=true
t=537..  live        ...2537 fps=25.5 ...
t=3017 injection cut=FI-01
t=3017   reconnecting fps=24.5 last_frame_ms=218079606 reconnect_count=1 presentable=false
t=3500/4000 reconnecting ...
t=4500/5000/5500 connecting fps=6.0→0.0 ... presentable=false
type=summary final_state=connecting reconnect_count=1 last_frame_ms=218079606 presentable=false
```

与实现者板端归档 r2 逐字段一致（仅时间戳与采样毫秒为本轮新值）；与用户可观察点核对：**断流后该路进入重连中（reconnecting）并显示已重连次数（reconnect_count=1）与最近帧时间（last_frame_ms 冻结在 218079606）**；**非 LIVE 状态 presentable=false**（旧帧不被当作实时；渲染占位层归 UI 域，见 §5）。

## 3. 与设计判据逐条核对

| 判据来源 | 本轮证据 |
|---|---|
| 任务项"原生 GStreamer 逐帧发布，接入 core 六态与质量标志"（DESKTOP_APP_DESIGN §6.1/§6.2） | 源码管线 `rtspsrc ... protocols=udp` 强制 UDP + tee 双分支（camera_stream.cpp:26-35）；appsink→`machine->on_frame` + `classify_quality`（camera_stream.cpp:196-203）；TC-V13-01 真实帧进 LIVE |
| "统计 fps/最近帧时间/重连次数" | TC-V13-01 滑窗 fps>5 且 last_frame_ms 推进；cut 后 reconnect_count/last_frame_ms 语义（§1.3） |
| "断流后进入重连中并显示已重连次数与最近帧时间" | §2 v6 序列 cut→reconnecting count=1 last_frame 保留 |
| "非 LIVE 状态渲染占位层且旧帧不被当作实时画面" | 数据侧 presentable=false 全断言组通过（含 DEGRADED/LIVE 边界由 ITEM-005 core 域覆盖 TC-CORE-02）；QML 占位层渲染属 UI/平台边界（§5.3） |
| "file/test 源运行同一管线（无相机可用）" | §1.4 cutonly 全程非 live 管线收尾干净；TC-V13-06 无效 file 不崩溃；`--source file/rtsp` 入口与参数解析实装（stream_probe.cpp:63-73、camera_stream.cpp:23-29） |
| 六态/升级矩阵（ reachability 本项不重复定义） | ITEM-005 core 单测（79/79）与 ITEM-009 `--fi 01` 仿真已覆盖；本轮媒体层证据与之一致（cut→reconnecting→退避→connecting 序列） |

## 4. 上游与历史核对（RESUME_SYNC）

- 实现者本轮交付摘要 §2 的板端实测（test 源 + cut 注入序列）本轮**逐字段独立复现一致**（v6 §2），不是对旧证据的转述。
- 工作树与 manifest 同步：git status 干净（HEAD d96b0d2），manifest 已含 ITEM-013 四个源文件与本证据目录条目；本轮新增验证侧文件见 §6。

## 5. 边界（未验不判过，缺口如实归属）

1. **真机 RTSP 六路（ENV-D / TC-STREAM-05/09）未执行**：`read_camera_stream` 入口对多路为 configured_unverified，四路凭据未核验且端到端取流属后续真机档；本轮 test 源覆盖的是管线/状态机/统计语义，不外推为真机取流通过。
2. **QML 占位层视觉呈现未验证**：目标板 Qt Quick/Qml 开发包缺失（E-01b，ITEM-010 起持续登记）；`presentable` 数据判据已验证，像素级渲染与"占位 + 最近帧时间"显示归 UI 渲染任务/评审域。
3. **file 源端到端解码帧**：板上无测试素材，仅验证"无效 file 源不崩溃 + 入口就绪"；真实文件解码重放一致性归 ENV-C 后续档。
4. **质量标志内容判定**：媒体层 mean/stddev → classify_quality 接线在 TC-V13-01 走通（无标志→LIVE）；质量阈值边界矩阵归属 ITEM-005 验证证据（40/40 + 扩展夹具），本轮不重复定义。
5. TC-STREAM-06（页面切换不重建）、TC-STREAM-07（六路并发）不在本工作项范围。

## 6. 本轮验证自有文件（维护至 file_manifest.txt）

- `VERIFY-ITEM-013.md`（本文档，TestEvidence 判定）
- `VERIFY-FIXTURES-VERIFY-20261016-v6.{log,transcript.json}`（权威轮日志与逐条命令 transcript）
- `VERIFY-FIXTURES-VERIFY-20261016-v6.core-evidence_stream-probe-VERIFY-20261016-v6.{log,json}`（板上交付探针独立复现的结构化归档）
- `VERIFY-FIXTURES-VERIFY-20261016-v1..v5.{log,transcript.json}`（过程轮留痕：v1 验证侧 `t=t_ms` 键错误/下载漏 /app；v2 last_frame_eq 断言时机错误；v3/v4 fps 采样过早与 pump-推进断言时机错误，均为验证侧自查更正，不作判据）
- `verify_stream13_ext.cpp` / `verify_stream13_fixtures.py`（验证自有源码，可独立复现：`python verify_stream13_fixtures.py --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU --cpp verify_stream13_ext.cpp --outdir . --stamp <stamp>`）

## 7. 复现（权威口径）

```sh
# 板端（经登记 SSH 入口，独立工作目录 /tmp/railhmi_verify013{,_board}，结束按名清理）
cd /tmp/railhmi_verify013_board/app && bash build.sh MediaCheck
./build-media/tools/railhmi-stream-probe --source test --duration 6 --cut-after 3 \
  --out core-evidence/stream-probe-<stamp>.log --json core-evidence/stream-probe-<stamp>.json
# 主机（验证运行器全程：上传快照 + 构建 + 两个探针轮 + 扩展夹具 + 清理）
python docs/hmi/test_evidence/ITEM-013-stream/verify_stream13_fixtures.py \
  --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --cpp docs/hmi/test_evidence/ITEM-013-stream/verify_stream13_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-013-stream --stamp VERIFY-20261016-v6
```

结论：ITEM-013 在本轮可执行范围内（test 源 + 媒体层注入 + 状态机/统计/presentable 契约）**TEST_PASS**；真机六路 RTSP 与 QML 占位层按 §5 边界留待对应环境，不得由本证据外推。
