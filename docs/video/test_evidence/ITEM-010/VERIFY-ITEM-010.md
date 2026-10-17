# ITEM-010 AI 帧派发边界 — 软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence
- Capability: `software_tester.software_verification`
- 被测交付: `video_systems_engineer.video_system_implementation` ITEM-010（commit `045ae4f`，印章 `20261017-item010-r2`）
  — `app/src/media/include/railhmi/media/ai_dispatcher.h`、`app/src/media/src/ai_dispatcher.cpp`、
  `app/src/media/tools/ai_dispatch_probe.cpp`、`app/tools/board_ai_dispatch_check.py`、`app/src/media/CMakeLists.txt`
- 日期: 2026-10-17；stamp `20261017-item010-verify-v1`
- 目标: compute_host `192.168.0.103`（RK3588 / aarch64 / GStreamer 1.20.3）；host key `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`
- 结论: **TEST_PASS**（媒体侧 AI 派发边界：选择 / 降频 / 有界丢最旧 / 可回联身份，全部独立判据成立）
- 覆盖范围: 媒体侧派发接口与真实租约链路；**不含**模型推理（本项交付契约明确不做，端到端推理时延归推理适配接线后的对应任务）

## 1. 独立复跑（不采信实现方 r2 归档）

交付入口 `app/tools/board_ai_dispatch_check.py --keep-remote` 独立复跑 6 步全 exit=0
（mkdir / upload 216 files / build-media / selftest / run-ai / download）：

- 步骤退出码: BUILD_MEDIA=0、SELFTEST=0、AI_EXIT=0（`ai-…-verify-v1.log`、`ai-…-verify-v1.transcript.json`）

## 2. 独立解析比对（`_verify_compare.py`，8/8 checks PASS）

实现方 r2 归档 vs 验证 fresh 复跑，判据全部独立重推导，不采信交付自身 acceptance 行：

| Check | 结果 |
|---|---|
| impl-r2 / verify-v1 各自独立语义（injection 回显 5/2/2.0、逐拍 depth≤2 与计数单调、summary 逐路 enq≈published/5、禁用路 enq=0 且 rejected>0、popped 身份域、acceptance 交叉） | PASS ×2 |
| tick 窗数对齐 12/12 | PASS |
| 逐拍语义序列 identical（2 窗 ≤1 帧采样边界抖动归类自然波动；无硬差异） | PASS |
| summary 六路逐行逐字段 identical（published=163、enq=32、drop=18、skip=131、rej=0/163） | PASS |
| acceptance 行逐字段 identical（六布尔 + PASS） | PASS |
| popped 结构域 identical（CAM-06 / cab_door / generation=1 / seq=145；session 与 capture 时刻自然波动） | PASS |
| 汇总 JSON 双轮逐字段 identical（interval=5, depth=2, total_enqueued=131, acceptance=true） | PASS |

## 3. EXT 扩展夹具（`_verify_ext.py`，6/6 PASS）

- **EX-A 独立参数轮**（`--interval 3 --depth 1 --consume-hz 0.4 --duration 4`，8 窗）:
  injection 回显 3/1 逐字段；逐拍 depth≤1、enq 单调；summary 逐路 enq≈published/3（逐行独立重算，
  六路并发 published 逐件 ±1 自然差）、禁用路 enq=0 rej>0、acceptance=PASS、AIX_EXIT=0
