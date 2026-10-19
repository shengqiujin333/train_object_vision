# VERIFY-N018-OVERALL v1 — 桌面应用完整交付独立软件验证（n018_software_tester_software_verification）

- Capability: `software_tester.software_verification`（节点 `n018_software_tester_software_verification`，RESUME）
- 被验证对象: `n005/repair_n008_embedded_linux_engineer_desktop_app_implementation` 完整交付范围
  （声明工件 `app/README.md`，ITEM-001..025 + 发布阻塞修复 + UI 修复轮 F-01/F-02）
- 验证范围: **complete_delivery 整体独立验证**（非单项复跑）。关键背景：上一轮 HMI 线整体
  TEST_PASS（`VERIFY-REPAIR-n008-v2`）锚定 commit `a92d008`/`450c43b`；此后视频线/视觉线
  共 **27 轮实现**（`450c43b..89d04a5`）直接修改共享 `app/src/core`、`app/src/media` 层——
  新增约 2.6 万行（vision_* 14 个业务源 + video 线 12 个源 + rules.cpp 追加 `invalidate_track/
  invalidate_camera` + timebase.cpp 小改），其中 `core 静态库` 与 `Release 可执行` 均为桌面
  应用链接的同一层；逐项单测 PASS 不证明组装层面仍然可构建/装载/运行，故必须重验。
- 验证轮: `VERIFY-20261020-n018-v1`
- 结论: **TEST_PASS**（本轮覆盖范围见 §5 判定；既有判据未修改、未放宽）

## 0. 版本锚定

| 项 | 值 |
|---|---|
| 被验证版本（板端复跑读取当前工作树） | HEAD = `89d04a5`（工作树干净 `git status` 确认） |
| 相对上轮整体验证的变更量 | `git diff --stat 450c43b..HEAD -- app/src/` → 130 files, +26034/−117；`app/src/ui/` 仅 P3Replay.qml +23（a92d008，已被 v2 PASS 覆盖） |
| HMI core 语义文件实改 | `rules.cpp`（追加 invalidate_track/invalidate_camera，+43，纯新增 API）；`persistence_model.cpp`/`stream_state.cpp`/`camera_registry.cpp`/`config.cpp` **零变更**（`git log 450c43b..HEAD --` 各文件仅 rules.cpp 命中） |
| 证据目录 | `docs/hmi/test_evidence/VERIFY-N018-OVERALL/` |

## 1. R2 独立板端全量 TestRun（交付入口复跑，不采信归档）

`python app/tools/board_build.py --mode TestRun --host 192.168.0.103 --user admin
 --credential-env TRAIN_RK3588_SSH_CREDENTIAL --stamp 20261020-n018-overall-v1
 --outdir docs/hmi/test_evidence/VERIFY-N018-OVERALL --keep-remote` → 主 log 25 步（mkdir/upload/
 upload-tokens/upload-perm-matrix/upload-packaging/testrun/tokens-selftest/ui-scan/sim-inject/
 status-probe/download×13/artifacts）**全部 exit=0**；core-tests `RESULT: PASS passed=1452 failed=0`
 （JUnit XML `tests=1452 failures=0`——自上轮 670 后新增 782 判例，含视觉/视频线全部新增域，
 本轮一并作为桌面交付回归覆盖）；10 份 core-evidence 探针/驱动日志全部随轮归档，无 FAIL。

## 2. R1 独立 Release 构建 + offscreen 装载冒烟（新版本组装层证明）

`python app/tools/board_release_build.py ... --stamp 20261020-n018-v1 --no-install-deps
 --no-bundle --build-timeout 900` → 构建 **exit=0**（345 上传文件；产物
 `build-target/src/ui/railhmi` ELF aarch64 940520 B，07:42 时间戳 = 本轮产物）；offscreen 冒烟
 **smoke_exit=124**（12 s 存活 = 完成 QML 全装载 + 事件循环持续运行），smoke 日志 4 行 INFO
 banner：`config dir: /tmp/railhmi_release/app/config`、`configuration OK: 6 cameras`、
 `render path: cpu`，**无 QML 错误行**。证据：`release-build-192_168_0_103-20261020-n018-v1.json`
 / `smoke-20261020-n018-v1.log`。

