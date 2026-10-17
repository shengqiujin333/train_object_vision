# VERIFY-ITEM-014 — 异常恢复控制器软件验证（TestEvidence）

- Artifact: TestEvidence（software_verification, 视频 ITEM-014）
- Capability: `software_tester.software_verification`（Node n013）
- 工作项: 视频 ITEM-014「异常恢复控制器」——真实会话重建（置 NULL→释放→重建）、退避后执行、
  每次实际尝试只计一次且保留失败链、恢复由真实新帧确认、AUTH_FAILED/CREDENTIAL_MISSING
  不自动重试、FORMAT/UNSUPPORTED 仅能力/输入变化后重试、ONVIF 看门狗受控默认关闭、单路隔离
- 验证版本（被测）: 实现 commit `09b6a1e`（实现 stamp `20261017-item014-r2`，基线 `9f0e784` + ITEM-014 增量）
- 验证执行标识: **VERIFY-20261017-v1**（板端 stamp `20261017-item014-verify-v1`）
- 判定: **TEST_FAIL**（1 项业务缺陷 D-01 实证；其余判据全绿）
- 日期: 2026-10-17
- 判据来源: 任务书 ITEM-014 + 接口契约 D-02（有界重建、实际 attempt 与状态一致）+
  `docs/video/VIDEO_SYSTEM_DESIGN.md` §6.2/§6.4 + `docs/video/VIDEO_TEST_DESIGN.md`
  TC-V-STATE-02/03 + ITEM-007（隔离）/ITEM-014 自有判据
- 验证范围声明: 板端 test 源三路/单路 + 板端 g++ 链接交付库黑盒驱动；**不含**真实相机 ENV-D
  断流取证、ONVIF 协议行为、CPU/RGA 资源（分别属预定边界/其他 ITEM）

## 1. 验证方法（独立于实现方证据）

1. **独立复跑**（`_verify_run.py`）: 同进程 PowerShell 用户级凭据导入 → 独立调用交付入口
   `app/tools/board_recovery_check.py --keep-remote`（不采信实现方 r2 归档）。
   8 步全 exit=0：build-core → core-tests（857/0）→ build-media → selftest（10/10）→ run-recovery。
2. **独立解析比对**（`_verify_compare.py`, 30 checks 0 failed）: fresh 复跑 vs 实现 r2 归档
   逐行/逐字段对齐（recovery_latency_ms 与 timing 行为调度抖动域按存在性归一；语义判据
   C1–C8 在 fresh 数据上独立重推导，不采信 acceptance 行）。
3. **扩展夹具**（`_verify_ext.py`, 24 checks 0 failed）:
   - EX-A 独立参数轮 `reconnect_backoff_ms=300`: 退避/delay/能力门控/acceptance 按参数独立重推导；
   - CLI 负例: `--bogus-flag` exit2 + `--help` exit0 用法行；
   - 确定性: 默认参数二跑语义行恒等（timing 除外）+ JSON `acceptance:true`；
   - EX-C 板端 g++ 直编验证侧自有夹具 `_verify_vext.cpp`（链接交付 core/media/platform 三层
     `.a`，黑盒驱动 MultiStreamManager+RecoveryController，8 场景 26 判例）；
   - 凭据泄漏扫描 0 命中；临时产物清理确认。
4. 判据协议: g02 两条 + g03b 一条为**缺陷呈证判据**（缺陷在位时必 FAIL，修复轮转 PASS）；
   其余 23 条为回归判据（必须全 PASS）。判定不因其余全绿而翻转 D-01 缺陷。

## 2. 全绿判据（交付在位、复跑一致）

| 判据 | 证据 |
|---|---|
| 断流→退避→真实重建（generation+1）→真帧确认（非 PLAYING） | fresh 复跑 CAM-01 `attempts=1 recovered_generation=2 latency=200ms`；vext g01 |
| 退避遵守（默认 1000ms：rebuild−cut≥950；参数轮 300ms：≥250） | compare C2；EX-A |
| 每次尝试只计一次 + 失败链峰值保留 | compare C1；core-tests 857/0（recovery_policy 组）；vext g01 |
| AUTH_FAILED/CREDENTIAL_MISSING 停止、attempts=0、generation 不变 | fresh 复跑 CAM-02；vext g05 |
| FORMAT/UNSUPPORTED 门控：变化前不重建、变化后恰一次重建并真帧确认 | fresh 复跑 CAM-03 `rebuild3=2532≥change=2500`；EX-A；vext g06 |
| 单路隔离（其余路 generation 不变且持续出帧） | compare C5；vext g08 |
| ONVIF 默认 disabled；启用未配置 `unavailable:UNCONFIGURED` 不动作；已配置 `unsupported:no_protocol` | compare C6/C7；vext g07 三态 |
| 重建后持续故障下按退避继续有限重试（设计 §6.2） | vext g04：连续故障 attempts 1→2, chain9→`MAX_ATTEMPTS` 有界停止（非冻结） |
| core 单测逐行一致（857 PASS 行） + selftest 10 判例一致 | compare（PASS 列表 identical） |
| 确定性复跑/凭据安全/负例 | EX-A/DET/NEG/EX-D |

