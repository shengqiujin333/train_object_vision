# VERIFY-REPAIR-n008 — 独立软件验证记录（发布阻塞缺陷修复，complete_delivery 范围）

- Capability: `software_tester.software_verification`（Restlessdream Runtime，RESUME_SYNC）
- 被验证对象: 修复轮 `repair_n008_embedded_linux_engineer_desktop_release_implementation`
  （commit `450c43b`，stamp `20261017-repair-r1`(Qt 构建+冒烟) / `20261017-repair-r2`(板端 TestRun)），
  声明工件 `app/README.md`，实现证据 `docs/hmi/test_evidence/REPAIR-n008-release-qt/README.md`
- 验证范围: `complete_delivery`（repair_scope，replaces n006_software_tester_software_verification）
- 上游失败: `embedded_linux_engineer.desktop_release` WORK_FAILED
  （`docs/hmi/release/RELEASE-PACKAGE.md` §5 B-01/B-02/B-03: 目标 Qt 构建编译失败 / QML 装载失败）
- 验证轮: `VERIFY-20261017-v1`
- 结论: **TEST_PASS**（本范围 = 修复缺陷三个 + 当前版本完整交付回归；不含改判既有验收判据）

## 0. 版本锚定

| 项 | 值 |
|---|---|
| 被验证实现 commit | `450c43b`（修复轮 r1/r2 同一版本，本轮全部板端复跑读取**当前工作树** HEAD=450c43b） |
| `packaging/VERSION` | 0.1.0 |
| 先前交付验证锚点 | ITEM-025 v4 轮 670/0（commit ee0a741）；本轮核心变更完成后 vs 该基线的一致性见 §2 |
| 核心域变更 | `git diff ee0a741..450c43b` core/platform/media 仅 `stream_state.h/.cpp`（emit→emit_event 改名 + 内部调用 8 处） |

## 1. 修复内容核对（源码级，按 RELEASE-PACKAGE §5 修复边界逐条）

| 缺陷 | 修复声明确认（源码核对） |
|---|---|
| B-01 `emit` 与 Qt 宏冲突 | `stream_state.h:117` `void emit_event(...)`；`stream_state.cpp` 定义与内部 8 处调用全部改为 `emit_event`，语义仅改名不变（diff 见 commit 450c43b） |
| B-02 `main.qml` 未导入 pages | `main.qml` 头部新增 `import "pages"`（commit diff 行 4） |
| B-03 QML 大写属性名 | `design_tokens.yaml` `alarm.L1/L2/L3 → level1/level2/level3`（颜色/文案/编码不变）；`Theme.qml/Tokens.qml`、`tokens.json` 再生成；使用点 `P2AlarmCenter/P3Replay` 改 `Theme.alarm.level1..3`；手写别名 `L/R/G/M` 改小写（P1Realtime→layout、P3Replay→replay、P4AllCameras→gridCfg、P6Maintenance→maint、RecoveryOverlay→health），本轮全树正则反扫 `alarm\.L[123]`/`property var [LRGM]`/依赖大写名的引用 = 0 处残留 |
| 通用守卫 | 新增 `app/tools/check_qml_qt_validity.py`（B-01 头文件 Qt 关键字同类名 + B-03 大写 property/id）；`gen_tokens.py` 生成期拒绝大写令牌键 + selftest 反例断言 |

## 2. 独立板端复验（不信任实现方归档）

