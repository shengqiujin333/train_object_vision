# ITEM-004 不可变帧租约与缓冲区所有权 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification）
- Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-004「不可变帧租约与缓冲区所有权」
- 实现版本: commit `46cf556`（交付 `docs/video/implementation/ITEM-004_FRAME_LEASE.md`，板端 stamp `20261017-item004-r2`）
- 验证 stamp: `20261017-item004-verify-v1`（本目录 `lease-…verify-v1.log/.transcript.json` + `lease-ext-….json`）
- 日期: 2026-10-17；目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现方 r2 相同）

## 1. 结论

**TEST_PASS**（范围=ITEM-004 当前版本：帧布局/描述符/共享租约/release 回调/并发读取/显式拒绝；不含多路聚合 ITEM-007、格式转换 ITEM-005、显示/推理/录像消费者）。

## 2. 独立复跑（不采信实现方归档）

交付入口 `app/tools/board_frame_lease_check.py --host 192.168.0.103 --user admin --outdir <本目录> --stamp 20261017-item004-verify-v1 --keep-remote`，全部 8 步 exit=0：

| 步骤 | 结果 | 与实现方 r2 对比 |
|---|---|---|
| build-core / core-tests | `726 passed, 0 failed` | 一致（726/0） |
| build-media / selftest | 6/6 PASS | 一致（6/6） |
| lease-test（test 源，origin=simulation） | NOT_READY→descriptor→并发→release_after_last=1 | 判据行全部一致 |
| lease-rtsp（真实 CAM-01，origin=live） | 同上 | 判据行全部一致 |

判据行核对（verify-v1 vs 实现 r2）：

- `lease_pre not_ready=true status=NOT_READY plane_count=0` — 两侧一致。
- `lease_first … format=NV12 size=1280x720 buffer_bytes=1382400 plane0_stride=1280 plane1_offset=921600 plane_ok=true` — 两侧一致（origin 分别 simulation/live）。
- `lease_shared use_count=3 released_while_shared=0` / `lease_partial release_after_copies=0` / `lease_last release_after_last=1` — 两侧一致（回调恰一次链路）。
- 自然波动项（非判据）：reads 4413→4405、published 77/78→77/80、max_use_count 2/4→3/4、released_total 76/77→76/79；`rejected=0` 一致。
- `summary final_not_ready=false` — 两侧一致（stop 后最新帧仍可取）。

算术独立核对：Y=1280×720=921600；UV offset=921600=Y size（连续 NV12）；总 1382400=921600×1.5。descriptor 的 planes/buffer_bytes 与 caps 推导一致。

## 3. 验证侧扩展夹具（EXT 10/10，自有判据）

绑定 `--keep-remote` 保留的 `/tmp/railhmi_lease` 树，黑盒驱动交付入口（`lease-ext-…verify-v1.json`）：

| # | 判据 | 结果 |
|---|---|---|
| A | `--consumers 0`：流程完整走完（`reads=0`、lease_shared/lease_last 段正常、`release_after_last=1`、A_EXIT=0）非崩溃 | PASS |
| B | 未知参数 `--bogus-flag` → exit 2 且报 `unknown arg`；`--help` → exit 0 且用法行含 `--selftest` | PASS |
| C | 独立解析租约日志（不采信交付 acceptance 行）：NOT_READY/平面计数、release 恰一次链路（shared=0/partial=0/last=1）、`final_not_ready=false`、首帧 `buffer_ref=fr-CAM-01-1` | PASS（4 项） |
| D | 验证侧自有 C++ 负例：板端 g++ 直编 `src/core/src/frame_layout.cpp` + 27 判例（连续/padded NV12 算术、零维、奇宽/奇高、Y/UV stride 不足、UV 进 Y、packed 最小 stride/size/stride<w×3、factory 拒绝 NV12、validate 零 buffer/UV 越界/UV size 不足/packed 越界/重叠/NV12 单平面、parse/to_string 往返）→ `vext OK (0 failure(s))` VEXT_EXIT=0 | PASS |
| E | 凭据泄漏扫描：本目录全部证据文件对两相机凭据值 0 命中（SSH 凭据值=登记用户名 `admin`，`--user admin` 是命令行合法 token，按 ITEM-002 同口径不计文件级假阳性） | PASS |

D 项复用 r2 留下的板端树只需一次编译（0.14s）+ 运行，独立期望先行（离线 dry-run `_dryrun_selfcheck.py` 对归档日志核对全部正则与组序后上板）。

## 4. 版本指纹

- 板端复跑 stamp `20261017-item004-verify-v1`；上传 191 文件（本地 `app/`）。
- 新证据文件 sha256(16)：transcript `3b484ae11a5c3a24`、日志 `55a3a69f40a8c2ed`、EXT JSON `72575328ead660b8`。
- 判据为当前版本行为，不外推其它组件/版本。

## 5. 边界与说明

- 本验证不覆盖：多相机聚合（ITEM-007）、NV12→BGR 转换（ITEM-005）、显示/推理/录像消费路径（ITEM-008+）、`colorimetry` 归一正确性依赖 caps 值（本轮 caps 无该字段，Design 允许 unknown）。
- 观察项 O-1（交实现参考，非缺陷）：test 源 3 消费者时 `max_use_count` 在不同轮次为 2..4（store 持有最新帧 + 主循环再取一次），与纯消费者数不恒等；语义符合实现（use_count 含 store 自身），无安全影响，仅提示后续性能仪器（ITEM-006/007）不应把 use_count 当消费者数。
- 观察项 O-2（非缺陷）：`--consumers 0` 时主循环照常泵帧且 store 仍持有最新帧（published=27、final_not_ready=false）；零消费者是探针的合法配置而非错误，记录为行为面。
- 验证侧过程记录：v1 夹具两处构造错（D 编译 include 路径含多余 `app/` 前缀；E 把 SSH 凭据=用户名的文件级同现当假阳性）在上板第一轮暴露，修正后复跑全绿；无产品缺陷误报。

## 6. 复现

```bash
python docs/video/test_evidence/ITEM-004/_verify_run.py          # 独立复跑（需板端凭据 env）
python docs/video/test_evidence/ITEM-004/_verify_ext.py          # EXT 夹具（需 --keep-remote 树在板）
```
