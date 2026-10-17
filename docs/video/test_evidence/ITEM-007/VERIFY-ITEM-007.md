# ITEM-007 多路并发与单路失败隔离 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification）
- Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-007「多路并发与单路失败隔离」（task_id `…-tasks-ITEM-007`）
- 实现版本: commit `c5b880f`（交付 `docs/video/implementation/ITEM-007_MULTI_WORKER.md`，板端 stamp `20261017-item007-r1`）
- 验证 stamp: `20261017-item007-verify-v1`（本目录 `multi-…verify-v1.log/.transcript.json` + `multi-ext-….json`）
- 日期: 2026-10-17；目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3 / librga）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现方 r1 同板同 key）

## 1. 结论

**TEST_PASS**（范围=ITEM-007 当前版本：每路独立 `CameraWorker` 专用线程、`MultiStreamManager`
线程安全快照（stats/latest_frame/is_presentable/start_error/started/index_of）、单路启动失败隔离
（`ChannelSpec.start_failure` 故障注入）、单路断流隔离（`request_cut` FI-01）、单路重建隔离
（`request_restart`：generation+1、不重连计数）、槽位顺序恒等不重排不丢格、六路并发无相互
阻塞（帧钟推进 + fps≥20 排除串行泵退化）；不含自动重连控制器（ITEM-014）、真实六路相机负载
（ENV-D）、端到端延迟/容量仪器（ITEM-017）、UI 上屏（ITEM-009 桌面域））。

## 2. 独立复跑（不采信实现方归档）

交付入口 `app/tools/board_multi_worker_check.py --host 192.168.0.103 --user admin
--outdir <本目录> --stamp 20261017-item007-verify-v1 --keep-remote`（凭据经 PowerShell 用户级
环境导入当进程），全部 8 步 exit=0（mkdir/upload 200 文件/build-media/selftest/run-multi/
download×2/cleanup；构建树先保留为 `/tmp/railhmi_multi` 供 EXT 用，收尾按名清除）。

与实现方 r1 对比（判据从原始逐拍 JSON 独立重建，**不采信两轮 type=acceptance 行**）：

| # | 判据（独立解析 `_verify_compare.py`） | 结果 |
|---|---|---|
| A1/A5/A6 | 两轮逐拍槽位身份恒等 CAM-01..CAM-06、cells=6、live/presentable 计数与 cell 行自洽；失败路 idx5 全程 offline/lease=false/generation=0（presentable 恒 false） | PASS |
| A2 | 断流前每拍非故障 5 路 presentable=true（两轮） | PASS |
| A3 | 断流后 cut 路 presentable=false 且 last_frame_ms 轮内冻结（r1 冻结于 354057673、verify 轮 355892359；单调钟跨轮本就不同，冻结性为轮内判据） | PASS |
| A4 | 断流后其余非故障路 presentable=true 且帧钟严格推进（两轮） | PASS |
| A7 | selftest 6 判例两轮逐行一致（`manager_size/slot_order_config/index_of/start_all_isolates_single_failure/fail_channel_reported/slot_order_after_start`） | PASS |
| A8 | 两轮 16 窗逐 cell (idx,state,presentable,lease,generation,无帧) 语义序列完全一致（fps 为自然波动非判据） | PASS |
| A9 | 实现者 acceptance 行两轮均 result=PASS（生产标签链路一致性核对） | PASS |

注意：实现者 `slot_order_stable` 判定只查每拍 `cells` 数量、未查槽位身份——本验证 A1 的
身份恒等判据为验证侧补强（逐拍 CAM-01..06 恒等已覆盖"网格槽位不重排"的措辞全义）。

## 3. 验证侧扩展夹具（EXT 6/6；`multi-ext-…verify-v1.json`）

交付轮次未覆盖、由本夹具按验收措辞独立驱动的边界：

| # | 判据 | 结果 |
|---|---|---|
| EX-A | **六路全健康并发非阻塞**（实现 r1 只测了单路失败/断流模式）：`fail-index=-1` 无注入 6s 运行，12 个采样窗 grid 恒 6 live/6 presentable、槽位恒等、全部 6 路帧钟逐窗严格推进（任何 worker 阻塞它路都会使其帧钟停滞）、每路 fps≥20（串行顺序泵 25fps 源时每路 ≤~4fps，判据排除功能退化） | PASS |
| EX-C | 板端 g++ 直编验证侧自有 C++ 夹具（`MultiStreamManager` 直驱，链 `librailhmi_media/core/platform.a`）**35 判例全过**（判例名表逐一核对，无静默少跑） | PASS |
| EX-D | CLI 负例：未知参数 exit=2（`unknown arg`）；`--count 0` → started=0 → exit=3（`all channels failed to start`） | PASS |
| EX-E | 确定性：同参六路两轮 acceptance 均 result=PASS，10 窗逐 cell (state,presentable,lease,gen,无帧) 语义恒等 0 差异（fps 自然波动排除） | PASS |
| EX-F | 凭据泄漏扫描：本目录 17 个文件对三凭据值凭据语义形态（user-pw/password/v@host）0 命中（SSH 凭据值=admin 同 ITEM-002/005/006 口径，仅计非凭据形态文件 6 个） | PASS |

