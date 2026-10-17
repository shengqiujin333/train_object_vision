# VERIFY-ITEM-015 — 时间基准与回放同步软件验证（TestEvidence）

- Artifact: TestEvidence（software_verification, 视频 ITEM-015）
- Capability: `software_tester.software_verification`（Node n013）
- 工作项: 视频 ITEM-015「时间基准与回放同步」——启动 `TimeBase.calibrate(wall,mono)` 与
  单调↔UTC 单次换算、未校准不得写 UTC 业务事件、按各路实际播放头统一 UTC 求 max−min、
  超 `replay_sync_threshold_ms`（默认 300，可配）显式提示、缺任一路有效时钟 `available=false`、
  seek 递增 request generation 并丢弃陈旧回调、定位未到达不提前显示已定位
- 验证版本（被测）: 实现 commit `8ce206a`（实现 stamp `20261017-item015-r2`，
  基线 `c288d04`；新增 replay_sync.h/cpp + timebase 守卫增量 + 探针 + 单测组）
- 验证执行标识: **VERIFY-20261017-v1**（板端 stamp `20261017-item015-verify-v1`）
- 判定: **TEST_PASS**
- 日期: 2026-10-17
- 判据来源: 任务书 ITEM-015 + 接口契约 §4（时钟/单位）/§8（IF-07 同步与 seek 语义）+
  `docs/video/VIDEO_SYSTEM_DESIGN.md` §5.2/§5.3/§8.3 + `docs/video/VIDEO_TEST_DESIGN.md`
  TC-V-PLY-02 + `config/thresholds.yaml` `replay_sync_threshold_ms=300`
- 验证范围声明: 板端 ENV-A0（core 纯逻辑单测 + 真实时钟探针）+ 验证侧自有黑盒夹具
  （链接交付 `librailhmi_core.a`）；**不含**生产 UI 时间轴渲染（桌面 UI 职责）、
  真实相机现场三联回放同步（ENV-D，另档）、ITEM-013 已独立验证的解码播放头行为复测

## 1. 验证方法（独立于实现方证据）

三层独立验证体系（验证侧自有夹具 `_verify_run.py`/`_verify_compare.py`/`_verify_ext.py`/
`_verify_vext.cpp`，沿 ITEM-014 先例模式）：

1. **独立复跑**（`_verify_run.py`）: 同进程 PowerShell 用户级凭据导入 → 独立调用交付入口
   `app/tools/board_timebase_sync_check.py --keep-remote`（不采信实现方 r2 归档）。
   8 步全 exit=0：build-core → core-tests（**879/0**，与实现轮逐行一致）→ selftest（9/9）→
   run-sync。host key 指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现轮一致）。
2. **独立解析比对**（`_verify_compare.py`, **50 checks 0 failed**）: fresh 复跑 vs 实现 r2 归档
   逐行逐字段对齐（真实墙钟/单调钟读数为波动域，`+1s` 语义独立重推导不比绝对值）；
   C1–C7 判据在 fresh 数据上独立重推导，不采信 acceptance 行；core-tests **879 PASS 行逐行
   identical**；replay_sync 组 22 个抽样判例名全在（"gen1 accepted" 两处出现合计 23 命中）；
   selftest 9 判例列表有序一致；JSON 汇总 8 项一致。
3. **扩展夹具**（`_verify_ext.py`, **23 checks 0 failed**）:
   - EX-A 独立参数轮 `replay_sync_threshold_ms=100`: config 回显 100、800ms 偏差按参数
     重推导超阈提示（prompt 含「阈值 100 ms」）、真实播放头 ≤100 in_sync、同参二跑
     语义行恒等（确定性）、JSON threshold 100；
   - CLI 负例: `--bogus-flag` exit2 + unknown arg；`--help` exit0 用法行；
   - EX-C 板端 g++ 直编验证侧自有夹具 `_verify_vext.cpp`（链接交付 `librailhmi_core.a`，
     **14 场景 48 判例 0 FAIL**，判例总数按夹具源码 `check(` 调用数独立统计）；
   - 凭据泄漏扫描 0 命中；板端 `/tmp/vt_*`、`/tmp/vext_timebase`、`config_vt`、`vext/` 清理
     确认 + `/tmp/railhmi_timebase` 收尾全清（`BOARD_CLEAN_DONE`）。

## 2. 全绿判据（fresh 复跑 + vext 夹具独立判定）

