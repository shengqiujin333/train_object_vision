# VERIFY-ITEM-014 — 异常恢复控制器软件验证（TestEvidence，修复轮 v2）

- Artifact: TestEvidence（software_verification, 视频 ITEM-014）
- Capability: `software_tester.software_verification`（Node n013）
- 工作项: 视频 ITEM-014「异常恢复控制器」——真实会话重建（置 NULL→释放→重建）、退避后执行、
  每次实际尝试只计一次且保留失败链、恢复由真实新帧确认、AUTH_FAILED/CREDENTIAL_MISSING
  不自动重试、FORMAT/UNSUPPORTED 仅能力/输入变化后重试、ONVIF 看门狗受控默认关闭、单路隔离
- 验证版本（被测）: 实现 commit `be00256`（D-01 修复轮，实现 stamp `20261017-item014-r3`，
  基线 `09b6a1e` + recovery_controller generation 感知去重失效修复）
- 验证执行标识: **VERIFY-20261017-v2**（板端 stamp `20261017-item014-verify-v2`）
- 判定: **TEST_PASS**（v1 全部判据复现全绿 + v1 业务缺陷 D-01 的 3 条呈证判据修复后转 PASS）
- 日期: 2026-10-17
- 判据来源: 任务书 ITEM-014 + 接口契约 D-02（有界重建、实际 attempt 与状态一致）+
  `docs/video/VIDEO_SYSTEM_DESIGN.md` §6.2/§6.4 + `docs/video/VIDEO_TEST_DESIGN.md`
  TC-V-STATE-02/03 + ITEM-007（隔离）/ITEM-014 自有判据
- 验证范围声明: 板端 test 源三路/单路 + 板端 g++ 链接交付库黑盒驱动；**不含**真实相机 ENV-D
  断流取证、ONVIF 协议行为、CPU/RGA 资源（分别属预定边界/其他 ITEM）

## 1. 验证方法（独立于实现方证据）

与 v1 相同的三层独立验证体系（夹具 `_verify_run.py`/`_verify_compare.py`/`_verify_ext.py`
即为 v1 轮已归档验证工具的修复轮适配版，vext C++ 夹具 `_verify_vext.cpp` **逐字节复用，
未改判据本身**——翻转的只有缺陷呈证判据的预期结果协议）：

1. **独立复跑**（`_verify_run.py`）: 同进程 PowerShell 用户级凭据导入 → 独立调用交付入口
   `app/tools/board_recovery_check.py --keep-remote`（不采信实现方 r3 归档）。
   8 步全 exit=0：build-core → core-tests（857/0）→ build-media → selftest（10/10）→ run-recovery。
   host key 指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与历史轮一致）。
2. **独立解析比对**（`_verify_compare.py`, 33 checks 0 failed）: fresh 复跑 vs 实现 r3 归档
   逐行/逐字段对齐（recovery_latency_ms 与 timing 行为调度抖动域按存在性归一；语义判据
   C1–C8（含 v2 新增 C1b/C1c 二次重建判据）在 fresh 数据上独立重推导，不采信 acceptance 行）。
3. **扩展夹具**（`_verify_ext.py`, 24 checks 0 failed）:
   - EX-A 独立参数轮 `reconnect_backoff_ms=300`: 退避（首窗 + 二次断流窗均 ≥250ms）/
     delay/能力门控/acceptance 按参数独立重推导；CAM-01 attempts=2（二次同文本断流真实重建）；
   - CLI 负例: `--bogus-flag` exit2 + `--help` exit0 用法行；
   - 确定性: 300ms 参数轮 vs 1000ms 默认轮语义行相异（sanity）+ 默认轮 backoff/acceptance 复现
     + JSON `acceptance:true`；
   - EX-C 板端 g++ 直编验证侧自有夹具 `_verify_vext.cpp`（链接交付 core/media/platform 三层
     `.a`，黑盒驱动 MultiStreamManager+RecoveryController，8 场景 26 判例）；
   - 凭据泄漏扫描 0 命中；板端 EXT 临时产物清理确认（EX-E）。

## 2. D-01 修复验证（v1 业务缺陷 → 本轮核心）

- **v1 缺陷回顾**: `tick()` 以文本等值去重新错误判定；恢复确认后去重状态未失效 →
  恢复后同文本断流被当旧错误跳过，该路永不重建（v1 vext g02: attempts 恒 1、gen 2→2）。
- **实现修复**（commit be00256）: 代际变化（真实重建）使去重失效 + `last_error` 变空清除
  去重 + 恢复确认路径清除 `last_seen_error_`。