EX-C 自有 35 判例要点（均不与交付 selftest 重合，期望先于实测构造，`vext OK (0 failure(s))`）：

- **重建隔离（任务书三场景之三，实现归档未测）**：六路健康运行 → `request_restart(3)` →
  重建窗口内其余五路 presentable 恒 true 且帧钟持续推进（g10_others_presentable/advancing_
  during_rebuild）→ 该路 generation 1→2（g10_rebuilt_generation_2）、`reconnect_count==0`
  （手动重建不算重连，g10_manual_restart_not_a_reconnect）、恢复 presentable（g10_rebuilt_recovers）、
  槽位顺序不变（g10_slot_order_unchanged）。
- **单路 cut 隔离+恢复**：cut(idx=1) → 该路 presentable=false、last_frame_ms 冻结（600ms 复查）、
  其余 4 健康路 presentable=true 且帧钟推进；restart 后 generation+1 并恢复 presentable、
  其余路 generation 恒 1（未被重启）（g06_*，9 判例）。
- **双路同时断流互不干扰**：cut(idx=0)+cut(idx=4) 后两路不 presentable、中间健康路 presentable
  且帧钟推进（g09_*，3 判例）。
- **启动失败隔离细查**：`start_all` 返回 5（g02_start_all_returns_5）、err 串点名 CAM-06（g02_err_
  names_failed_channel）、其余 starts=true 且 1.2s 后五路 presentable（g02_others_started/g02_five_
  presentable）、槽位 start 前后恒等（g03_slot_order_unchanged）。
- **API 边界**：未 start 全 offline/presentable=false（g05）；`latest_frame(999)` 空租约 NOT_READY、
  `is_presentable(999)=false`、`started(999)=false`、`start_error(999)` 空串（g08_*，越界安全）；
  `latest_frame(camera_id)` 与 `latest_frame(index)` 同源同 session（g07_same_*，4 判例）、未知
  camera_id 返回空租约（g07_unknown_id_empty_lease）；size/index_of（g01）。

## 4. 版本指纹

- 板端复跑 stamp `20261017-item007-verify-v1`；上传 200 文件（本地 `app/`）。
- 新证据 sha256(16)：VERIFY 文档见 file_manifest（本文件写入后登记）；复跑逐拍 log `1b5825db38664b48`
  / JSON `077e9b8648dc20a1`；transcript `e66c09fc98cac820` / log `d876534606b79b4a`；
  EXT 结果 `941dc9b8346fdee8`；比对结果 `_verify_compare_result.json` `c266fb42f75fd787`。
- 实现方 r1 归档 digest 核对一致（同板同 key，见 r1 目录件）。
- 判据为当前版本行为，不外推其它组件/版本。

## 5. 边界与说明

- 本验证不覆盖：自动重连控制器（ITEM-014，本项交付 request_restart 手动路径）、真实六路相机
  负载与带宽（ENV-D/现场）、端到端延迟与容量预算（ITEM-017）、UI 上屏占位渲染（ITEM-009 桌面域）。
- 观察项 O-1（非缺陷，交实现参考）：probe 的 `--cut-index/--fail-index` 接受 -1 和越界值均可
  正常收尾（越界注入为无操作，acceptance 判据自身对注入不存在时恒真分支按设计放行），CLI
  负例仅覆盖未知参数与 count=0；未新增验收判据，不作为缺陷。
- 观察项 O-2（非缺陷）：六路 test 源 25fps 全健康时每拍 fps 采样在 25~25.5 之间波动（沙漏
  帧时钟量化），EX-A 以 ≥20 界判定，非逐拍恒等。
- 板端临时项已清理：`/tmp/ext007_board`、`/tmp/vext_multi_stream`、`vext_multi_stream.cpp`
  逐项确认不存在；railhmi_multi 构建树（--keep-remote 保留）收尾按名删除确认 `MULTI_TREE_CLEANED`。