- **EX-B CLI 负例**: `--bogus-flag` → exit=2 + `unknown arg`；`--help` → exit=0 含 --selftest/--interval/--depth
- **EX-C 板端 g++ 直编自有 C++ 夹具 28/28**（`vext_ai_dispatch.cpp`：链接交付
  `ai_dispatcher.cpp`+`frame_lease.cpp`+librailhmi_core+librailhmi_platform，用 GStreamer NV12 样本经
  `FrameStore::publish` 走真实租约链路驱动 `AiFrameRouter`）：
  - 选择: v01 启用过滤 / v02 未知相机 off / v05 禁用路 11 帧全 rejected / v06 禁用路 enq=0 且 drop=0 /
    v28 禁用路 depth=0
  - 降频: v03 seq1..11 → enq=2（seq 5/10 命中 `core::frame_due`）精确、v04 skip=9、
    v21 interval=1 全帧直通 7/7、v23 interval=1 skip=0
  - 有界: v07 depth≤2、v08 未满容量时 drop=0 精确（2 入队不逐出）、v24 interval1 下 7 入队 depth4 → drop=3 逐命数、
    v25 depth 恰 4、v26 FIFO 逐出序 pop 得 seq 4,5,6,7
  - 身份回联: v11 FIFO seq 序、v12 session/camera、v13 generation（begin_generation 后 =1）、
    v14 profile 透传、v15 capture_mono_us 精确、v17 buffer_ref=-`fr-CAM-A-` 前缀回联键、
    v18 plane0 像素内容 Y=200 可读
  - 租约语义: v16 pop 后 f1 成为唯一持有者 use_count==1（转移非复制）、v19 双持有（外部副本 + 队列帧）use_count==2 共享受证
  - 边界: v09/v10 pop 空语义、v20 未知相机 pop=false、v27 空租约(NOT_READY) 静默不计数
- **EX-D 确定性**: EX-A 同参两轮（a1/d1）逐拍语义序列仅 1 窗 ≤1 帧采样边界抖动、无硬差异、两轮各自 recheck 全过
- **EX-E 凭据泄漏扫描**: 本目录 15 文件凭据语义形态 0 命中
- 板端清理: `rm -rf /tmp/railhmi_ai` 后 ls 确认 `BOARD_CLEAN_DONE`（构建树/vext 产物/日志全清）

## 4. 观察项（非缺陷，交实现方知悉）

- **O-1 汇总 JSON `total_enqueued=131` = summary 行 `enq=32×4=128` + 3**：json 在 `mgr.stop_all()` 前
  序列化，worker 停机窗口边界帧继续入队所致；双轮同值 131（结构行为一致），属停机时序口径，交实现方
  在接线推理适配器时注意 total_enqueued 与 summary 行的取值瞬差。
- **O-2 逐拍窗 ±1 帧采样抖动**：六路并发 test 源 25fps 下，跨轮逐拍计数在采样窗边界存在 ±1 帧相位差
  （复跑轮 2 窗、EXT 独立参数轮 1~3 窗），summary 终值恒等；自然波动，非缺陷。
- 测试设计中 FI-V-03（重连风暴）/FI-V-05 全场景的 generation 重建/stale 拒绝谓词归帧身份与多路隔离
  轮（ITEM-003/007 已验证）；本项派发边界以 `(session,generation,seq)` 透传 + buffer_ref 回联为证。

## 5. 结论

媒体侧 AI 派发接口在真实 GStreamer 租约链路下行为符合交付契约：仅 `ai_enabled && ai_profile!=none`
相机降频入队、左右两侧永不入队、队列有界丢最旧并逐命数计数、派发帧携带可回联身份与共享租约。
**TEST_PASS**（本地 commit `045ae4f` 版本；范围=媒体侧派发边界 + CLI/配置面，不含模型推理）。

## 6. 记录文件

| 文件 | 说明 |
|---|---|
| `ai-192_168_0_103-20261017-item010-verify-v1.log` / `.transcript.json` | 独立复跑全步骤原始 transcript |
| `ai-…verify-v1.core-evidence_ai-dispatch-….log` / `.json` | fresh 六路运行逐拍 + summary + acceptance 原始输出 |
| `_verify_compare.py` / `_verify_compare_result.json` | 独立解析比对脚本与结构化结果（8/8） |
| `_verify_ext.py` / `ai-ext-192_168_0_103-20261017-item010-verify-v1.json` | EXT 夹具脚本与结构化结果（6/6） |
| `_verify_run.py` | 复跑驱动（PowerShell 用户级凭据导入 → 同进程交付入口） |
