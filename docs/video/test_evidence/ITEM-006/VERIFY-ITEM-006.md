# ITEM-006 每路取流 worker 与状态机接入 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification）
- Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-006「每路取流 worker 与状态机接入」（task_id `…-tasks-ITEM-006`）
- 实现版本: commit `c6e0e7d`（交付 `docs/video/implementation/ITEM-006_CAMERA_WORKER.md`，板端 stamp `20261017-item006-r3`）
- 验证 stamp: `20261017-item006-verify-v1`（本目录 `worker-…verify-v1.log/.transcript.json` + `worker-ext-….json`）
- 日期: 2026-10-17；目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3 / librga）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现方 r3 同板同 key）

## 1. 结论

**TEST_PASS**（范围=ITEM-006 当前版本：每相机独立 worker 线程、有限等待拉帧（单次 pull 钳制 ∈ [1,200]ms）、
drain GstBus ERROR/EOS/STATE_CHANGED、core 六态机驱动（首帧超时/冻结/重连退避/阈值升级）、
fps/最近帧单调+墙钟/重连次数/质量标志统计、真正重建 generation+1、presentable = may_present_as_live(now)
且租约属于当前 generation；不含多路并发隔离（ITEM-007）、自动重连控制器（ITEM-014）、端到端延迟仪器（ITEM-017）、UI 上屏（ITEM-009））。

## 2. 独立复跑（不采信实现方归档）

交付入口 `app/tools/board_camera_worker_check.py --host 192.168.0.103 --user admin
--outdir <本目录> --stamp 20261017-item006-verify-v1 --keep-remote`（凭据经 PowerShell 用户级环境导入
当进程），全部 9 步 exit=0（board tmp 树保留为 `/tmp/railhmi_worker` 供 EXT 用）。

与实现方 r3 对比（状态序列从原始逐拍日志独立解析，不采信 acceptance 行）：

| 运行 | r3 → verify-v1 状态序列一致性 | 时间抖动 | summary 关键项 |
|---|---|---|---|
| freeze→restart | 完全一致（live→frozen(1.8s)→reconnecting(2.5,count1)→connecting→reconnecting(3.3,count2)→connecting→offline(4.0,count3,escalated)→restart 后 live） | ≤1ms | `freezes=1 reconnect_attempts=3 escalations=1 nonlive_presentable=0 final_generation=2 iterations=126` |
| cut FI-01 | 完全一致（live→reconnecting(1.3,count1)→connecting→reconnecting(2.0,count2)→connecting→offline(2.8,count3,escalated)） | ≤1ms | `reconnect_attempts=3 escalations=1 nonlive_presentable=0 iterations=50` |
| freeze→resume | 完全一致（同升级序列；resume(6.0)→live gen1 seq 恢复递增） | 0ms | `recoveries=1（Recovered 同会话触发）nonlive_presentable=0` |
| rtsp CAM-01 | 完全一致（connecting(~0.8s)→live ~26fps） | ≤1ms | `bus_state_changes=102 nonlive_presentable=0` |

selftest 8/8 逐行一致（六态 live→frozen→reconnecting→offline 升级→恢复计数清零；pull 钳制 200/1）；
全部运行 `nonlive_presentable=0`（非 LIVE 状态下旧帧从未被当作实时画面）。

## 3. 验证侧扩展夹具（EXT 8/8，自有判据；`worker-ext-…verify-v1.json`）

交付轮次未覆盖、由本夹具按验收措辞独立驱动的边界：

