# TestEvidence — VERIFY-N012-OVERALL：n012 视频系统实现交付整体复验（软件验证轮）

- 能力: `software_tester.software_verification`（n012_software_tester_software_verification，整体交付轮）
- 验证 stamp: `20261021-n012-overall-v1`（VERIFY-20261021-n012-v1）
- 被验主体: `runtime.artifacts.delivered.n012_video_systems_engineer_video_system_implementation.implementation`
  → `docs/video/implementation/ITEM-017_MEASURE_INSTRUMENTS.md`（VideoSystemImplementation）
- 实现权威留证: `docs/video/test_evidence/ITEM-017/ITEM-017-ENV-A0-C-20261017-r1.md` + `measure-…-20261017-item017-r3.*`（实现 r3，6s 窗）
- 板端: RK3588 `192.168.0.103`（fingerprint `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现轮一致）
- 本轮基线 commit: `d8430d8` 后工作树（ITEM-017 实现基线 `195b370` → 现 `main`，其间视频线 ITEM-001..015 复核 + 视觉线 ITEM-001..013 落库约 +19.6k 行共享 core/media 改动）
- 结论: **TEST_PASS**（2026-10-20/21）

## 0. 为什么需要本轮（整体轮口径）

单轮 ITEM-017 验证（VERIFY-20261018-v1，TEST_PASS）证明的是**当时版本**（commit 195b370）的已测范围。此后共享 `core/`/`media/` 大量演进（视觉线视觉推理 13 项交付 + 视频线复核），本交付整体轮按「总体验证 ≠ 最后一次局部单测」要求，在**当前完整应用源树**上重验交付入口全链，证明组合层（新 CMake 注册的 6 个 media 源 + metrics + measure 探针）仍构建、装载、运行、留证。

**实现文件变更差分**（`git diff 195b370..HEAD`）：`metrics.h` / `metrics.cpp` / `measure_probe.cpp` / `board_measure_check.py` / `test_metrics.cpp` 五件全部 **零变更**（δ=0，逐文件 name-only diff 为空）→ 行为面与 r3 验证轮同源；本轮重点 therefore 是**新树组合层复跑**。

## 1. 判据（沿用 ITEM-017 验证轮已验证判据面 J1..J9，不改判据）

| # | 判据 | 本轮断言方式 | 结果 |
|---|---|---|---|
| J1 | 三类源分层留证（sim/file/rtsp 三条 `source_class` 记录互不替代；rtsp 缺授权如实 `to_be_executed`+`CREDENTIAL_MISSING`+零域不伪造） | compare C2a..C2f（r3+fresh 各 6）+ ext EX-A | PASS |
| J2 | 吞吐装置真实采样（sim fps∈[20,30] 带 / display>0 / cpu∈(0,100] / rss>0 / temp>0；NPU 显式 -1+note） | compare C5a..C5g ×2 | PASS |
| J3 | 端到端四段延迟：键面完整、min≤p50≤p90≤p99≤max 逐段、p99(e2e)≤Σp99(段)+1 | compare C3/C4（双归档×2） | PASS |
| J4 | 存储面实测（clip>0、188 粒度、bitrate=bytes·8/window±5%、free≤total、file 复用同 clip） | compare C6 + ext 字节面 SFTP 复判 | PASS |
| J5 | 恢复与同步实测（attempts≥1、last∈(50,4000)、sync dev<300、file 域显式区分） | compare C7/C8 ×2 | PASS |
| J6 | 测量装置原语正确性（LatencyHistogram 分位/边界/乱序 cap/负样本/1e9 大值；JSON 三态 token；归档结构） | vext 板端直编链接交付 `librailhmi_core.a` **40/40** | PASS |
| J7 | CLI 契约（未知参数 exit2；--help exit0+usage） | ext EX-B | PASS |
| J8 | 稳定性（同参两轮 acceptance 键面恒等 + 三类 status/reason 结构 token 恒等；数值自然波动豁免） | ext EX-D | PASS |
| J9 | 凭据不落证据（SSH secret + 双相机凭据全证据目录扫描；admin 登记用户名豁免） | ext EX-E | PASS |

## 2. 执行链（独立复跑 → 语义预言机 → EXT 夹具 → 收尾）

### 2.1 交付入口独立复跑（_verify_n012_run.py → app/tools/board_measure_check.py）

stamp `20261021-n012-overall-v1`，新鲜树 build（upload 340 文件 — 含视觉线新增全部源与描述器）：

```
host fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU（与实现轮一致）
mkdir/upload/build-core/core-tests/build-media/selftest/run-measure/download 全 exit=0
== 1452 passed, 0 failed ==   （core 单测；r3 权威轮为 918 —— 视觉线 +534 判例 INCLUDES metrics 组回归）
RESULT: PASS passed=1452 failed=0
SELFTEST_EXIT=0（railhmi-measure --selftest）
type=acceptance simulation_recorded=true file_recorded=true rtsp_to_be_executed=true
               three_source_classes=true latency_percentiles=true storage_measured=true
               recovery_measured=true sync_instrument=true result=PASS
