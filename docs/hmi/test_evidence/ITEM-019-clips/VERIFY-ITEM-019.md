# ITEM-019 验证报告 — 本地报警保护片段与缺口记录（software_verification 独立验证）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-019`
- 被测版本: commit `97aacc8`（`[capability:embedded_linux_engineer.desktop_app_implementation] … returned IMPLEMENTED`）
- 结论: **TEST_PASS**
- 验证方式: 经登记 Paramiko SSH 入口（192.168.0.103，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）独立复跑交付入口 `board_build.py --mode TestRun` + 验证方独立断言 + 验证方扩展夹具；主机（Windows）不做板端替代结论。

## 1. 验证范围与判据来源

任务用户可观察点（Runtime current task）：触发报警后保护片段存在且时长符合配置（前 30s/后 60s，可配置）；先写临时文件再原子重命名落盘；容量上限与保留策略来自配置并优先保留保护片段；任何录像中断写入缺口记录；事件与时间轴出现“录像不可用”标记而非静默。

判据来源不新增：`docs/hmi/DESKTOP_APP_DESIGN.md` §10（实现 B `ClipRecorder`：[t-30s,t+60s] 固化、容量/保留配置化、保护优先、先临时文件→原子重命名、缺口显式记录）、PLY-008（缺口斜纹/显式标记）、`INTERACTION_DESIGN.md` v2、TEST_DESIGN TC-SMOKE-10/TC-REC-02/TC-REC-03（写入中断归 FI-10 记录 + P3 呈现）。

## 2. 独立板端复跑（权威轮 VERIFY-20261016-v3）

### 2.1 交付入口 TestRun 复跑

`verify_clip19_fixtures.py` 以子进程方式重跑 `board_build.py --mode TestRun --keep-remote`（上传 app/ → build.sh CoreTests → railhmi-core-tests → tokens selftest → ui scan → sim-inject → status/ai/alarm/clip 探针 → 归档取回）。逐项 exit=0：

- 核心单测 **537/537（实现者 r2 与本轮 fresh log 的 [PASS] 序列逐行相同，identical=True）**；JUnit XML 独立解析 `tests=537 failures=0`；片段/缺口/保留域用例 56 条（clip 正则域 ≥30 阈值）。
- `railhmi-clip-probe` fresh 输出与实现者归档 r2 探针日志**逐行 byte-identical**（确定性复现）；本轮同机二次运行 `diff` IDENTICAL；`--help` exit=0、未知参数 `--nope` exit=2（非静默）。
- 回归留痕同轮在档：sim-inject（log+json，FI-10 gap 数字字段 `gap_count=1`/`gaps=1` 保留）、status-probe（maintainer+driver）、ai-probe、alarm-probe 全部 exit=0。

### 2.2 clip-probe 独立断言（不相信探针自述 acceptance 行，验证方自行解析）

对 fresh `clip-probe-VERIFY-20261016-v3.log` 独立解析，逐条判据：

- **J1 片段存在 + 时长 = 配置**：config `pre_s=30 post_s=60`；clip t0=30000/t1=120000（触发 60000 −30s/+60s）duration **90000ms**；post 窗结束 finalize `before=0 at=1`（119000 未完成 → 120000 完成），`state=finalized complete=true frames=2001`（滚动缓冲统计）✅
- **J2 原子写盘**：`type=clip-store dir_ok=true wrote=true exists=true path=/tmp/railhmi_clips/clip-1.json`；manifest 为完整 JSON 行（键集合 id/cam/level/trigger_ms/t0_ms/t1_ms/duration_ms/state/complete/frame_count 全在，`"state":"finalized" "complete":true "frame_count":2001`），FileStore::write_atomic 实现 `<path>.tmp.<pid>` 写入→`rename`（源码核验 file_store.cpp:52-70）✅
- **J3 配置容量 + 保护优先**：buffer 推导深度 2275 = (30+60)×25+25、实测 frames=2251 ≤ 容量；retention `evicted=1 continuous=1 critical=0 protected_remaining=1`（两段 8GiB 普通 + 1 保护，12GiB 上限，先淘汰普通最旧；保护片段保留）✅
- **J4 缺口显式记录**：两次中断 [200000,205000]+[204000,210000] 合并为单行 `t0=200000 t1=210000 reason=recording_interrupted` ✅
- **J5 “录像不可用”非静默呈现链**：缺口数据 → `RecordingGapRow` 为 P3 时间轴斜纹/事件 `record=unavailable` 标签的数据来源；同一 fresh 轮 alarm-probe 输出保留了 id2 行内 `[录像不可用]`、id3 `[录像不完整]` 行内标注（PLY-008），sim-inject FI-10 注入 `gap_count=1` 摘要链（ITEM-009 回归）✅

### 2.3 验证方扩展夹具（verify_clip19_ext.cpp，链本轮 fresh librailhmi_core.a + librailhmi_platform.a，板端 g++）

**57/57 通过（EXT_EXIT=0）**，覆盖交付测试/探针之外的可证伪边界：