过程轮夹具教训（验证侧自查，非产品缺陷）: vext v1 的 g04 期望构造错（未先驱动出 awaiting 就断言）、
负例断言 `endswith` 忽略 rrun 的 stderr 追加、PASS 计数错位；v2 全部修正后上板复跑。

## 3. 业务缺陷（交实现）

### D-01（P2 — 恢复后同文本错误不再触发重建，单路失去自动恢复能力）

- **现象**（板端实证, vext g02/g03b, stamp 20261017-item014-verify-v1, commit 09b6a1e）:
  单路断流→退避重建→真帧恢复成功后，**再次发生完全相同文本**的断流
  （test 源固定 "simulated stream cut (FI-01)"），控制器第二次 `attempts` 保持 1、
  generation 2→2，**永不重建**。复现窗 4s ≫ 退避窗（200ms 配置）+ 2 重建周期。
- **根因**（源码级, `app/src/media/src/recovery_controller.cpp:23`）:
  `tick()` 以 `s.last_error != last_seen_error_` 作新错误判定（字符串等值去重），而恢复确认
  路径（`on_recovered` 后）不清除/失效 `last_seen_error_`。恢复后重建已使 worker 清空
  last_error，但**下次同文本错误**被误判为已处理旧错误，分类被跳过，重建排程无法建立。
- **业务影响**: 真实 RTSP 断流错误（connection refused/EOS/timeout 等固定短语）在一次恢复后
  若再次以相同消息出现（网络抖动常态），该路自动恢复永久失效，只有异文本错误或应用重启才能
  再触发重建。任务书验收「断流后按退避重建并以新帧确认恢复」在第二次断流时字面失败；
  TC-V-STATE-01 六态全序的再次进入亦被阻断。
- **对照证据**: vext g03（异文本 freeze 首次注入正常分类重建）与 g03b（freeze 同文本第二次
  同样不重建）证明根因是文本等值去重而非 manager 状态残留；交付探针单次断流设计恰好
  绕开此路径（三路各注入一次），core 单测（纯策略层）也无法覆盖控制器轮询层。
- **判例形态**（修复轮验证复用）: vext g02 `attempts==2`、`g2==g1+1 且 recovered_generation==g2`、
  g03b `attempts==2` 三条断言在修复后应为 PASS；修复方向建议：恢复确认时失效
  `last_seen_error_`（如 clear 或记录消费世代），或改为 last_error 空转盖（resume/clear→
  误判消除）每次成功重建后重置——具体实现方案归实现能力，不改验证判据。
- **复现**: 板端 `/tmp/railhmi_recovery` 树（已按验证协议清理）rebuild 后按 `_verify_vext.cpp`
  g02 场景即可复现；本目录 `recov-ext-…verify-v1.vext.log` 第 7–10 行为原始输出。

## 4. 平台与环境事实

- 目标: RK3588 `192.168.0.103`（指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，
  与实现轮一致）；Paramiko SSH（授权 `access.rk3588_inspect`/`rk3588_verify` 有效范围）。
- 板端构建: CoreTests 857/0（包含 recovery_policy 组）；MediaCheck；`railhmi-recovery`。
- 板端资源边界: 仅写 `/tmp/railhmi_recovery` + `/tmp/vext_recovery` 等临时产物，运行后清理确认
  （EX-E）；无系统修改、无进程批量操作。
- 时序波动域登记: `recovery_latency_ms`（200–226ms 跨轮自然波动）、timing 行 1ms 抖动
  （tick 25ms 粒度）——按存在性判据，不作为跨轮字节级恒等判据（与 ITEM-012/013 同口径）。
- 观察项 O-1（非缺陷，交实现参考）: g03 场景 `simulate_freeze` 错误文本
  "simulated stream freeze (FI-02)" 未含分类关键字，被归为 `OTHER`（走退避重试路径）。
  分类器覆盖面与六态机 FROZEN 触发域的一致性由实现侧评估是否需扩关键字（本项目只要求
  不把未登记错误当 NONE 丢弃——当前为 OTHER 已覆盖）。
- 观察项 O-2（非缺陷）: vext g04 中 controller 在 awaiting 期间继续消费 worker last_error
  分类（`on_error` 计板 failure_chain），使 `MAX_ATTEMPTS` 提前达成并保持 `awaiting_recovery=true`
  与 `stopped=true` 并存——停止语义一致（不再 Rebuild），但恢复确认窗口被 stop 状态封顶。
  属策略参数耦合的边缘交互，非既有验收判据违例，登记参考。

## 5. 结论与移交

- 其余全部验收判据（10 项语义域 + 单测/探针/参数轮/负例/确定性/安全/清理）**PASS 且独立复现**。
- **D-01 为交付缺陷**，按能力契约交回 `video_systems_engineer.video_system_implementation`
  修复（`TOTAL` 失败域 3/26 判例，全部指向同一根因）；修复轮验证将复用本夹具并以
  三条呈证判据转 PASS + 全绿回归为通过门。不判定实现者能否修复。
- ENV-D 真实相机断流/鉴权现场取证仍为预定边界，修复轮后如需 ENV-D 另档另行安排。
