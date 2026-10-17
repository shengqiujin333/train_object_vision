# TestEvidence — ITEM-017 预算测量装置与三类源分层留证（软件验证轮）

- 能力: `software_tester.software_verification`（ITEM-017）
- 验证 stamp: `20261018-item017-verify-v1`（VERIFY-20261018-v1）
- 实现主体: `docs/video/implementation/ITEM-017_MEASURE_INSTRUMENTS.md`
- 实现留证: `ITEM-017-ENV-A0-C-20261017-r1.md`（实现者 r3 记录）+ `measure-192_168_0_103-20261017-item017-r3.*`
- 板端: RK3588 `192.168.0.103`（指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现轮 transcript 一致）
- 结论: **TEST_PASS**（2026-10-18）

## 1. 判据（独立推导，不采信交付 type=acceptance 行为最终依据）

| # | 判据 | 独立断言方式 |
|---|---|---|
| J1 | 三类源分层留证（Simulation/File/Rtsp 三条记录各显式 source_class，不互替；RTSP 凭据缺失必须 to_be_executed 而非伪造值） | compare C2a..C2f + ext EXA 记录语义重判 |
| J2 | 吞吐装置真实采样（sim 20..30fps 带；display>0；CPU 0..100；RSS/temp>0；NPU 不可用时显式 -1 + note，禁止捏造） | compare C5a..C5f + ext |
| J3 | 端到端延迟四段分位（arrival/decode/convert/e2e）：键面完整、min≤p50≤p90≤p99≤max、p99(e2e)≤Σp99(段)+1 必要条件 | compare C3/C4 + preflight b8/b9 |
| J4 | 存储面实测（clip_bytes>0 且 188 粒度；bitrate=bytes·8/window ±5%；free≤total；file 复用同 clip） | compare C6a..C6e + ext 独立下载字节面复判 |
| J5 | 恢复与同步真实测量（recovery attempts≥1 且 last_recovery_ms∈(0,4000)；sync available=true，dev<300；file sync 值域可区分） | compare C7/C8 |
| J6 | 测量装置原语正确性（LatencyHistogram 最近秩分位/边界/负/大值/负样本/乱序 cap 语义；measure JSON 全嵌套/三态 token/归档结构） | vext 40 判例板端直编链接交付库独立断言 |
| J7 | CLI 契约（未知参数 exit2；--help exit0 + usage） | ext EXB |
| J8 | 稳定性（同参两轮 acceptance 键面恒等 + 三类 status/reason 结构 token 恒等；自然波动域豁免） | ext EXD |
| J9 | 凭据不落证据（SSH secret + 双相机凭据全证据文件扫描；admin 豁免为登记用户名 ITEM-002 先例口径） | ext EXE + preflight 泄漏面 |

## 2. 执行链（A. 独立跑过低炉链 → B. 三驱动板端轮 → C. 扩展夹具 → D. 语义预言机）

### 2.1 交付入口独立复跑（_verify17_run.py → app/tools/board_measure_check.py）

stamp `20261018-item017-verify-v1`，窗口 duration=8（与实现 r3 的 6s独立参数）：

```
host 192.168.0.103 fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU
mkdir/upload/build-core/core-tests/build-media/selftest/run-measure/download 全 exit=0
== 918 passed, 0 failed ==   （core 单测，含 metrics 域 = 交付 r3 918/918 同值）
RESULT: PASS passed=918 failed=0
SELFTEST_EXIT=0（railhmi-measure --selftest 5/5）
type=acceptance simulation_recorded=true file_recorded=true rtsp_to_be_executed=true
               three_source_classes=true latency_percentiles=true storage_measured=true
               recovery_measured=true sync_instrument=true result=PASS
```

产物：`measure-192_168_0_103-20261018-item017-verify-v1.{log,transcript.json}`、
`measure-192_168_0_103-20261018-item017-verify-v1.core-evidence_measure-…-v1.{log,json}`、
`measure-192_168_0_103-20261018-item017-verify-v1.core-evidence_measure_MEASURE_measure-clip.ts`。

