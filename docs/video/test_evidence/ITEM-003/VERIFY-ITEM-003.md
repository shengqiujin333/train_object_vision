# ITEM-003 帧身份与跨重连代际 — 独立软件验证 TestEvidence（VERIFY-20261017-v1）

- Artifact: TestEvidence（software_verification 独立复验）
- Capability: `software_tester.software_verification`
- 被验证实现: video_systems_engineer.video_system_implementation（执行证据 `ITEM-003-ENV-A0-B-20261017-r1.md`，板端 stamp `20261017-item003-r2`；实现提交 `cd15a73` 树）
- 被测版本核对: 板端构建使用本轮独立上传的 `app/` 工作区副本（183 文件，源文件集与交付 r2 一致）；板端产物 `build-media/tools/railhmi-stream-probe`
- 目标: compute_host 192.168.0.103（RK3588 / Ubuntu 22.04.5 / GStreamer 1.20.3 / aarch64）
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与交付 r2 一致）
- 验证日期: 2026-10-17; stamp `20261017-item003-verify-v3`（v1/v2 为过程中轮，判据构造缺陷见 §5）
- 验证范围: ITEM-003 工作项（帧身份与跨重连代际）与测试设计 TC-V-SEQ-01/02 判据；**不外推**真实相机断流重建（ITEM-014）、多路并发（ITEM-006/007）、录像回放

## 1. 验证方法与独立复现入口

独立复跑全部经交付运行入口 `app/tools/board_frame_identity_check.py`（build-core → core-tests → build-media → selftest → `--rebuild-after` 真实重建运行 → 取回证据）与验证侧自有扩展夹具：

```
python docs/video/test_evidence/ITEM-003/_verify_run.py --stamp 20261017-item003-verify-v3
python docs/video/test_evidence/ITEM-003/_verify_compare.py   # impl r2 vs verify v3 结构一致性
python docs/video/test_evidence/ITEM-003/_verify_ext.py       # EXT-1..9 + CLEANUP（10 项）
```

fresh-round 防陈树机制：`_verify_run.py` 先清场 `/tmp/railhmi_identity` 并写入含本轮随机 token 的 `core-evidence/verify-marker.txt`，runner 结束后读回核对（token_ok=True）；`_verify_ext.py` 的 EXT-1 再独立 cat 该 marker 核对构建树来自本轮 upload。凭据全程经 PowerShell 用户级环境变量导入本进程（不落盘、不打值）。

## 2. 结果总览：TEST_PASS

| 判据 | 结果 | 证据 |
|---|---|---|
| 独立板端全链构建复跑 | PASS（build-core/core-tests/build-media/selftest/identity-run 全 exit=0，elapsed=80.5s） | verify-v3 transcript |
| core 单测帧身份组 28 项 | PASS（698/698，`CORE_EXIT=0`；28 项身份 PASS 行与交付 r2 名单逐项相等） | C-3 比对 |
| 交付 selftest 23 项（含 7 身份项） | PASS（逐行与 r2 完全一致，12 判例行相等） | C-4 比对 |
| `--rebuild-after 2` 真实重建逐拍行为 | PASS（1→2 代际、seq 重置为 1、旧代际 stale_generation 判定行、session 复核一致性、summary live/generation=2） | verify-v3 log + EXT-2 |
| 与实现 r2 独立会话逐行结构一致 | PASS（C-1..C-4 全 True；瞬时值/会话 id 口径归一，见 §4） | `_verify_compare.py` 输出 |
| 边界: `--rebuild-after 0` 立即重建 | PASS（全部采样 gen=2、exit=0、final_state=live） | EXT-3 |
| 边界: `--rebuild-after 99 > duration` | PASS（全程无 rebuild 行、全部 gen=1、summary generation=1） | EXT-4 |
| 边界: 非法值 `--rebuild-after -5` | PASS（运行级等价"永不触发"路径：无 rebuild、全 gen=1、exit=0） | EXT-5 |
| 时间语义（三基分离） | PASS（wall_ms=UTC 毫秒且 2026-10-17 同日、mono last_frame_ms 不作 UTC、pts_ns∈[0,2^33) 且重建后从头计） | EXT-8 |
| 交付 selftest 身份 7 项独立性核对 | PASS（7 项独立 PASS/FAIL 判例行，非合并弱串） | EXT-6 |
| summary 关键域完整 | PASS（final_state/session_id/generation/frames_emitted/gate_admitted/gate_rejected 全在） | EXT-7 |
| 证据目录 23 文件凭据语义零泄漏 | PASS（3 组真实凭据 user-pw/password=/user@host 语义形态 0 命中；SSH 字面量仅以用户名 token 形态出现） | EXT-9 |
| 板端构建树清理 | PASS（`rm -rf /tmp/railhmi_identity` + `board_tree_cleaned` 读回确认） | CLEANUP |

## 3. 独立复跑重建运行逐拍观测（verify-v3，session_id=s1a14884f407-c00e9f-0）