| # | 判据 | 结果 |
|---|---|---|
| EX-A | pull 钳制黑盒复现：`--pull-ms 9999 → pull_ms=200`、`--pull-ms 0 → pull_ms=1`（注入行实值） | PASS |
| EX-B | **真实 GstBus ERROR 路径**（实现方夹只走 simulate_* 注入；本夹用空 URL rtsp 驱动真 bus ERROR）：错误 `Resource not found.` 被 drain → reconnecting；快阈值下 reconnect_attempts=3、escalations=1、final offline、nonlive_presentable=0、iterations=30 有界 | PASS |
| EX-C | **file 源 EOS 循环**（EOS 分支）：2s x264 素材跑 5s，t≥4000ms 槽位仍有帧（4 槽），reconnect_attempts=0、escalations=0（EOS 回卷不算源错误）、final live | PASS |
| EX-D | 缺失文件显式运行期失败：error 行非空、final offline、正常收尾退出 0、iterations=10 有界 | PASS |
| EX-E | 冻结期不忙等实测：4s 运行 1s 冻结，worker iterations=40（忙等形态应为万级；≤120 界内） | PASS |
| EX-F | 板端 g++ 直编 `librailhmi_media.a` **验证侧自有 C++ 夹具 18 判例**（CameraStream 直驱）→ `vext OK (0 failure(s))` VEXT_EXIT=0 | PASS |
| EX-G | 凭据泄漏扫描：本目录 16 个文件对两相机凭据值凭据语义形态 0 命中（SSH 凭据=admin 同 ITEM-002/005 口径仅记非凭据形态文件） | PASS |

EX-F 自有判例（均不与交付 selftest 重合，期望值先于实测构造）：

- **旧代纪租约门（验收"租约属于当前 generation"的直接观测）**：保存 generation1 租约 →
  `stop()+start()` 真正重建 → generation 1→2 → `lease_presentable(旧租约)=false`（f01）、
  当前租约=true（f07）、`presentable_now=true`（f10）、generation 恰 +1（f05）。
- **质量标志统计出口**：阈值边界黑盒驱动 —— dark_mean=255 → `quality_flags` 含 too_dark 且
  state 转 **degraded**（f11/f12）；degraded 有新鲜帧 `presentable_now=true`（f13，唯一依据
  may_present_as_live 不能只看 state==live 的契约点）；bright_mean=0 → too_bright（f14）；
  min_stddev=999 → occluded（f15）。
- 未 start 的 `presentable_now=false` / `latest_frame()=NOT_READY`（f08/f09）。
- `start()` 幂等：二次 start 直接 true 且不递增 generation（f17/f18/f19）。

## 4. 版本指纹

- 板端复跑 stamp `20261017-item006-verify-v1`；上传 199 文件（本地 `app/`）。
- 新证据 sha256(16)：VERIFY 文档 `7c701cded0136432`、transcript `712df637f931ec5a`、
  复跑日志 `cd695facf430eda9`、EXT JSON `7472a38c456972cc`、freeze/cut/resume/rtsp 原始件
  `5e7e4051c35e3b07` / `619afc1804fd68d0` / `8f4982c50fe5cfff` / `0b7c332486d275ed`。
- 实现方 r3 归档 digest 全部核对一致（见 `…r3.log/.transcript.json`）。
- 判据为当前版本行为，不外推其它组件/版本。

## 5. 边界与说明

- 本验证不覆盖：六路并发隔离与槽位不重排（ITEM-007，TC-V-WORKER-01/02）、自动重连控制器
  （ITEM-014，本项交付的是 request_restart/resume 手动路径）、端到端延迟（ITEM-017）、
  UI 上屏占位渲染（ITEM-009 桌面域）。
- 观察项 O-1（非缺陷，交实现参考）：空 URL rtsp 的真实 bus 错误只产生一次 `Resource not found.`
  事件（bus 死后不再重复发错），后续重连尝试由 connecting 内 first-frame timeout 驱动 ——
  与 `on_source_error` 直注路径（reconnect#1 即 reconnecting）不同；行为与退避设计一致，
  事件序列符合 stream_state.cpp 语义。
- 观察项 O-2（非缺陷）：file 源 EOS 回卷 fps 达 ~620（纽帧时钟 sync=false 非实时），属
  ITEM-002 已登记 O-2 同源现象；防下游仪器把 file fps 当容量数据。
- 验证侧过程记录：v1 夹具一次通过（EX-F 编译在正式 ext 全跑前先单独预检编译+运行成功后
  再随夹具全流程复跑）；无产品缺陷误报。
- slow Downing test 源实测 FPS 波动（2→26 爬坡）为 2s 滑窗统计口径（frames/2.0），非缺陷。

## 6. 复现

```bash
python docs/video/test_evidence/ITEM-006/_verify_run.py   # 独立复跑（需板端/相机凭据 env）
python docs/video/test_evidence/ITEM-006/_verify_ext.py   # EXT 夹具（需 --keep-remote 树在板）
```
