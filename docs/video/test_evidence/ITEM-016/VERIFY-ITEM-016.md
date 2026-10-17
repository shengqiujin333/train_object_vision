# ITEM-016 视频诊断、能力探测与结构化日志 — 软件验证 TestEvidence

- Artifact: TestEvidence（软件验证产出的独立验证证据）
- Role-Capability: `software_tester.software_verification`
- 工作项: 视频任务 ITEM-016（诊断三态 / 结构化每路指标 / 整条脱敏 / 能力+版本独立读取 / 按日滚动结构化日志）
- 判据来源: `docs/video/VIDEO_TEST_DESIGN.md` G6（TC-V-LOG-01/02/03，ITEM-016 声明行）+ 交付
  `docs/video/implementation/ITEM-016_VIDEO_DIAGNOSTICS.md` §2 契约（12 字段/三态/四触发词/pipeline 原文整条替换）。
  `runtime.current_task.acceptance` 为空，无附加自定义判据；不新增验收判据。
- 被测版本: 实现 commit `15ae2e8`（基线 `372200d` + ITEM-016 实现），实现 stamp `20261017-item016-r3`，
  验证 stamp `20261017-item016-verify-v1`（VERIFY-20261017-v1）。
- 目标: compute_host `192.168.0.103`（RK3588，GStreamer 1.20.3 / librga 1.10.1 / librknnrt 2.3.2）。
- host key fingerprint: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（两轮一致性）。
- 日期: 2026-10-17 ~ 2026-10-18（板端运行 UTC 2026-10-17）

## 1. 结论

**TEST_PASS**。实现轮全量判据在验证侧独立复跑下全部复现；91 项独立语义比对 + 16/16 EXT 检查
（含板端自有 C++ 夹具 54 判例）全绿，无产品缺陷。过程轮 7 次失败均为验证侧夹具构造错（不涉产品源码），
已按节 6 自查修正并复跑；涉及"最后已知教训"记录于节 7。

## 2. 验证方法（证明链，不采信交付 acceptance 行为最终依据）

| 步骤 | 入口 | 依据 |
|---|---|---|
| V1 主复跑 | 交付 `app/tools/board_diagnostics_check.py --keep-remote --stamp 20261017-item016-verify-v1` | 上传 257 文件 → build CoreTests 898/0 → core-tests → build MediaCheck → selftest 7/7 → run-diag → 产物取回；板端保留 /tmp/railhmi_diag 供 EXT |
| V2 独立语义比对 | `_verify16_compare.py`（91 checks） | C1 capability/元素/版本独立解析 r3 vs fresh 恒等（版本串逐字一致：gstreamer=1.20.3 / librga=rga_api version 1.10.1_[0] / librknnrt=2.3.2 (…@2025-04-09T09:09:27)；11 元素全 present 含 Long-name）；C2 三态由验证侧独立复刻 classify 推导并与探针行一致（不采信 probe 自报）；C3 12 字段键集齐全（TC-V-LOG-02）；C4 按日 JSON 三行键集/presence/attempts 与探针行一致且脱敏；C5 redaction 重推导；C6 acceptance 键面恒等且全 true；C7/C8/C9 transcript 完整性（keep-remote 无 cleanup、selftest 7 PASS）；C10 汇总 JSON。**91/91 PASS** |
| V3 EXT 独立参数与负例 | `_verify16_ext.py`（16 checks） | EX-A：thresholds `freeze_timeout_ms=300/reconnect_backoff_ms=300` 独立参数轮 → keys full/三态/redaction/daily 3 行重判据全 PASS；EX-B：`--bogus-flag` exit=2（参数面拒绝通道）、`--help` exit=0 + usage 行；EX-D：同参二跑间 acceptance/capability/element/redaction 行逐字恒等（状态无关行确定性，metric 行墙钟/计数波动域按物理量含义豁免，已独立解析恒等——见节 5） |
| V4 板端自有夹具 | `_verify16_vext.cpp`（g++ 直编 + 链接交付 .a，54 判例） | 完整判例清单见 `diag-ext-…-verify-v1.log`；覆盖 classify 边界（degraded/connecting/reconnecting/offline/gap 优先/frozen）、attempt_id（gen>0/0/空→none）、userinfo/kv 三类脱敏（passwd+pwd/token/secret/user-id/多键/分隔符边界 `&` `,` `)`）、pipeline 四触发词整条脱敏、结构化 JSON（ts/level/source/message/fps 精确 6 位格式/quality_flags 透传/wall_ms）、last_error 三类脏输入脱敏（含 pipeline→`<redacted>`）、真实 test 源双路 fps>0+presence online+wall 校准+attempt 形态、roll_health_logs_by_day 三日裁一+同日不裁、log_day_key UTC 换算与 ts≤0 "—" 标记、contains_credentials 正负样本、二次脱敏文本幂等、**真实断流隔离（request_cut 只影响 CAM-21，CAM-22 保持 online，断流路不判 online）** |
| V5 凭据泄漏扫描 | EX-E | SSH secret + front/side 相机凭据在目录全部证据文件 0 命中；tooltip 形态（password=/pwd=/token=/location=rtsp://）0 命中；`admin` 为登记用户名与已打码 `***` 形态豁免（ITEM-002 先例）；S3CRET 等自造样本值只出现在夹具输入字符串，扫描范围只覆盖证据产物文件不含夹具/脚本源文件 |
| V6 板端清理 | EX-E 步骤 `cleanup-tree` | `rm -rf /tmp/railhmi_diag /tmp/vt_* /tmp/vext16.* config_vt` 全清；`BOARD_CLEAN_DONE` 确认。过程轮误清 1 次（v1 过程轮 EXT 末尾把 r3 树一并清理导致 v1 需重建，判据不受影响） |