- 配置化：非默认 pre=5/post=10 → duration 15s；buffer 深度推导 25×(pre+post)+25；显式 `max_frames_per_channel` 不被推导覆盖
- RollingBuffer：capacity 0 无界语义；`set_capacity` 收缩裁剪；`drop_before` 闭合边界（保留 t==阈值）；`count_in` 单点/内部/全闭区间/外部；未知相机 `buffer()==nullptr`
- 片段生命周期：无帧相机报警 finalize 后 frames=0；finalize 幂等（二次返回 0）；两片段 119999 未完成 → 120000（t1）完成 → 160000 完成下一片段，完成数单调不重复计数
- 自定义小窗（pre=2/post=3）：报警于 5000 → [3000,8000]；**推进到 t1==8000 的最后一帧把裁剪阈值恰好压到 t0，finalize 时全窗 51 帧仍在**；finalize 后继续推帧不改已固化片段的 frame_count
- 保留策略经 ClipRecorder（验证模型层状态联动）：3×5GiB 保护 + 12GiB 上限 → 仅淘汰最旧保护（critical=1），且 `clips_/segments_` 同步收缩、剩余为较新两条；2×8GiB 普通 + 1GiB 保护 + 10GiB 上限 → 仅淘汰最旧普通且普通行移除；期限淘汰（retention_days=1）只淘汰过期普通、不淘汰保护、fresh 普通保留；容量有余则零淘汰
- 缺口：t0/t1 反转自动交换；相邻（差 1ms）合并；新缺口**桥接**吸收两个本不相邻区间（0..10 + 20..30 被 5..25 合并为 0..30）；reason 取最先存在非空值、空 reason 被新值替换；跨相机分行并按 cam 排序
- manifest：finalize 前 `state=recording/complete=false`，finalize 后翻转；键集合与 level/cam 字符串逐项
- 原子写边界：覆盖写替换内容且**无 `.tmp.<pid>` 残留**；写于普通文件之下失败时目标不存在、err 非空、原文件内容不变

## 3. 结论与边界（与实现者 README §3 一致，验证方独立复核认可）

- **判据内全部通过**：保护片段固化时长/配置化、原子写盘、配置容量+保护优先、缺口合并记录的 core 层均有板端实测证据；用户可观察点中“片段存在且时长符合配置”与“中断后出现可用性标记而非静默”在核心模型与探针/回归链中得到证实。
- **实际视频编码/封包复用媒体层**（GStreamer 链路，属媒体/视频职责）；本项交付的保护片段为逻辑、时间范围、清单原子落盘与保留/缺口记录，视频文件接入点（`ProtectedClip::path`）就绪但未接真实编码器。清单时长为配置值（pre+post），**实际视频文件的媒体时长判据（TEST_DESIGN ffprobe 口径）待真实编码接入后生效**。
- **SQLite 段行/缺口行入库**复用 ITEM-007 `SqliteRepository`（板端 537 用例含 gap 落库、重启恢复、WAL），但应用进程尚未打开数据库，本项的持久化在 core 模型与清单文件层，数据库接线归后续集成项。
- UI 时间轴缺口的图形化渲染与事件/告警中心的 record_available 三态行内标注已由数据层与展示派生层双重证实（alarm-probe 同轮 [录像不可用]/[录像不完整] 行内标注，PLY-008），但 P3 回放页斜纹/时间轴本体属后续回放工作项，未在本次运行期证实。
- 探针写入 `/tmp/railhmi_clips/`（板端临时目录，非产品数据路径）。

## 4. 观察项（交实现参考，不改本次验收结论）

- **O-1 `frame_count` 的口径注记**：`finalize(now)` 的帧数统计依赖**当次缓冲现场**——缓冲按 `(pre+post)` 秒滚动裁剪，因此若实现侧在 `now >> t1`（缓冲已滚过窗口尾部）才调用 finalize，`frame_count` 会偏小而不影响 `complete=true` 的语义。建议后续集成时约定 finalize 在 post 窗结束时即调（或统计时以片段固定页 [t0,t1] 的独立保留段为准）。ext 用“推进到 t1 恰好完成”与“缓存滚动后补 finalize”两种形态确认了这两个口径的实测值。
- `on_frame` 裁剪阈值 `ts - (pre+post)*1000` 相对**最后一帧**自然滚动；依赖真实到达帧序列，长断流（无帧）不会裁剪，容量上限（fps 推导 2275）兜底。行为合理，登记知悉。

## 5. 过程轮次（按 Runtime loop 记录）

- v1：验证侧 `parse_clip_probe` 把 `type=config` 行剥成无键首 token 全部解析为空（采纳 ITEM-017 kv 保留原行教训已修）；cleanup 误把删除确认 `ls` 的 exit=2 当失败（验证侧误报；交付探针/单测/ext 在本轮已全绿，ext 57/57 已在板端实测）。
- v2：ext/主链全绿；独立断言仅剩一处解析映射笔误（`type=clip-store` → `store` 键）、cleanup 核对含未删除目录名——均为验证侧自查修正，交付无改动。
- v3：**权威轮，全部业务步骤 exit=0**：537/537 逐行一致 + probe 逐行 byte-identical + 独立断言 fails=[] + 二次运行 IDENTICAL + negatives exit2/0 + ext 57/57 + 回归留痕全在 + cleanup_ok=True。

## 6. 证据文件（本轮）

- 主记录: `VERIFY-FIXTURES-VERIFY-20261016-v3.log` / `.transcript.json`
- 板端 fresh: `build-TestRun-192_168_0_103-VERIFY-20261016-v3.core-evidence_core-tests-20261016-v3.{log,xml}`、`.clip-probe-…v3.log`、`.ai-probe-…v3.log`、`.alarm-probe-…v3.log`、`.sim-inject-…v3.{log,json}`、`.status-probe-…v3.log`、`.status-probe-driver-…v3.log`、`build-TestRun-…-VERIFY-20261016-v3.{log,transcript.json}`
- 过程轮: v1/v2 对应同名 `VERIFY-FIXTURES-*` 与 `build-TestRun-*-VERIFY-20261016-v1/v2.*`（保留失败轮原样）
- 验证工具（复现入口）: `run_verify_round.sh`、`verify_clip19_fixtures.py`、`verify_clip19_ext.cpp`