## 3. R3 六页真实 Xvfb 导航观察（用户行为面，验证方独立夹具复用）

自含验证判据的 `verify_nav_capture.py`（v2 轮夹具原样复用，仅改 RD 目录/stamp/日志名；
判据面 WIN_UP/app.log 错误关键字/截图差异完全同 v2 轮口径）独立复跑：
- 上传 340 文件到 `/tmp/railhmi_verify_nav_n018/app` → `bash build.sh Release` **exit=0**；
- 临时配置副本（simulation=true/admin）→ Xvfb :99 xcb+software 后端启动 → `WIN_UP=1`；
- xdotool 逐 NavBar 槽位点击六页 → ffmpeg x11grab 截图；
- **六页 PNG 全部非空且两两直方图差 >0.03**（15 对全部 OK，`verify_nav_judge.py` 判定）；
- app.log **仅 4 行 INFO**，`TypeError`/`is not a type`/`doesNotExist`/`Cannot open`/`chipName`
  **全文 0 命中**（对照评审轮修复前 3×TypeError + 48×Cannot open）；
- **P3 人工语境复核**（`p3_replay.png`）：三联标题 "车辆左侧 (CAM-02)/车头前方 (CAM-01)/
  车辆右侧 (CAM-03)"（F-01 修复特征在位）、关键帧行全 `—` 占位（F-02 特征）、同步偏差提示
  "同步偏差 900 ms（阈值 300 ms）" 红字可见、来源回退 "NVR 不可用，回放本机片段" 正常、
  右栏事件详情（时间/距离 12.3m/置信度 87%/已确认）完整。
- PLAN/驱动路径深度自查：`REPO = HERE.parents[3]`（VERIFY→test_evidence→hmi→docs→root），
  adapt 时先以 parents 枚举确认后才上板（历轮深度教训防复发）。
- 证据：`verify_nav_capture.py` / `ui-nav-transcript.json` / `ui-nav-20261020-n018-v1.log` /
  `nav-app.log` / `nav-xvfb.log` / 六页 PNG / `verify_nav_judge.py`。

## 4. R4 板端 ext 夹具（沿用两套，判据零修改）

1. stream 触碰域（ITEM-005 夹具原样）: `verify_stream_ext` **40 passed / 0 failed** + 配置
   负例 fx1（首帧超时=0 拒绝 EXI1）fx2（负退避拒绝）fx3（好配置通过）——log
   `VERIFY-FIXTURES-20261020-n018ext1.log`，指纹核验行在 transcript。
2. P3 触碰域（v2 轮新增夹具原样）: 复跑 `run_p3_ext_fixtures.py`（适配版仅改 RD/stamp/日志
   路径，判据零改）→ **16 passed / 0 failed**（registry 3 槽/F-01 数据/F-02 thumbSource 四
   边界镜像/sync 偏差 900>300 提示可达）——log `VERIFY-FIXTURES-20261020-n018-p3ext.log`。
   前置: stream 夹具 cleanup 删 `/tmp/railhmi_build` 后，经 `board_build.py --mode CoreTests
   --keep-remote --stamp 20261020-n018core` 重建（BUILD_EXIT=0）再链接（v2 轮教训 §4-2 同序）。

## 5. R5 主机静态与负例（交付链路完整性，离线）

- `check_qml_qt_validity.py --root app/src/ui --headers ...` → **EXIT=0**（21 QML / 52 头文件，
  上轮 28 头 → 现 52 头含视觉线新增头，全部通过 Qt 关键字宏冲突检查）。
- `negcheck_qml_qt_validity.py` → NEG_HEALTHY=0 / NEG_B01=1 / NEG_B03=1 / **NEG_F01=1** /
  NEGCHECK_RESULT=PASS（守卫判据不空转）。
- `verify_packaging25_scan.py --app app --pkg packaging` → **exit=0**，P1..P16 + D-01/D-01b/D-02
  布尔标志全部 True（`targets_prefix=False` 为信息诊断非缺陷标志）。