方法 = 本验证能力自有入口调用 + 当前源码的**立即复审**板端复跑；板端 RK3588 `192.168.0.103`，
SSH 指纹逐轮校验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（RejectPolicy），
凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL` 环境变量。

### R1 目标 Qt Release 构建 + QML 装载冒烟（决定性新增验证面）
- 入口: `python app/tools/board_release_build.py --host 192.168.0.103 --user admin
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008 --stamp 20261017-verify-v1
  --no-install-deps --no-bundle --build-timeout 900`
- 结果: 构建 `exit=0`，产物 `build-target/src/ui/railhmi` ELF **aarch64** 944016 B
  （sha256 `27f673104dd4c1bcb439e4cd5a3256ff77a37af7981aa9e0d2898518c08c08d5`）；
  offscreen 冒烟 `smoke_exit=124`（12 s 生存期 = QML 全装载 + 事件循环运行），
  无任何 QML 错误行（`configuration OK: 6 cameras` / `render path: cpu`）。
- 发布轮（WORK_FAILED 域）中以**同一入口**曾复现 B-01 编译失败 / B-02 B-03 装载 exit 3；
  本轮以**当前版本**在同一链路上得到相反结果 → 三个缺陷在该链路上缺席（非仅静态）。
- 证据: `release-build-192_168_0_103-20261017-verify-v1.json`（steps/smoke_exit_parsed=124/artifact sha256）、
  `smoke-20261017-verify-v1.log`。

### R2 板端全量回归 TestRun 独立复跑（stamp `20261017-verify-v1`）
- 入口: `python app/tools/board_build.py --mode TestRun --host ... --user admin
  --stamp 20261017-verify-v1 --outdir docs/hmi/test_evidence/VERIFY-REPAIR-n008 --keep-remote`
- `TESTRUN_EXIT=0`: core 单测 **670 passed / 0 failed**
  （`== 670 passed, 0 failed ==`，RESULT: PASS）。
- 其余 `TOKENS/SCAN/REPLAY_WIRE/AIDBG_WIRE/MAINT_WIRE/PERM_WIRE/HEALTH_WIRE/QML_VALID/PACK
  /SIM/STATUS/AI/ALARM/CLIP/REPLAY/MAINT/PERM/HEALTH/FAULT_EXIT = 0` 全绿（19 项）。
- 逐行比对（验证方独立解析，比较交集为 PASS 行）：
  - 本轮 core-tests `[PASS]` 行 670 行 vs 实现方 repair-r2 归档 670 行 **完全一致**（identical=True）。
  - 所有 14 份 core-evidence 归档与实现方 repair-r2 对应文件**逐字节一致**（sha256 相等，
    唯一例外 core-tests.log 差 1 行：junit 写入行的 stamp 字符串），包括 fault-probe、
    sim-inject、10 探针、JUnit XML —— 实现方 r2 归档可信，且复跑结果与它可重现一致。
- 证据: `build-TestRun-192_168_0_103-20261017-verify-v1.log` + 14 份
  `build-TestRun-...verify-v1.core-evidence_*`。

### R3 修复内容新守卫（主机侧独立复跑 + 反例）
- `check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include`
  → EXIT=0，扫 21 QML / 28 头文件。
- `gen_tokens.py --yaml docs/hmi/design_tokens.yaml --selftest` → 全 PASS
  （含 `[PASS] rejects upper-case token key`）。
- `check_no_hardcoded_ui.py --root app/src/ui` → PASS。
- 反例自检 `docs/hmi/test_evidence/REPAIR-n008-release-qt/negcheck_qml_qt_validity.py`
  （验证方独立复跑）→ `NEG_HEALTHY_EXIT=0 / NEG_B01_EXIT=1 / NEG_B03_EXIT=1 /
  NEGCHECK_RESULT=PASS`：证明新守卫对两类缺陷真实有效（判据不空转）。

### R4 板端 ext 夹具复跑（针对 rename 直接触碰的 stream_state 域）
- 驱动: `docs/hmi/test_evidence/ITEM-005-stream/verify_stream_fixtures.py`
  （验证方既有夹具，未修改），在看板上针对**本轮 TestRun 新构建**的
  `librailhmi_core.a` 源（`/tmp/railhmi_build`，--keep-remote 保留）编译运行
  `verify_stream_ext.cpp`：**40/40 passed, 0 failed**（GXX_EXIT=0, EXT_EXIT=0）。
- config-check 负例一并保留（fx1 timeout=0 拒 / fx2 反向 backoff 拒 / fx3 好参考过）。
- 意义: B-01 改名触碰的类在改名后其全部验收边界（阈值边界、升级链/事件、
  present-as-live 判窗、OFFLINE 粘滞）行为不变 —— 超出 core-tests 670/0 的
  验证方独立判域。
- 证据: `VERIFY-FIXTURES-20261017-verify-v1-ext.log` / `.transcript.json`
  （首次尝试因 Git-Bash MSYS 路径转换把 `/tmp/...` 改写为 Windows 路径导致 cd 失败，
  以 `MSYS_NO_PATHCONV=1` 重跑成功；属验证方工具环境问题，不涉目标）。

### R5 打包/部署契约回归（ITEM-025 修复内容不受 B-01..03 修复影响）
- `docs/hmi/test_evidence/ITEM-025-packaging/verify_packaging25_scan.py --app app --pkg packaging`
  （rc=0）: D-01/D-01b/D-02 = 全 True（修复缺陷缺席维持），
  `D02_parses_arg=True`（真实 parse+consume 路径）。
- `negcheck_packaging_fix.py`（实现方交付的交付检查器负控制）→ `0/1/1 PASS`。

## 3. 缺陷缺席证据（反向）

- 全树搜索 `alarm\.L[123]` / `property var [LRGM]` / `\.emit\(`（qml）:
  0 处命中（旧形态全部消失）。
- `Theme.alarm.level1..3` 使用点正常（P2AlarmCenter/P3Replay），报警级别数据字符串
  `"L1"/"L2"/"L3"` 与模型契约保持不变（既有 ITEM-018/020 断言在修复轮 TestRun 中全绿，
  无更动）。

## 4. 判定与边界

- **TEST_PASS 范围**: ① B-01/B-02/B-03 三类缺陷在当前版本缺席（目标构建/装载链路独立复证 +
  反例证守卫有效 + 反向搜索）；② 当前版本 complete_delivery 回归（板端 TestRun 全绿
  670/0 + 19 项 EXIT=0 + 与实现方 r2 归档逐行/逐字节一致）；③ 修复触碰域（stream_state）
  的验证方 ext 夹具 40/40。
- 未纳入本轮范围（与既有 ITEM 轮边界一致，不因本项扩展判据）:
  - 不在用户 X 会话/真实屏幕做人工视觉确认（offscreen 冒烟即本轮 UI 装载手工事实上界；
    视觉内容属用户确认范畴）。
  - 未执行 `systemctl enable`/整机重启自启（ENV-E 现场操作）。
  - 6 路真实 RTSP 摄像头取流、真实模型推理画面属取流/推理工作项，非本项范围。
- 实现方证据本轮仅在逐行/逐字节比对中作为信任锚点参与：核心断言均由验证方独立重跑与
  独立解析产生，非单方自检。

## 5. 复现

```sh
# 主机（离线）
python app/tools/check_qml_qt_validity.py --root app/src/ui --headers app/src/core/include app/src/platform/include
python app/tools/gen_tokens.py --yaml docs/hmi/design_tokens.yaml --selftest
python docs/hmi/test_evidence/REPAIR-n008-release-qt/negcheck_qml_qt_validity.py
python docs/hmi/test_evidence/ITEM-025-packaging/verify_packaging25_scan.py --app app --pkg packaging
python docs/hmi/test_evidence/ITEM-025-packaging/negcheck_packaging_fix.py

# 目标 Qt Release 构建 + 装载冒烟（约 2 分钟，依赖已就绪 --no-install-deps）
python app/tools/board_release_build.py --host 192.168.0.103 --user admin \
  --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008 --stamp 20261017-verify-v1 \
  --no-install-deps --no-bundle --build-timeout 900

# 板端全量回归（约 20 分钟，--keep-remote）
python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin \
  --stamp 20261017-verify-v1 --outdir docs/hmi/test_evidence/VERIFY-REPAIR-n008 --keep-remote

# 复触碰域 ext 夹具（看板上留存的 /tmp/railhmi_build）
# Windows Git-Bash 下须 MSYS_NO_PATHCONV=1 防路径改写
MSYS_NO_PATHCONV=1 python docs/hmi/test_evidence/ITEM-005-stream/verify_stream_fixtures.py \
  --host 192.168.0.103 --user admin \
  --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
  --cpp docs/hmi/test_evidence/ITEM-005-stream/verify_stream_ext.cpp \
  --outdir docs/hmi/test_evidence/VERIFY-REPAIR-n008 --stamp 20261017-verify-v1-ext \
  --remote-build-dir /tmp/railhmi_build --workdir /tmp/railhmi_verify_ext
```

## 6. 证据清单（本轮验证方生成）

- `VERIFY-REPAIR-n008.md`（本文件）
- `release-build-192_168_0_103-20261017-verify-v1.json` / `smoke-20261017-verify-v1.log`
  （R1 目标 Qt构建/装载冒烟）
- `build-TestRun-192_168_0_103-20261017-verify-v1.log` + 14 份 `core-evidence_*`
  （R2 TestRun 复跑归档）
- `VERIFY-FIXTURES-20261017-verify-v1-ext.log` / `.transcript.json`（R4 ext 夹具板端复跑）