| 判据 | 证据 |
|---|---|
| 启动校准 + 单次线性换算（mono+1s → wall+1000ms，换算不叠加、幂等、往返稳定） | compare C1；vext v03 重复校准不叠加 / v04 幂等 / v05 负与大跨度线性 |
| 未校准守卫：`can_write_utc=false`、`try_to_wall_ms` 不写输出参数、nullptr 安全 | compare C2；vext v02 四判例 |
| 三路实际播放头 UTC 同步：available / in_sync / dev=10ms≤300 / API 无 offsets 参数 | compare C3；探针 `source=calibrated_monotonic_clock`（真实时钟连续读） |
| 超阈值显式提示：dev=800>300、in_sync=false、prompt 含「同步偏差 800 ms（阈值 300 ms）」 | compare C4；EX-A 参数轮阈值 100 下同形提示 |
| 可配阈值：fresh 默认 300 + EX-A 参数轮 100 行为行独立证明（config 回显/prompt/JSON） | compare C3/C7 + ext EX-A |
| 缺任一路有效时钟：available=false / MISSING_CLOCK / missing=CAM-03（多缺按锁定序） | compare C5；vext v07 多缺路两缺全报、未知键不污染 |
| seek 世代：gen 1→2 递增、陈旧回调丢弃且位置保持最后接受值、未达不提前 reached、达容差才 reached | compare C6；vext v09 容差半开边界（±tol/±1）/ v10 多代陈旧保持 / v11 同代最后接受值 |
| 单测逐行一致（879 PASS 列表 identical、`== 879 passed, 0 failed ==` 两轮同现） | compare core-tests 段 |
| selftest 9 判例两轮一致、0 FAIL | compare selftest 段 |
| 负例/确定性/凭据安全/清理 | ext NEG×2、DET（语义行恒等）、EX-D 0 命中、EX-E + 收尾清理 |

## 3. vext 夹具新增边界覆盖（交付探针与单测未测面，48/48 PASS）

阈值半开语义（dev==threshold in_sync / dev==threshold+1 not in_sync）· 守卫不写调用方
输出 · 重复校准覆盖参照点（重启语义，不叠加偏移）· 换算幂等 · 负 mono/负 wall/1e12µs
大跨度线性 · 空锁定列表 available=true（不谎报不可用）· 多缺路按锁定序报 missing ·
未知相机键不进偏差计算 · threshold=0 与负阈值保守语义 · seek 容差半开边界
（dev==±tol reached / +1 not）· 负容差归一 0 · 多代陈旧回调位置保持 · reset 归零 ·
同代多次回调最后接受值 · gen0 初始代接受语义（O-2）· 单路 dev=0 · 未校准优先于时钟齐备。

## 4. 观察项（交实现参考，非验收判据违例）

- **O-1**: 交付探针 acceptance 公式硬编码默认阈值 300（`timebase_sync_probe.cpp:178`
  `threshold == 300` 与 `threshold_from_config` 键名实际检查的是「等于 300」而非「等于配置值」）。
  非默认配置（EX-A 100）下探针行为行全部正确（config 回显/prompt/JSON 用 100），但
  acceptance 行自报 `result=FAIL`/`threshold_from_config=false`。EX-A 按交付契约（默认自检）
  断言该行 FAIL 为预期；阈值可配语义由行为行与 vext 夹具独立证明。建议实现方后续把
  acceptance 的 threshold 键改为「与配置回显一致」口径（不影响本轮判据）。
- **O-2**: `SeekController` 初始 generation 0 未 seek 时，gen0 回调按「当前代」接受
  （`replay_sync.cpp:60` 用 `!=` 比较），且默认 target=0/tol=1000 下落窗即 `reached=true`。
  无激活 seek 的 reached 属初始态语义；「定位未到达不提前显示已定位」验收针对 seek 后
  locating 阶段（v09/v10 覆盖全绿）。建议桥接消费方在未发起 seek 时不读 `reached`
  （或在首个 request_seek 前视为 idle），交实现参考。

## 5. 过程轮与教训（验证侧自负，无产品缺陷误报）