## 3. 独立提取的关键判据（非交付 acceptance 行）

- 板端（fresh verify-v1，独立收集）：
  - `type=capability config_ok=true gstreamer=1.20.3 librga=rga_api version 1.10.1_[0] librknnrt=2.3.2 (429f97ae6b@2025-04-09T09:09:27) elements_present=true missing=0`（与 impl r3 逐字符恒等）。
  - 11 元素 `status=present` 含 Long-name（如 `mppvideodec = Rockchip's MPP video decoder`）。
  - `type=metric camera=CAM-01 presence=online (在线) state=live fps=25.000000 … display_dropped=49 ai_dropped=48 recording_overflow=20 gap_count=0 attempt_id=CAM-01#g1`；
    `CAM-02 presence=gap (缺口) gap_count=1 attempt_id=none`；
    `CAM-03 presence=unverified (未核验) state=offline fps=0.000000 last_frame_mono_ms=0 last_frame_wall_ms=-1`。三态的全部 12 字段独立解析齐全带数值域校验。
  - `type=redaction … redacted=<redacted> clean=true`（dirty 带 userinfo+password+rtspsrc+location+pipeline 全脱敏，独立重推导确认不泄漏 S3CRET/admin:/rtspsrc）。
  - `type=acceptance … result=PASS`（键面是否恒等由 V2-C6 独立断言）。
- 按日 JSON `video-2026-10-17.log`：3 行单行 JSON，fields 含 presence/state/fps/last_frame_mono_ms/last_frame_wall_ms/reconnect_count/quality_flags/display_dropped/ai_dropped/recording_overflow/gap_count/attempt_id/last_error，由验证侧逐行 JSON 解析键集=探针键集判定（V2-C4，含 presence 独立推导一致）。
- Nhà 板端 core 单测 898/0 在 V1 独立复跑中实现（非采信实现方归档），selftest 7/7。
- **TC-V-LOG-03 分层结论**：本项验证在 ENV-A0（core 单测级：三态/脱敏/字段纯逻辑）与 ENV-C（板端 test 源真实管道：fps 25 / drops/墙钟校准/断流隔离）分别取证；**真实网络相机 ENV-D（.65/.160/.162/.163/.165/.167 断流现场脱敏）未执行**——按环境约束本项显式不在范围内，
  证据文件不声称真实相机断流现场行为已验证（实现方交付同口径，在 `ITEM-016-ENV-A0-C-20261017-r1.md` §6 已显式声明）。

## 4. 与实现轮文件的比对

| 产物 | impl r3 | verify-v1 | 判定 |
|---|---|---|---|
| core-tests | 898 passed / 0 failed | 898 / 0（独立复跑同 commit） | 一致 |
| selftest | 7/7 PASS | 7/7 PASS（独立） | 一致 |
| capability/版本串 | gstreamer 1.20.3 / librga 1.10.1 / librknnrt 2.3.2 (…) | 逐字节恒等 | 一致 |
| 11 元素 | 全 present + Long-name | 全 present + Long-name | 一致 |
| 三路三态 | online/gap/unverified | 独立 classify 推导 = 交付，双轮一致 | 一致 |
| acceptance 键面 | 7 bool 键 + result | 键面集合恒等、全 true | 一致 |

## 5. 过程轮失败归档（验证侧自查，无产品缺陷）