MEASURE_EXIT=0
```

产物：`measure-192_168_0_103-20261021-n012-overall-v1.{log,transcript.json}` +
`…core-evidence_measure-…-v1.{log,json}` + `…core-evidence_measure_MEASURE_measure-clip.ts`（2,204,300 B 真实 MPEG-TS）。

新鲜 8s 窗 sim 记录核心域（与 r3 同域，物理波动）：
- throughput: source_fps 24.9954 / display 24.8705 / cpu 83.36 / rss 67468 KB / temp 41.615 / npu -1 + `unavailable:needs_privileged_rknpu_load`
- latency e2e: n=199，min 5827 / p50 15010 / p90 18900 / p99 21138 / max 21246 us
- storage: clip 2,204,300 B（188×11725），bitrate 2,203,897 bps；free 32.99 GB ≤ total 61.13 GB
- sync: available/in_sync，dev 7 ms；recovery: attempts 1，last 643 ms
- file 源: fps 99.95（非实时快进口径），e2e p50 90721 us

### 2.2 语义预言机（_verify_n012_compare.py，r3+fresh 双归档 78 判例 fails=0）

C1 schema/finite（schema=railhmi.measure.v1、records=3、NaN-free、NaN/Inf 递归检查全零）；
C2 三类分层 + rtsp `to_be_executed`/`CREDENTIAL_MISSING`/零域不伪造（r3+fresh 各 6）；
C3/C4 四段直方图不变量 + p99 可加必要条件（双记录×双归档）；
C5 吞吐带（sim 20..30 / display>0 / cpu / rss / temp / NPU 显式 / file 快进 ≥ sim 低边）；
C6 存储算术（188 粒度 / bitrate±5% / free≤total / file 复用同 clip）；
C7 同步（dev<300 / in_sync / file available=false 域区分）；
C8 恢复（attempts≥1 / last∈(50,4000) / file 未接触域显式 attempts=0/-1）；
C9 acceptance 键面恒等（r3↔fresh 键集 identical）+ fresh 9 键全 true。
留痕：`verify-n012-compare-v1.log`。

### 2.3 EXT 夹具轮（_verify_n012_ext.py，--keep-remote 保留树，31/31 fails=0）

- **EX-A** 第三窗独立参数轮（--duration 3）：17 判例（三记录/状态/窗口注入窗/吞吐带/CPU/188 粒度/bitrate 算术/sync<300/recovery 真实/e2e 单调/acceptance PASS）+ MPEG-TS 字节面 SFTP 独立下载校验（0x47 全同步 + 长度==JSON clip_bytes，2 判例）
- **EX-D** 同参稳定性（3s 第二轮）：acceptance 键面/结果 PASS/三类 status-reason 结构 token 恒等（3 判例）
- **EX-B** CLI 负例：`--bogus-flag` exit2；`--help` exit0+`usage: railhmi-measure`（2 判例）
- **EX-C** 板端 g++ 直编 `_verify_n012_vext.cpp` 链接交付库：VEXT_BUILD_OK + 40/40 判例全 PASS + PASS 行数==check() 调用数（3 判例；判据面与 VERIFY-20261018-v1 已验证夹具逐字节一致——乱序 cap-4、负样本、1e9 大值、三态 token、归档结构全断言，不改判据）
- **EX-E** 凭据泄漏扫描（SSH secret + front/side 相机凭据全目录 .md/.log/.json/.txt 零命中 + secrets 非退化，2 判例）
- 清理：`/tmp/railhmi_measure` + EX-A/EXD 产出 + vext 产物全删（BOARD_CLEAN_DONE）

产物：`measure-ext-…-20261021-n012-overall-v1.{log,transcript.json,checks.json}`。
过程轮留痕（验证侧夹具构造错，已修）：v1 ext 首跑因驱动仍引用 `_verify17_vext.cpp` 名 FileNotFoundError 退出——得分复用清单须与夹具名同步替换；修复后复跑 31/31。独立板端复跑首跑 driver ROOT 深度错（parents[4]→parents[3]）零板成本修复。

### 2.4 离线自检（_selfcheck_n012_preflight.py，23/23 PASS）

三驱动 py_compile / r3 预言机预热（b1..b11 全 PASS）/ vext 判例数-名称对账+花括号深度平衡（字符串字面量剥除后）/ 三脚本 stamp 一致性。
留痕：`preflight-n012.log`。

## 3. 与实现权威留证值域对账

| 域 | r3 (6s 窗) | 本轮 fresh (8s 窗) | EX-A (3s 窗) | 结论 |
|---|---|---|---|---|
| core 单测 | 918 passed/0 | **1452/0**（+534 视觉线判例，metrics 组仍全过） | — | 一致且增长 |
| selftest | 5/5 | 5/5 | — | 一致 |
| sim fps | 24.9952 | 24.9954 | band 20..30 | 同域 |
| sim e2e p50/p99 us | 10860/21415 | 15010/21138 | >0 单调 | 同域（物理波动） |
| clip (B) | 1,668,500 | 2,204,300（188×11725，SFTP 字节面独立复判） | 188 粒度 | 窗长正比、真 TS |
| bitrate | 2,224,244 | 2,203,897 | 算术±5% | 一致 |
| sync dev | 6 ms | 7 ms | <300 | 一致 |
| recovery | 663 ms / 1 | 643 ms / 1 | real | 一致 |
| file fps | 74.99 | 99.95 | >sim | 快进口径 |
| rtsp | to_be_executed/CREDENTIAL_MISSING | 同 | 同 | fix-typed |

## 4. 结果与观察项

- J1..J9 全 PASS（无 TEST_FAIL 域、无新增缺陷、无新判据）→ `software_verification = TEST_PASS`
- 覆盖范围声明：本轮 TEST_PASS 证明 n012 交付（预算测量装置与三类源分层留证）在**当前版本**（本轮工作树，含视觉线 13 项落库后的共享 core/media）上构建、运行、测量、留证可复现；真实 RTSP 记录仍按设计如实 `to_be_executed`（缺现场授权），不冒充通过——与交付文档 §6 边界一致，paste既是实验器评审证据也是本期缺口的固定欠账。
- O-1（延续 r3）：NPU 占用 -1 + `unavailable:needs_privileged_rknpu_load`（特权接口边界非伪造）；文件源 cpu 96/83 属快进满载域。
- O-2（登记参考）：整体轮核心单测量 918→1452 的增量全部来自视觉线独立交付，非本交付改动；metrics 组逐例保持于通过集内（JUnit/RESULT 双口径一致）。
- 板端树与本地临时产物收尾已清理；main 工作树留待 Runtime 交接 Commit。