过程轮 vext 3 处夹具期望构造错 + 扫描/计数 2 处，全部自查修正：① `to_mono_us` 幂等写成
嵌套调用（第二次把 mono 当 wall 输入）；② 负 wall×1000 单位写错（−1ms 应为 −1000µs）；
③ v12 初始代语义预期写反（实际 `!=` 比较下 gen0 被接受）；④ EX-A 首版断言「acceptance
PASS with threshold=100」与交付默认自检契约冲突——离线复盘 `timebase_sync_probe.cpp`
源码确认 acceptance 公式硬编码 300，改按交付契约断言并登记 O-1；⑤ 判例计数 49→48 与
凭据扫描器自匹配（`password=` 正则文本 + 数字凭据子串命中夹具 `123456789LL` 字面量）——
判例计数改按夹具源码 `check(` 调用数统计、扫描器脚本与其结果 JSON 从语义形态扫描排除
（ITEM-002 同源口径）。`raw_kv` 前缀匹配 bug（`startswith` vs `in`）在上板运行前由
py_compile + 两次运行显式失败暴露后修正。

## 6. 平台与环境事实

- 目标: RK3588 `192.168.0.103`（指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，
  与实现轮一致）；Paramiko SSH（授权 `access.rk3588_inspect`/`rk3588_verify` 有效范围）。
- 板端构建: CoreTests 879/0（含 replay_sync 组）；`railhmi-timebase-sync` 探针。
- 板端资源边界: 仅写 `/tmp/railhmi_timebase` + `/tmp/vt_*` + `/tmp/vext_timebase`，
  ext 后收尾 `rm -rf /tmp/railhmi_timebase` 清理确认（BOARD_CLEAN_DONE）；无系统修改、
  无进程批量操作、不触碰相机。
- 波动域登记: `wall_ms`/`mono_us`（真实时钟读数，跨轮必然不同——按 `+1s`/roundtrip 语义
  独立重推导，不跨轮比绝对值）；`max_deviation_ms=10`（真实时钟 5ms×3 读的自然抖动窗内）。
- 实现方 r1→r2 调试两处（陈旧回调位置归零、selftest µs 换算期望值）均为实现侧修复且在
  fresh 复跑中确认有效（v10/v04 判例独立复现该语义）。

## 7. 结论

**TEST_PASS**。本项被测版本（commit `8ce206a`，stamp `20261017-item015-r2`）在板端
ENV-A0 范围内满足任务书全部用户可观察判据：

1. 启动校准与单调↔UTC 单次换算（幂等、不叠加、往返一致、真实时钟）。
2. 未校准不得写 UTC 业务事件（守卫显式拒绝且不写输出）。
3. 回放同步只消费各路实际播放头（UTC）求 max−min；超阈值（可配）显式提示；
   缺任一路有效时钟 available=false（MISSING_CLOCK）；未校准 available=false
   （UNCALIBRATED，优先于时钟齐备）。
4. seek 递增 request generation；陈旧回调丢弃且位置不变；定位未到达不提前显示已定位；
   达容差才 reached。
5. 阈值配置路径生效（EX-A 参数轮 100 独立证明）。

范围外（如实声明）：生产 UI 时间轴/游标渲染（桌面 UI）、真实相机现场三联回放（ENV-D）、
解码播放头行为（ITEM-013 已独立验证）。O-1/O-2 为交实现参考的观察项，不构成本轮判据违例。

## 8. 原始证据文件（本目录）

| 文件 | 内容 |
|---|---|
| `tb-192_168_0_103-20261017-item015-verify-v1.log` | 验证轮板端独立复跑全 transcript（upload/build-core/core-tests 879/selftest 9/run-sync/downloads） |
| `tb-192_168_0_103-20261017-item015-verify-v1.transcript.json` | 验证轮结构化 transcript（指纹/逐命令/退出码） |
| `tb-192_168_0_103-20261017-item015-verify-v1.core-evidence_timebase-sync-….log/.json` | fresh 校准/守卫/同步/seek/acceptance 原文 + 汇总 JSON |
| `tb-ext-192_168_0_103-20261017-item015-verify-v1.exa.log` | EX-A 参数轮（阈值 100）探针原文 |
| `tb-ext-192_168_0_103-20261017-item015-verify-v1.vext.log` | vext 夹具板端运行原文（48/48 PASS） |
| `tb-ext-192_168_0_103-20261017-item015-verify-v1.json` | ext 23 判定汇总 |
| `_verify_run.py` / `_verify_compare.py` / `_verify_ext.py` / `_verify_vext.cpp` / `_cleanup_board.py` | 验证侧工具（可复现） |
| `_verify_compare_result.json` | compare 50 判定明细 |