| 轮次 | 失败 | 根因（验证侧） | 修正 |
|---|---|---|---|
| v1 过程轮 1 | _verify16_run.py 首跑前 COMPARE `C1a` 失败 | fresh 产物尚未生成（时序），非夹具缺陷 | 复跑后 PASS |
| v1 过程轮 2 | COMPARE `C4e` NameError | 验证侧夹具死代码残留 `{c: …}` 推导式中未定义 `c` | 删残留表达式 |
| v1 过程轮 3 | COMPARE `C7 verify transcript` 失败 | `--keep-remote` 语义下交付 runner 正确不 cleanup；夹具预期未区分 | C7 期望改为"keep-remote 无 cleanup 步，树由 EXT 阶段清理" |
| v1 过程轮 4 | vext 编译失败 `"state=" + core::to_string(...)` | C++ `const char* + const char*` 非法（ITEM-014 同类教训） | 链首加 `std::string`（两处）+ g17 动态名走 `c_str()`；自检脚本 `_selfcheck16_chains.py` 建立静态扫描防御 |
| v1 过程轮 5 | EXT 全 FAIL（板端树已被过程轮 4 首轮末 cleanup 清掉） | EXT 驱动无"树在位"前置检查（ITEM-017 `--keep-remote` 后置负例踩同坑） | EXT 头加 `tree-check` 前置（树不在位时直接退出不误判） |
| v1 过程轮 6 | vext 三判例 FAIL：g12_contains_credentials(打码串)=true / g22 "severity" 键 / g44 log_day_key(0)=="1970-01-01" | 全部是验证侧误设期望：① 交付契约 contains_credentials 输入是原始文本，打码串含 "***@" 属预期命中；② log_record_json 键名是 `level` 不是 `severity`（health.cpp 直读确认）；③ format_time_text ts≤0 返回 "—"（alarm_center.cpp 直读），day key 同语义 | 三处期望按源码直读语义修正（g12 拆正/g12b；g22 改 level；g44 拆"ts<=0 → —"+新增 g44b/g44c UTC 午夜翻日、g45 保留） |
| v1 过程轮 7 | EXT 计数 53 ≠ 52；随后 54 ≠ 52 | 验证侧夹具行数计数随判例修正漂移；g17 动态名占 4 行 | 预期改为"50 具名 + 4 动态名 = 54"（与源码 `check(` 52 处、剔除 g17 单行双计数后的 50 具名一致，经 `_selfcheck16_extra.py` named/dynamic 分账确认） |
| v3 终 | **16/16 + 54 vext 判例全 PASS** | — | — |

每次修正后都重跑了 keep-remote 复跑 + COMPARE + EXT 全链；最终轮产物为真一次性成功运行（非过程轮拼接）。

## 6. 证据文件（本目录，verify 侧）

| 文件 | 内容 |
|---|---|
| `verify16-run-driver-tail.log` | 独立复跑驱动 stdout 摘要（build-core/core-tests/build-media/selftest/run-diag 全 exit=0） |
| `diag-192_168_0_103-20261017-item016-verify-v1.log/.transcript.json` | fresh 复跑 transcript（指纹/逐命令/退出码；keep-remote） |
| `diag-192_168_0_103-20261017-item016-verify-v1.core-evidence_diag-….log/.json` | fresh run-diag 探针日志与汇总 JSON |
| `diag-192_168_0_103-20261017-item016-verify-v1.core-evidence_logs_video-2026-10-17.log` | fresh 按日滚动结构化 JSON（板端 UTC 2026-10-17） |
| `verify16-compare-v1.log` | 91 项独立语义比对全 PASS 留痕 |
| `diag-ext-192_168_0_103-20261017-item016-verify-v1.log/.transcript.json/.checks.json` | EXT 全 transcript 与逐判例明细（16 checks）|
| `_verify16_run.py / _verify16_compare.py / _verify16_ext.py` | 验证驱动与比对脚本（可复跑） |
| `_verify16_vext.cpp` | 验证自有扩展夹具（52 check 调用 / 54 运行行 / 板端 g++ 直编 + 链接交付三层 .a） |
| `_selfcheck16_*.py` | 验证侧离线自检（夹具期望构造防御三件套） |

实现方证据原样保留：`diag-…-item016-r3.*`（r3 主链）与 `ITEM-016-ENV-A0-C-20261017-r1.md`。

## 7. 观察项/移交（不判 FAIL，交实现方参考）

- **O-1** 汇总性能数字（display_dropped=49/ai_dropped=48/recording_overflow=20）双轮完全恒同：
  test 源固定帧率采样在实现夹具中是确定性节律，2s 窗口下数字稳定属预期实现语义（ITEM-010 已有参数化证据域），不是测量噪声。若未来把探针接入真实相机 ENV-D 校准，数字将自然波动；当前判据域不涉及该域的通过/失败。
- **O-2** EXT EX-D 同参二跑的恒等域限定在"状态无关行"（capability/element/redaction/acceptance）；
  metric 行含墙钟/计数自然波动域（ITEM-002/043 先例），确定性判断不逐字节比较 metric 行。
- **O-3** `contains_credentials` 对已打码串（`***@host`、`password=***`）持续命中——按交付语义它是
  "原始文本预检"而不是"打码后审计"，不影响日志安全（redact 后的写盘路径已整条 `<redacted>` 或 `***`）；
  不判缺陷，交实现方在未来扩展"redacted 审计模式"时参考。
- **O-4** 板端 `railhmi-diagnostics` 无显式 `--user/--password` 等凭据参数（凭据经配置/环境），探针凭据注入面
  与 UI `cameras.yaml` credential_env 语义一致；真实相机凭据轮（ENV-D）未做因为不在本项声明的源类型范围。

## 8. 范围声明

- 本轮 **TEST_PASS** 覆盖 ITEM-016 判据域的全部已交付面（每路结构化指标三态/脱敏/能力探测/按日滚动）。
- 不覆盖：生产 UI 诊断面板渲染（桌面 UI 职责）、ENV-D 真实相机断流现场取证（实现方交付同判未执行）。
- 被测代码状态运行前有 7 次验证侧夹具修正（见节 5）；产品源码零修改（validation 侧文件仅在本目录新增）。
