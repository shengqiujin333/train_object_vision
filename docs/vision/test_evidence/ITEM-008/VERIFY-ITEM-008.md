# ITEM-008 验证轮 — 跟踪与多帧确认协同（VERIFY-20261019-item008-v1）

- Capability: `software_tester.software_verification`
- 被验证实现: `computer_vision_engineer.vision_inference_implementation` r4（commit 018cef0，stamp `20261019-item008-r4`）
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-008`
- 日期: 2026-10-19
- 结果: **TEST_PASS**

## 1. 验证模式（与实现方轮次独立）

| 步骤 | 手段 | 基线 |
|---|---|---|
| V-RUN | 交付入口 `app/tools/board_vision_tracking.py` 独立复跑（`--keep-remote` 保留板端树），stamp `verify-20261019-item008-v1` | 全新板端会话：上传 307 文件→CoreTests 构建+测试→selftest→8 fixtures→config→取回 21 份产物 |
| V-CMP | 独立解析比对 `verify/verify_item008_compare.py`（26 判据，不采信交付 RESULT 行） | fresh vs 实现 r4 归档逐字段语义比对 + 独立重推导 |
| V-EXT | 验证自有扩展夹具 `verify/vext_item008.cpp` 板上 g++ 直编（链接 `build-core/src/core/librailhmi_core.a`），27 判例 + CLI 负例 + 确定性 + 泄漏扫描 + 清理，驱动 `verify/verify_item008_vext.py` | 断言 core-tests/selftest/fixtures 未覆盖的跟踪边界 |

环境: RK3588 192.168.0.103（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，与实现 r4 同板同指纹），Paramiko 登记 SSH 入口，凭据经 `TRAIN_RK3588_SSH_CREDENTIAL`（用户级导入本进程，不落日志）。

## 2. 独立复跑结果（fresh vs 实现 r4）

| 域 | 判据 | 实测 |
|---|---|---|
| T1 退出码 | 真实退出码从 `*_EXIT` token 解析（外层 shell 恒 0） | build-core(BUILD)/core-tests(TEST)/selftest(SELFTEST+SELFTEST_JSON)/8×fixture(FIX_*)/config(CONFIG) 全 0；`--keep-remote` 无 cleanup 步骤；21 份产物取回 |
| T2 逐行恒等 | selftest/8 fixtures/config 共 10 份 `.log` fresh vs impl r4 | 10/10 全文逐字节恒等；log 与 `.json` 两输出面逐行恒等 |
| T3 core 单测 | JUnit 独立解析（`\sname=` 锚防 classname 子串匹配） | fresh `tests=1161 failures=0`；判例名多重集与 r4 恒等（1161=1161）；跟踪域（track/lightweight/camera-level/generation/null）47 判例 |
| T4 TC-T-TRK-01 | 同目标连续命中（独立重推导，不读 RESULT） | `same_target`：4 帧 `track=1` 连续、`events_total=0/0/1/1`（第 3 帧确认、第 4 帧不重复开事件） |
| T5 TC-T-TRK-03 | 多目标互不干扰 | `two_targets`：`events=2`（独立解析 events 行，非采信 RESULT） |
| T6 标签隔离 | 标签变化不串 id | `label_guard`：`ids=2`（同位置 obstacle 开新 id，不复用 track1） |
| T7 丢失淘汰 | 超 `max_misses` 删除 + 重现不复用 id | `miss_drop`：`dropped=1 new_id=2` |
| T8 TC-T-TRK-04 | generation 变化先 reset 跟踪器再映射、重新累计 | `generation_reset`：`tracker_resets=1 gen_keys=1 new_id=2 events=0/0/1`（新代第 1 帧不报警、第 3 帧才确认） |
| T9 相机级显式 -1 | 禁 0 冒充（CameraLevel 显式 null=-1） | `camera_level`：`policy=camera_level det_id=-1 events=1`；8 fixtures+selftest 全部行内无 `track=0`/`ids=0`/`det_id=0` 呈证 |
| T10 TC-T-TRK-02 | 跟踪不可用 null 丢弃（回归） | `envelope_only`：`det=0 no_track=1`、零事件 |
| T11 容量上限 | 超 `max_tracks` 显式 -1 不伪造 | `max_tracks`：`ids=1,-1 untracked=1` |
| T12 配置 | 阈值来源交叉核对 | `confirm_frames=3 confidence_min=0.5 suppress_window_ms=3000` 与 `app/config/thresholds.yaml` 逐项一致；配置阈值下 `same_target` 3 帧 1 事件 |
| T13 selftest 独立统计 | `[PASS]/[FAIL]` 行计数 + 判例名与探针源 `check(` 第二实参集合恒等（不手抄） | pass=23 fail=0；src=23 log=23 集合恒等（`RESULT: PASS failures=0`） |

比对脚本最终口径 `COMPARE SUMMARY passed=26/26`（退出码 0，归档 `verify/item008-compare-verify-20261019-item008-v1.json`）。

## 3. 验证自有扩展夹具（vext 27 判例，板上全 PASS）

CoreTests/selftest/fixtures 未覆盖的边界（`verify/vext_item008.cpp`，g01–g18 共 27 check 调用）：

- 匹配算法边界：g01 IoU 低于门限（0.25<0.9）时中心距回退命中（dist≈0.054≤0.3）；g02 `center_fallback=false` 时同位移不回退、新开 id；g03 回退受 `center_dist_max` 上限约束（0.054>0.05 → 新 id）
- 贪心顺序：g04 两 track 各自小幅移动保持 ids 1/2 不互换；g05 IoU 贪心先取最高对（track2↔IoU=1 的框），IoU≈0.667 次优对不抢配，配额耗尽后新框开 id 3（ids=3,2 与输入序一致）
- 生命周期边界：g06 `max_misses=0` 单帧未匹配即删除；g07 `tick_missing` 三帧计数 then 删除 (0/0/1) 后重现新 id 2；g08 帧间命中把 misses 清零（不复制累计）
- id 不变量：g09 每帧新位框逐帧新建，新 id 严格递增（last_id≥10）、`next_id_` 单调且从不产 0；g10 同帧三框（两重叠+一分离）各自独立 id 1/2/3（`created=3`）
- 时间鸿沟边界：g11 `ts_delta == max_gap_ms` 仍匹配（严格 `>` 才过期，id=1）；下一帧差 501>500 → 新 id 2
- 相机级合并语义：g12 `camera_level_track_id()==-1` 恒等且非 0；g13 CameraLevel 下同帧 person+obstacle 聚合为单一 -1 条（`aggregated=1`），3 帧 1 个 `track_id=-1` 事件（**验证确认实现声明的合并行为真实在场**，与文档 §5.2 声明一致）
- 适配器集成：g14 person/obstacle 同帧各自 track/type（PersonOnRail vs FrontObstacle 两类事件互不串）；g15 单适配器跨相机时 Detection 正确携带 `camera_id=CAM-02`（多相机须每路一个适配器的文档边界在场）
- 容量上限反向：g16 超 `max_tracks=2` 的第三个框显式 -1（saw_zero=0、无 0 冒充），下一帧重复出现仍显式 -1（`untracked=2`，无 id 泄漏）
- 抑制窗口：g17 同 target 连续 20 帧（2 秒内）恒 1 个事件（`suppress_window_ms=3000` 内无重复）
- 跨代 id：g18 适配器跨 generation 后 `next_id_` 不重置（gen1→2 后新 id=2，gen2→3 后 id=3），与 tracker `reset()` 语义互通

驱动判定（`vext-item008-…json`）：9/9 检查 PASS — V0 保留树在场、V1 板端 g++ 直编 1.8s、V2 板上 27/0 + 判例名集合与夹具源恒等（src=27 board=27）+ g01–g18 域全 present、V3 CLI 负例 unknown-arg=2/no-action=2/help=0/未知 fixture=2、V4 `--selftest` 与 `--fixture same_target` 同参两轮 sha256 恒等、V5 板端泄漏 0、V6 板端 `/tmp/railhmi_vision_item008_verify-20261019-item008-v1` 树与 vext 构件清理确认（`No such file` 即删除成功确认输出）。

## 4. 工作项要求逐条核对

| 任务书要求 | 验证证据 |
|---|---|
| 无专用跟踪模型时提供显式可验证的关联策略 | T4/T12 交付面复核；vext g01/g02/g03 匹配算法边界独立呈证（IoU 门限/回退开关/距离上限全可观察） |
| 使 RuleEngine 的 (camera,type,track_id) 抑制与多帧确认可用 | T4 `same_target` 4 帧单事件 + vext g17 抑制窗口 20 帧恒 1 事件（独立重推导） |
| 同一目标连续命中只更新同一事件、不重复开新事件 | T4 `events_total=0/0/1/1`、T13 selftest 独立统计 |
| 不同目标互不清除对方确认 | T5 `events=2` + vext g04/g10 多 track 独立 id、g14 两标签两类事件 |
| 跟踪不可用时明确 null 并走相机级适配路径 | T9 `det_id=-1`、T10 `envelope_only` 回归;vext g12/g13 显式 -1 非 0、合并语义在场 |
| 禁止把所有目标映射为 track_id=0 | T9 全行零 0 呈证、T11 上限显式 -1；vext g16 saw_zero=0、g12/g13 非 0 断言 |
| 重连（generation 变化）后旧目标状态被清理 | T8 `tracker_resets=1 gen_keys=1 events=0/0/1`;vext g18 跨代 id 连续不复用 |
| 标签变化不串 id / 丢失重现不复用 id | T6 `ids=2`、T7 `dropped=1 new_id=2`;vext g14 同帧双标签隔离、g07/g09 单调不复用 |

## 5. 观察项（不阻塞，交后续参考）

- O-1 实现方 r1 夹具 `in_roi=false` 无事件、r2 generation 顺序错误（先 adapt 后 reset）两轮返工件已由实现方保留并订正；本轮 r4 与提交 `018cef0` 一致性确认无残留（core-tests 1161/0 判例名多重集恒等、10 份日志逐行恒等）。
- O-2 `CameraLevel` 同帧多目标被 `by_track` 聚合合并为单 -1 条（vext g13 `aggregated=1` 一手证实）：与实现文档 §5.2 声明一致，但意味着 **同帧 person+obstacle 只报其中置信度优先的那个类型**；生产默认应为 `Lightweight`，若未来现场开启 CameraLevel 须知晓此行为（属显式文档声明边界，非缺陷）。
- O-3 中心距回退 g03 显示 `center_dist_max=0.05`（小于实现默认 0.25）会拒更近匹配——边界语义方向正确（<= 严格上限），现场调整该参数时注意双向退化：过小则漏匹配（新 id 增多）、过大则误合并相邻目标。
- O-4 探针 `label_guard`/`miss_drop`/`max_tracks` fixture 走独立 `LightweightTracker` 直驱动（不经过适配器），属交付层面合理拆分（跟踪器本体与适配器协同分层验证）；本轮 fixtures 与 vext 两条线互补覆盖。
- O-5 未实现外观/ReID：交叉行为 g05 显示贪心配额是不可回退的（次优对丢失 track 后直接新开 id），属单跟踪自由度边界；非本工作项缺陷。

## 6. 验证侧过程记录（自查修正，无产品缺陷误报）

1. V-CMP 自检（FRESH=IMPL 模式）两判据失真为验证侧预期差异：C1_keep_remote_no_cleanup（impl r4 用 `keep_remote=False`、存在 cleanup）和 camera_level `policy=` 正则锚写法（`^…$` 处理 type=config 行时失败，改 `\bpolicy=`）。实际 fresh 跑（`--keep-remote=true`）26/26 全绿。
2. V-EXT 首轮板面 26/27 + 名集漂移两个 vext 判据：g09 新建框仅作 0.001 微移导致 IoU≈0.91 与旧 track 复配、永不出新 id（验证侧构造错——`center_fallback=false` + 每帧 0.1 实距新位才强制新建）；g13 `check(…, // 行内注释` 让源扫描器停在首个顶层逗号后命中非 `"` 字符（掉出集合）——注释挪出调用行后 src=27。板上直编/直跑次轮 27/0 全绿（板上 g++ 直报比离线审查快，既有教训）。
3. V-CMP 上板前离线自查修正 C3 json/log 双面恒等口径首版对照式错误（jl.index 对重复行 O(n²) 且 min() 截断错误面）——改直接序列 `!=` 比较，避免一次无效上板（夹具预期先行自查纪律）。

## 7. 证据清单（本目录，`verify-20261019-item008-v1` 标记）

- `vision-item008-192_168_0_103-verify-20261019-item008-v1.transcript.json` / `.log` — 独立复跑结构化/人类可读 transcript
- 同名 `app_core-evidence_item008-*` 21 份 — core JUnit、selftest log/json、8×fixture log/json、config log/json
- `verify/verify_item008_run.py`、`verify/verify_item008_compare.py`、`verify/verify_item008_vext.py`、`verify/vext_item008.cpp`、`verify/item008-compare-verify-20261019-item008-v1.json` — 验证自有驱动/比对/扩展夹具/比对归档
- `vext-item008-verify-20261019-item008-v1.stdout.log` / `.json` — vext 板端判定归档

## 8. 覆盖边界（本轮 TEST_PASS 不声称的范围）

- 仅证明 ITEM-008 与本版本（commit 018cef0，r4）的已测范围：跟踪与多帧确认协同纯逻辑（夹具 envelope，无真实 RKNN 前向）。
- 未执行真实前向推理（无真实帧/检测输入）；未连接相机（ENV-D 未执行）；未测多路并发、长时间跟踪稳定性、真实目标运动下的 id 保持率；未实现线程/调度、P5 调试视图、测距——均为交付文档声明的边界（归 ITEM-009 及后续）。
- `LightweightTracker` 是标签+IoU 非外观跟踪：目标交叉或长时间遮挡可换 id（实现声明+O-3/O-5 观察在场）；主机侧仅静态审阅与 oracle dry-run，所有功能结论来自板端实测。
