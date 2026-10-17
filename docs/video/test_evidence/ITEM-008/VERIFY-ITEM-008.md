# ITEM-008 三类消费者的有界队列与背压 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification）
- Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-008「三类消费者的有界队列与背压」（task_id `…-tasks-ITEM-008`）
- 实现版本: commit `6e14fce`（交付 `docs/video/implementation/ITEM-008_BACKPRESSURE.md`，板端 stamp `20261017-item008-r2`）
- 验证 stamp: `20261017-item008-verify-v1`（本目录 `bp-…verify-v1.log/.transcript.json` + `bp-ext-….json`）
- 日期: 2026-10-17；目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3 / librga）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现方 r2 同板同 key）

## 1. 结论

**TEST_PASS**（范围=ITEM-008 当前版本：header-only `BoundedQueue<T>`（逐出最旧、可选字节预算、
非阻塞、计数 pushed/dropped/popped/size/bytes，cap0 归一 1）；`FrameDispatcher` 三消费者策略——
显示 latest-only（默认深 1，display_dropped）、AI 丢最旧有界队列（cap=`app.ai.queue_depth`，
配置域 [1,8]，ai_dropped/ai_depth）、录像独立有界分支（cap=`recording.frames` 可选字节预算，
recording_overflow 记缺口）；`pump()` 单线程泵内 dispatch，慢消费者不阻塞取流；背压探针
`railhmi-backpressure`（test/file/rtsp 源、--display-hz/--ai-hz/--stop-recording-after/--ai-depth/
--recording-frames、exit 0/1/2/3 语义、--selftest 4 判例）。**不含**：压缩/分片管线本身与录像落盘
（ITEM-011）、UI 上屏（ITEM-009 桌面域）、`ai.queue_depth` 配置接线到产品启动路径（ITEM-010 配置域，
交付内探针已带同参数）、真实相机负载（ENV-D））。

## 2. 独立复跑（不采信实现方归档）

交付入口 `app/tools/board_backpressure_check.py --host 192.168.0.103 --user admin
--outdir <本目录> --stamp 20261017-item008-verify-v1 --keep-remote`（凭据经 PowerShell 用户级
环境变量 `TRAIN_RK3588_SSH_CREDENTIAL` 导入当进程），全部 9 步 exit=0（mkdir/upload 206 文件/
build-core/core-tests/build-media/selftest/run-bp/download×2；构建树先保留 `/tmp/railhmi_bp`
供 EXT 用，收尾按名清除并确认 `BP_TREE_CLEANED`）。

verify-v1 轮实测：core-tests `== 744 passed, 0 failed ==`（exit=0）、selftest 4/4、run-bp
acceptance `result=PASS`（capture_rate=30、display/ai 1Hz 慢消费者、stop-recording@4s）。

与实现方 r2 对比（判据从两轮原始逐拍行独立重建，**不采信两轮 type=acceptance 行**；
`_verify_compare.py` → `_verify_compare_result.json`）：

| # | 判据（独立解析） | 结果 |
|---|---|---|
| C1..C4 | 两轮 injection 配置回显 `source=test duration=8s display/ai 1Hz stop@4s ai_depth=2 recording_frames=8`；t≥2001ms 每拍取流 fps≥20（ramp 完成后取流不塌陷）；display_dropped/ai_dropped 逐拍非降且终值大额（慢消费者丢帧持续可观测）；ai_depth 每拍 ≤2（有界） | PASS |
| C5 | 停录像注入前（t<4002ms）每拍 recording_overflow==0（显示/AI 重丢帧下录像分支零溢出——TC-V-REC-01 独立性判据） | PASS |
| C6 | 注入后 recording_overflow 严格递增、recording_depth 每拍 ≤8 且收尾==8（有界溢出记缺口） | PASS |
| C7 | 注入后 recording_reads 冻结（录像消费者停止读取） | PASS |
| C8 | 两轮 type=summary + type=acceptance result=PASS、run-bp exit=0（生产标签链路一致性核对） | PASS |
| C9 | 尾段帧率斜率独立重推导（summary 标量不采信，见 C2 逐拍） | PASS |
| C10 | selftest 4 判例两轮逐行一致 | PASS |
| C11 | 两轮 744/0 且 bounded_queue 组 18 条 PASS 行逐行一致 | PASS |

比对 8/8 PASS、problems=0。逐拍证据补充：两轮 display_dropped 终值 225/225（==dispatched−1，
latest-only 每 push 后逐出一次的精确算术，见 O-2）、ai_dropped 终值 216/216、pre-stop overflow
0→0、post-stop overflow 7→82（r2）/同序（verify）、recording_depth 收尾 8/8。

## 3. 验证侧扩展夹具（EXT 7/7；`bp-ext-…verify-v1.json`）

交付轮次未覆盖、由本夹具按验收措辞独立驱动的边界（复用保留构建树 /tmp/railhmi_bp）：