| t(ms) | 事件 | generation | seq | wall_ms(UTC ms) | pts_ns | admit |
|---|---|---|---|---|---|---|
| 0 | stream_state | 1 | 1 | 1792218035244 | 16912514 | accepted |
| 1537 | stream_state | 1 | 40 | 1792218036804 | 1576912514 | accepted |
| 2017 | **rebuild** | **2** | — | — | — | — |
| 2017 | **identity_gate** `stale_old_generation=stale_generation old_gen=1 new_gen=2` | 2 | — | — | — | — |
| 2017 | stream_state | 2 | **1**（seq 重置） | 1792218037267 | 14857554 | accepted |
| 5519 | stream_state | 2 | 89 | 1792218040786 | 3534857554 | accepted |

summary：`final_state=live … generation=2 frames_emitted=152 gate_admitted=152 gate_rejected=0`。与交付 r2（151/151）的差异仅为 6 秒窗口内统计落拍数的必然瞬时差（C-1 比对该字段单独归一，见 §4）。

## 4. 与实现者归档（r2）的独立一致性比对

`_verify_compare.py` 对两轮**独立板端会话**（r2 vs verify-v3）比对，归一口径全部显式可复核：

- `session_id`：`make_session_id` 含时钟+计数，每次应用启动恒不同 → 归一；
- 逐秒 `t=/fps=/last_frame_ms/seq 槽/wall_ms/pts_ns`：跨会话瞬时值归一（结构不变），单位/来源/重置语义由 EXT-8 语义判据覆盖；
- summary 累积计数器 `frames_emitted/gate_admitted/gate_rejected`：墙钟窗口内落拍数的瞬时计数 → 归一（两轮 151/152 差即来源于此，非结构差异）；
- rebuild / identity_gate / summary 行 token 序列逐字比对。

结果 **COMPARISON_IDENTICAL=True**：C-1 逐行结构对齐（16=16 行）、C-2 语义批次一致（rebuild/gate/summary）、C-3 身份 28 项 PASS 名单相等、C-4 selftest 12 判例行相等。

## 5. 验证侧扩展夹具（自有判据，交付 selftest 未覆盖的边界）

| id | 刺激（判据先独立构造，再取实测） | 实测 | 结果 |
|---|---|---|---|
| EXT-1 | fresh-round marker: 本轮 token 在板端构建树（防历史缓存树冒充） | token_present=True | PASS |
| EXT-2 | 全新进程 `--rebuild-after 2` 全判据：pre gen1 / post gen2 / rebuild 行 / gate stale 行 / injection session_id=rebuild session_id / post 首 seq=1 / summary live+gen2 | 7/7 真，exit=0 | PASS |
| EXT-3 | 负边界 `--rebuild-after 0` 立即重建：全部采样 gen=2、exit=0、final_state=live | exit=0, 6 采样全 gen2 | PASS |
| EXT-4 | 正边界 `--rebuild-after 99 > duration 3`：无 rebuild、全 gen1、summary gen1 live | exit=0, no_rebuild | PASS |
| EXT-5 | 非法值 `--rebuild-after -5`：运行级等价"永不触发"（无 rebuild 行、全 gen1、exit=0） | exit=0 | PASS |
| EXT-6 | 交付 selftest 独立性：7 项身份判例行独立 PASS（`identity_*`/`gate_*` 逐项） | 7/7 PASS | PASS |
| EXT-7 | fresh summary 域完整性：6 个必需 token 全存在 | required 缺失=[] | PASS |
| EXT-8 | 时间语义：wall_ms 可转 UTC（2026-10-17 同日、单调、epoch 有界）、pts_ns∈[0,2^33) 且重建后首采样 < 重建前末端（从头计） | date=2026-10-17 06:21:39→06:21:45 UTC；pts_reset=True | PASS |
| EXT-9 | 本地证据目录 23 文件对 3 组真实凭据的凭据语义形态（user-pw/password=/user@host）扫描 | 0 命中 | PASS |
| CLEANUP | 板端构建树用后清理 | board_tree_cleaned | PASS |

夹具调试过程（透明记录，均为验证侧期望/判据构造错误，非产品缺陷）：

- v1 过程轮 EXT-2 把 `EXIT=` 哨兵值 0 直接放进 `all()` 聚合布尔——0 是成功却是 falsy，导致全真判据仍报 FAIL；退出码必须单独句柄（`pz == 0 and`）。
- v1 过程轮 EXT-8 重采样源误用轮内最后一份 `combined`（EXT-5 的负值运行，本就无重建段），pts_reset 判据在无 gen2 采样域内必失败；修正为固定取 EXT-2 的 `--rebuild-after 2` 输出重采样。
- v1→v2 过程轮 pts_reset 判据过严（未先要求重建前后各 ≥2 采样）；对比轮 v1→v3 把 summary 累积计数器归一补进归一规则（瞬时落拍数计，非结构域）。
- 离线 dry-run（`_verify_dryrun0.py`，`_verify_ext.py` 解析器跑本地归档日志）先于板端轮核对了全部判据域，防止夹具期望错变成假 FAIL/假 PASS。