新鲜窗口 sim 记录核心域（与 r3 值域一致，window 不同）：
- throughput: source/display fps 24.999809，cpu 86.999334，rss 60144 KB，temp 40.692，npu -1 + `unavailable:needs_privileged_rknpu_load`
- latency e2e: n=200，min 4520 / p50 16504 / p90 19074 / p99 21189 / max 21916 us
- storage: clip 2205428 B（188 粒度 ×11731），bitrate 2205411 bps
- sync: dev 6 ms；recovery: attempts 1, last 663 ms

### 2.2 板端 EXT 全链（_verify17_ext.py，31 checks 全 PASS）

`/tmp/railhmi_measure` 保留树（--keep-remote）之上：

- **EX-A** 独立第三窗口轮（`--duration 3` ≠ 交付 6s/验证 8s）：17 判例（三记录/sim recorded/file recorded/rtsp to_be_executed/CREDENTIAL_MISSING/窗口注入窗/吞吐带/CPU/188 粒度/bitrate 算术/sync<300/recovery 真实/在线 e2e 单调性/acceptance PASS）；**MPEG-TS 字节面独立下载校验**：SFTP 取回 `MEASURE_measure-clip.ts`，0x47 同步字节 全 segs==total，长度 == record clip_bytes（2 项）
- **EX-D** 同参稳定性对照（3s 第二轮 4 判例）：acceptance 键面恒等 / 两轮 PASS / 三类 status-reason 结构 token 恒等（数值自然波动豁免）
- **EX-B** CLI 负例（2）：`--bogus-flag` exit2；`--help` exit0 且含 `usage: railhmi-measure`
- **EX-C** vext 板端直编 2 判例：g++ 链接交付 `librailhmi_core.a` 构建成功 + `./vext17` **40 判例全 PASS**；PASS 行数 == 源码 `check(` 调用数 40（不手抄对账）
- **EX-E** 凭据泄漏扫描 2 判例：目录全证据产物（.md/.log/.json/.txt）对 SSH secret 与双相机凭据零命中；打码/admin 豁免口径同 ITEM-002
- 清理 1 判例：板端 `/tmp/railhmi_measure` + EXT 临时产物全删（`BOARD_CLEAN_DONE`）

vext 判例集（h01..h23 原语语义 + j01..j13 JSON/归档结构，判据独立推导，覆盖交付单测未断言的域）：
- 奇 n=7 逐点最近秩（p50/p90=ceil(6.3→7)/p99=max/bounds/越界与负 p 戒断/p25/p60 半点）
- 偶 n=8 rank=4 严格 + p12.5/p87.5 半点 rank
- **乱序 cap-4 语义**：`add` 保持有序数组 + erase(begin()) → 丢当前最小值而非最新插入（交付单测仅覆盖单调 drop-oldest 域；本夹具反向域断言 window={4000,6000,7000,8000}, p50=6000/p90=p99=8000）
- 单调 cap-3 = drop-oldest 等价口径（回归）+ clear 全复位
- 恒值样本 / cap 内重复样本窗 / 1e9 大值 no-overflow 均值 / 负样本存取 / mean 连续 add 一致性
- measure JSON 全嵌套块（latency/sync/storage/recovery/window 6dp）+ 三类源 token × 三态 token 完整面（含 to_be_executed/not_applicable）/ 归档空串精确 / 双记录序 / 三记录条数

产物：`measure-ext-192_168_0_103-…-v1.{log,transcript.json,checks.json}`。

### 2.3 主链（_verify17_run.py 之外）语义预言机（_verify17_compare.py，双归档 79 判例全 PASS）