- `negcheck_packaging_fix.py` → NEG_HEALTHY=0 / NEG_D01=1 / NEG_D02=1 / RESULT=PASS。

## 6. 判定与边界

- **TEST_PASS 范围**: ① 上轮整体 TEST_PASS 后（视频线/视觉线 27 轮共享层变更）新版本组装面
  独立复验：Release 构建 exit=0 + offscreen 冒烟存活 + 六页导航 0 QML 错误 + 截图差异；
  ② 完整 TestRun 1452/1452（含 782 新增判例）+ 19 检查步全 0；③ P3 触碰域与 stream 触碰域
  ext 56/56（两套夹具判据零修改）；④ 主机静态守卫 + 负例 + 打包扫描全 PASS。
- **不纳入本轮范围**（沿用历轮边界）: 真实 6 路 RTSP 取流下的页面实测（视频线交割）、
  真实 RKNN 模型推理画面（视觉线交割）、systemctl/断电自启（ENV-E 现场操作）、
  操作员真实 X 会话人工视觉确认（用户核验）。
- **测试环境自修复记录（授权 scope=test）**: ① stream 夹具 cleanup 后 `/tmp/railhmi_build`
  被删 → CoreTests 重建后再跑 p3 ext（历轮既有教训的标准恢复，无需新修复）；② Git-Bash 会话
  下 argv 的 `/tmp/...` 需要 `MSYS_NO_PATHCONV=1` 前缀（既有环境事实，未新增修复）。
- **观察项（非缺陷）**: 无新增。v2 轮 O-1（banner `simulation=off` 编译期宏与 YAML 配置同名
  不同源）维持登记。
- 注意: 驱动清理闸门历轮教训均遵守；本轮 nav 夹具 pkill 仅针对 railhmi/Xvfb 本轮进程，
  确认后收尾；板端 `/tmp` 残留检查后仅清理本轮自有 `railhmi_verify_ext_n018`，历轮树保留。

## 7. 过程教训（验证侧）

1. fixture driver 把日志写回 `HERE`（J 指向原目录）→ 复用时必须同时替换 CPP 查找目录与
   log 文件名（本轮先发现 cpp 缺失、再发现第一轮跑了日志落旧目录；适配后重跑才立案）。
2. unpack 命令含 `$( )`/复杂引号被命令 gate 判 UNPARSEABLE → 落为临时脚本文件执行（历轮
   教训：复杂字面量走文件不上 heredoc）。
3. board_build.py 的远端命令输出本体（`RESULT: PASS`）在 core-evidence log 内，主 log 只含
   步级汇总——两份都读，不单边下结论。
4. 上轮 ext 日志命名 stamp 在后（`...n018ext1.log`），与其余文件（stamp 中段）不同——验证侧
   证据命名要统一 stamp 位置（本轮新文件已统一为 `...-n018-...`）。

## 8. 本轮验证方证据清单

- 本文件 `VERIFY-N018-OVERALL-v1.md`
- R1: `release-build-192_168_0_103-20261020-n018-v1.json` / `smoke-20261020-n018-v1.log`
- R2: `build-TestRun-192_168_0_103-20261020-n018-overall-v1.log` + `.transcript.json` +
  14 份 `core-evidence_*`（core-tests/XML/10 探针/sim-inject/log+json）
- R3: `verify_nav_capture.py` / `verify_nav_judge.py` / `ui-nav-transcript.json` /
  `ui-nav-20261020-n018-v1.log` / `nav-app.log` / `nav-xvfb.log` / 六页 PNG
- R4: `VERIFY-FIXTURES-20261020-n018ext1.log(+transcript)` /
  `VERIFY-FIXTURES-20261020-n018-p3ext.log` / `verify_p3_ext.cpp`（判据原样复用）/
  `run_p3_ext_fixtures.py`（适配版）/
  `build-CoreTests-192_168_0_103-20261020-n018core.(log/transcript.json)`
- R5: 主机三件输出在 §5 内联（远程无独立文件；结果以 exit/PASS 计数留证）