## 6. TC-V-SEQ-01/02 判据映射

| TC | 判据 | 本轮证据 |
|---|---|---|
| TC-V-SEQ-01 | seq 在同一 (session,generation) 内严格递增自 1 起；重建归 1 | core-test 28 项独立复现（`seq starts at 1 and strictly increases`/`rebuild resets seq to 1 in new generation`）；板端逐拍 seq=1→40（gen1）→rebuild→1→89（gen2）；EXT-2 seq_reset 判据 |
| TC-V-SEQ-02 | generation 仅在真实重建时 +1；旧 generation/session 帧、重复/乱序 seq 被消费者明确拒绝 | core-test `begin_generation increments to 2`/`old generation rejected after rebuild`/`duplicate seq rejected`/`out-of-order seq rejected`/`different session rejected`；板端 identity_gate `stale_old_generation=stale_generation` 行；selftest `gate_stale_generation/gate_stale_session/gate_duplicate_seq/gate_out_of_order_seq` 项独立复现 |

时间戳契约（接口 §4）：`capture_mono_us` 单调接收时刻不作 UTC 显示、`capture_wall_ms` 为 UTC 毫秒（未校准为 -1）、`pts_ns` 为源 PTS 纳秒空间的值域由 core 单测 + EXT-8 板端语义复核。

## 7. 观察项（不阻塞 TEST_PASS）

- **O-1（run 级帧落拍计数瞬时差）**：两独立轮 6 秒窗口 frames_emitted 151/152 属于每秒 tick 的墙钟取整差，counter 域按口径归一（§4），非结构差异，不判缺陷。
- **O-2（--rebuild-after 非法文本未在解析期报错）**：`atoll` 对非法/负文本归 0 或负值，运行级行为是"永不重建"（EXT-5 实证），工具入口自身退出码正常。属 CLI 参数校验的宽进约定；本验证按运行行为判据不判缺陷，如需解析期报错可作为工具体检改进项交实现参考。

## 8. 结论

**TEST_PASS**：ITEM-003（帧身份与跨重连代际）在当前实现版本（工作项 ITEM-003 交付，基线 `cd15a73` 板端独立复现栈）下，TC-V-SEQ-01/02 软件侧判据全部通过：帧身份四元组 (session_id, camera_id, generation, seq) 全链生成、真实重建 generation+1 且 seq 归 1、旧代际/旧 session/重复/乱序被消费者明确拒绝、三种时间基准单位与来源符合契约且语义相互分离。独立板端复跑与 impl r2 全部判据结构一致（COMPARISON_IDENTICAL=True），扩展判据 10/10 PASS。**本验证只证明信源逻辑与 test 源链路**；真实相机断流重建（ITEM-014）、多路并发身份隔离、录像回放不外推。

## 9. 原始证据索引（本目录）

| 文件 | 说明 |
|---|---|
| `identity-192_168_0_103-20261017-item003-verify-v3.transcript.json` / `.log` | 验证轮独立复跑全流程结构化/人类可读 transcript（build-core/core-tests/build-media/selftest/identity-run/下载） |
| `identity-…-verify-v3.core-evidence_identity-…-verify-v3.{log,json}` | 独立板端逐拍身份/时间/接纳运行 |
| `identity-ext-192_168_0_103-20261017-item003-verify-v1.json` | 验证侧 ext 夹具结构化结果（EXT-1..9+CLEANUP 10/10，针对 v3 轮） |
| `verify_state.json` | fresh-round token / runner exit / marker 读回状态 |
| `_verify_run.py` | 验证轮独立复现入口（预置 fresh-round marker + 包装交付 runner，可复现） |
| `_verify_compare.py` | impl r2 vs verify-v3 逐行结构一致性比对（归一口径显式可复核） |
| `_verify_ext.py` | 验证侧可复现扩展夹具（板端操作全部有限超时；凭据 env 导入；用后清理确认） |
| `_verify_dryrun0.py` | 离线 dry-run 夹具（本地归档日志驱动 ext 解析器与判据域预检） |
| `identity-…-r2.*` | 实现者交付归档（比对基线） |
| `identity-…-verify-v1/v2.*` | 过程轮证据（夹具调试历史，判据缺陷见 §5；最终判据轮为 v3+ext） |

## 10. 资源清理

- 板端 `/tmp/railhmi_identity`（验证轮构建树，前轮 `--keep-remote` 保留供 ext 用）: ext 收尾 `rm -rf` 并确认 `board_tree_cleaned`。
- 本机无遗留后台进程（runner/ext 后台任务均 completed）。
- 凭据全程不落盘、不打值；证据目录凭据语义形态 0 泄漏（EXT-9）。