- **修复验证（板端实证）**:
  - vext **g02 二次同文本断流**: `gen=2->3 attempts=2 recovered_generation=3 recovered=true`
    —— v1 呈证判据 `g02 second cut attempts==2`、`g02 second cut rebuilt+confirmed` 转 PASS；
  - vext **g03b 同文本二次 freeze 对照**: `gen=2->3 attempts=2` —— 呈证判据转 PASS，
    同时证明修复对任意错误文本类别一致生效（非 NETWORK 特化）；
  - 交付探针 fresh 复跑 CAM-01: `attempts=2`、`generations cam01=1->3`、
    `rebuild2=2759ms`、`second_cut=true`（与 impl r3 `rebuild2=2785` 同形）；
  - 探针 `repeat_same_error_recovers=true`、`attempts_match_actual=true`（attempts=真实重建次数 2）。

## 3. 全绿判据（回归，26 vext 判例全 PASS + 交付面）

| 判据 | 证据 |
|---|---|
| 断流→退避→真实重建（generation+1）→真帧确认（非 PLAYING） | fresh 复跑 CAM-01 rebuild1=1304ms + vext g01 |
| **恢复后同文本二次断流再次退避重建并真帧确认（D-01 修复）** | vext g02 `gen 2→3 attempts=2`；g03b 同形态；fresh 探针 `rebuild2/repeat_same_error_recovers=true` |
| 退避遵守（默认 1000ms: rebuild1−cut≥950, rebuild2−rebuild1≥950；参数轮 300ms: 两窗 ≥250） | compare C2/C1c；EX-A |
| 每次尝试只计一次（attempts=重建次数）+ 失败链峰值保留 + 恢复后归零 | compare C1；fresh `attempts=2`=`rebuild1_count`；core-tests 857/0；vext g01/max_failure_chain=1 |
| AUTH_FAILED/CREDENTIAL_MISSING 停止、attempts=0、generation 不变 | fresh 复跑 CAM-02；vext g05 |
| FORMAT/UNSUPPORTED 门控：变化前不重建、变化后恰一次重建并真帧确认 | fresh 复跑 CAM-03 rebuild3=2533≥change=2500；EX-A；vext g06 |
| 单路隔离（其余路 generation 不变且持续出帧） | compare C5；vext g08 |
| ONVIF 默认 disabled；启用未配置 `unavailable:UNCONFIGURED` 不动作；已配置 `unsupported:no_protocol` | compare C6/C7；vext g07 三态 |
| 重建后持续故障下按退避继续有限重试（设计 §6.2） | vext g04: attempts 1→2, chain10→`MAX_ATTEMPTS` 有界停止（非冻结） |
| core 单测逐行一致（857 PASS 行） + selftest 10 判例一致 | compare（PASS 列表 identical） |
| 确定性复跑/凭据安全/负例/清理 | EX-A/DET/NEG/EX-D/EX-E |

## 4. 平台与环境事实

- 目标: RK3588 `192.168.0.103`（指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，
  与 v1/实现轮一致）；Paramiko SSH（授权 `access.rk3588_inspect`/`rk3588_verify` 有效范围）。
- 板端构建: CoreTests 857/0（含 recovery_policy 组）；MediaCheck；`railhmi-recovery`。
- 板端资源边界: 仅写 `/tmp/railhmi_recovery` + `/tmp/vt_*`、`/tmp/vext_recovery` 临时产物，
  运行后清理确认（EX-E）；无系统修改、无进程批量操作。
- 时序波动域登记: `recovery_latency_ms`（25–231ms 跨场景自然波动）、timing 行 1ms 抖动
  （tick 25ms 粒度）——按存在性判据，不作为跨轮字节级恒等判据（与 v1 同口径）。
- v1 观察项 O-2 复核: g04 本轮 `failure_chain=10 attempts=2 stop_reason=MAX_ATTEMPTS
  awaiting_recovery=true`——持续故障期 on_error 继续计链的耦合仍在（策略参数交互），
  维持「登记参考非缺陷」结论，停止语义有界且无排程冻结。
- 观察项 O-1（v1）: freeze 文本分类为 OTHER（走退避路径而非 FROZEN 态）——本轮 g03/g03b
  都经 OTHER 路径正常重建/再建，不构成既有验收判据违例，仍为交实现参考项。

## 5. 结论

- **D-01 修复有效**：3 条呈证判据全部转 PASS 且其余 23 条回归判据全绿（26/26），
  交付探针/参数轮/负例/确定性/单测一致/凭据安全/清理全过（compare 33/33, ext 24/24）。
- v1 范围声明不变：ENV-D 真实相机断流/鉴权现场取证仍为预定边界（另档）。
- 判定 **TEST_PASS**；本轮判定仅证明被测版本 commit `be00256` 在已测范围（板端 test 源
 ENV-A0/ENV-C）内的验收语义，不外推真实相机现场断流能力。