对实现 r3 与新鲜 verify-v1 归档各自独立重推语义（两轮结果域算子×归档参数独立重判，不阅读 acceptance 行）：
- C1 归档 schema r3+fresh（finite；records=3；NaN-free）
- C2 三类源分层 + rtsp `to_be_executed`/`CREDENTIAL_MISSING` + rtsp 全零域（不伪造）(r3+fresh ×12)
- C3 四段直方图不变量（键面完整；min≤p50≤p90≤p99≤max；sim+file ×8)
- C4 p99(e2e) ≤ Σp99(三段)+1 必要条件 (r3+fresh ×2)
- C5 吞吐带（sim 20..30fps / display>0 / cpu / rss / temp / npu 显式 -1+note / file fps≥sim 低边 = 非实时快进）(r3+fresh ×7)
- C6 存储算术（clip>0 / 188 粒度 / bitrate=bytes·8/window ±5% / free≤total / file 复用同 clip）(r3+fresh ×5)
- C7 sync（dev<300 / in_sync / file sync 可区分 false）/ sim attempts≥1 + last∈(0,4000) / file 值域未接触 (r3+fresh ×4)
- C9 双轮 acceptance 行键集恒等 + fresh 9 键全 true（10 判例）

留痕：`verify17-compare-v1.log`。

### 2.4 离线自检（_selfcheck17_preflight.py，23/23 PASS）

三驱动 py_compile / 预言机 11 项在 r3 存档上板前预热 / vext 判例名与 `check(` 调用对账 + 花括号深度平衡（字符串字面量剥离后，防 ITEM-019 漏闭包同病）/ 三脚本 stamp 一致性。

## 3. 与实现留证对账（值域对比）

| 域 | 实现 r3 (6s 窗) | verify-v1 fresh (8s 窗) | EX-A (3s 窗) | 结论 |
|---|---|---|---|---|
| core 单测 | 918 passed/0 | 918/0 | — | 一致 |
| selftest | 5/5 | 5/5（SELFTEST_EXIT=0） | — | 一致 |
| sim fps | 24.995 | 24.9998 | band 20..30 | 一致（实时带） |
| sim e2e p50/p99 us | 10860/21415 | 16504/21189 | >0 单调 | 同域（物理波动） |
| clip (B) | 1,668,500 | 2,205,428 (188×11731) | 188 粒度 | 均真 MPEG-TS，窗长成正比 |
| bitrate | 2,224,243 | 2,205,411 | 算术±5% | 一致 |
| sync dev | 6 ms | 6 ms | <300 | 一致 |
| recovery | 663 ms / 1 attempt | 663 ms / 1 attempt | real | 一致 |
| file fps | 74.99 | 99.98（非实时快进） | >sim | 一致口径 |
| rtsp | to_be_executed/CREDENTIAL_MISSING | 同 | 同 | fix-typed（不伪造） |

fingerprint 与实现轮一致（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）。

## 4. 结果与观察项

- **判据 J1..J9 全 PASS** → `software_verification = TEST_PASS`
- 独立口径：metrics 原语乱序 cap 窗口“丢当前最小值”语义已由验证侧 vext 反向域断言（h15..h17），与实现注释的单调口径互补不冲突
- O-1：EX-A 首轮 SFTP 绝对路径 `REMOTE + "/tmp/…”` 与 EXP 上写坐标不匹配致 ENOENT 中断 → 修正为绝对 `/tmp/...` 后全链 31/31（夹具修正已随日志留痕，属验证夹具缺陷非交付缺陷）
- O-2：NPU 占用恒为 -1 + `unavailable:needs_privileged_rknpu_load`（需特权接口，属装置已知边界非伪造）；文件源 cpu_pct 96.98 属满载解码快进域，与 sim 87.0 同批不属伪造
- O-3：三窗口（6/8/3 s）× 2 独立驱动 × vext 40 + 预言机 79 判据，三源分层留证与消耗面在多参数下均独立可复现
- 板端树、贡献证据目录（_tmp81 临时脚本/`_tmp_exa_clip.ts`/`__pycache__`）均于收尾清理；main 工作树留待 Runtime 交接 Commit