| # | 判据 | 结果 |
|---|---|---|
| EX-A | **独立参数全背压轮**（非交付参数 6s/2Hz/2Hz/stop@3s/depth2/frames4）12 拍逐拍判据全过：injection 回显、t≥2s fps≥20、display/ai dropped 非降且增长、ai_depth≤2、pre-stop overflow==0、post-stop overflow 递增 rd 收尾==4、recording_reads 冻结、acceptance PASS | PASS |
| EX-B | **快显示/慢 AI 摆放反转**（display 25Hz≈不丢帧 / ai 1Hz）：TC-V-REC-01 字面——慢 AI 单独可观测（ai_dropped 3s 内增长 >20）、显示丢弃近零、录像分支照常（pre-stop 0 溢出） | PASS |
| EX-C | CLI 负例/正例：`--bogus-flag` → exit=2 且 `unknown arg: --bogus-flag`；`--help` → exit=0 且 usage 含 `--selftest`/`--recording-frames` | PASS |
| EX-D | 板端 g++ 直编验证侧自有 C++ 夹具（`bounded_queue.h` header-only 独立编译，`-std=c++17 -pthread -I src/core/include`，不依赖交付库）**38 判例全过**（判例名表逐一核对 missing=none，无静默少跑） | PASS |
| EX-E | 确定性：同参（EX-A 参数）两轮 12 窗逐拍 (dd,ad,aid,ov,rd,dr,ar,rr) 语义序列 0 差异（fps 自然波动排除） | PASS |
| EX-F | 凭据泄漏扫描：本目录 14 文件凭据语义形态（user-pw/password/admin@host token 形态）0 命中（SSH 凭据值=admin 为登记 username，同 ITEM-002/005/006/007 口径） | PASS |

EX-D 自有 38 判例要点（均不与交付 selftest 重合，期望先于实测构造）：

- **v01/v02 逐出策略**：cap1 三 push → size1/dropped2/latest=3/push 返回 true；cap2 FIFO 丢旧序
  [b,c] 与 pop 序 b,c。
- **v03 字节预算**：累计 80 字节逐出到 60 界、逐出序保持最旧先出（pushed==dropped+popped+size）。
- **v04 退化参数**：cap(0,0) 不崩不挂、计数自洽、空 pop false（cap 归一后仍 bounded）。
- **v05 latest 非驻留**：latest() 非移除（size 不变）、pop 最旧先出、latest 在 pop 后更新。
- **v06/v07 空队列与 cap0**：空队列 pop/latest false 且计数归零；cap0 构造归一为 cap1。
- **v08 计数不变式（含字节预算路径）**：cap(3,60) 12×10B push（9 次逐出 → size3/bytes30）后
  2 次 pop → size1/bytes10、不变式恒等（首次上板期望值 20 为夹具构造错——重推导后修正，
  见 §5 O-3）。
- **v09 双生产者双消费者并发**：cap(64,6400) 2×2000 push / 2 receiver，join 有限完成（无死锁）、
  不变式 pushed==dropped+popped+size 恒等（INFO 行 popped_by_receivers=2595+popped_verify=4000+
  dropped=1405+size=0 == pushed=4000）、size≤64 且 bytes≤6400 双界。
- **v10 NV12 量级字节预算**：bytes=1_500_000、单项 700_000 → 持 2 项；第 3 项逐出最旧（dropped=1）。
- **v11 混合尺寸字节账目**：80+10+5=95 恰界内；逐出序经 pop 复核为 2,3,4（1 已被逐出）。
- **v12 pop 清空**：清空后 bytes/size 归零。

## 4. 版本指纹

- 板端复跑 stamp `20261017-item008-verify-v1`；上传 206 文件（本地 `app/`）。
- 新证据 sha256(16)：复跑逐拍 log `dfc4a278fa5afc51` / transcript `f16de8272e4a1293`；
  core-evidence log `8f5d18942ff0b39f` / json `84c0d0868a886d52`；EXT 结果
  `94d396f1bbe4b1bc`；比对结果 `_verify_compare_result.json` `67ebdc0d2c70d17c`；驱动脚本
  `_verify_run.py` `90683293ec7183ca` / `_verify_compare.py` `970feb00c7af04fe` /
  `_verify_ext.py` `610e555c95c3aa81` / `_dryrun_selfcheck.py` `a908c6d15309936b`。
- 实现方 r2 归档 digest：逐拍 log `43b9ca5bd8b8bbb6` / transcript `36c5e13a666b1fa6` /
  core-evidence log `e818f3c12614f5d6`（同板同 key）。
- 判据为当前版本行为，不外推其它组件/版本。

## 5. 边界与说明

- 本验证不覆盖：压缩/分片管线与录像落盘本身（ITEM-011，本项仅交付其上游有界队列与
  recording_overflow 机制）、`ai.queue_depth` 配置接线到产品启动路径（ITEM-010 配置域；交付内
  探针 CLI 已带同参 `--ai-depth`，配置域 [1,8] 校验在 `config.cpp` 已见）、真实相机负载（ENV-D）、
  UI 上屏（ITEM-009）。
- 观察项 O-1（非缺陷，交实现参考）：探针 summary 行在 worker join 后、worker.stop() 前读取
  counters，显示/AI 消费者可能再多消费一帧 → summary 的 display_dropped 可比逐拍终值多 1
  （off-by-one 读序竞态，非产品缺陷；acceptance 判据不受影响）。
- 观察项 O-2（非缺陷，语义澄清）：cap1 latest-only + 非移除 latest() 下 display_dropped
  == pushed−1 精确成立（首个 push 占位后每次 push 均逐出），逐拍表两轮均符。
- 观察项 O-3（夹具侧，非产品缺陷）：EX-D 首次上板 v08 期望值 20 为验证夹具构造错误
  （重推导：12×10B 入 cap(3,60) → 9 次逐出 → size3/bytes30 → 2 pop → size1/bytes10），修正
  期望后 38/38 全过；期望修正记录于本文件以保持过程透明（v1 过程轮口径）。
- 板端临时项已清理并确认：`/tmp/vext_bounded_queue(.cpp)`、`/tmp/extbp-*.log/.json` 逐项
  不存在；railhmi_bp 构建树（--keep-remote 保留）收尾 `rm -rf` 确认 `BP_TREE_CLEANED`。
