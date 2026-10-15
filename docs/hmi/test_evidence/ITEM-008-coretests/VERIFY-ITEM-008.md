# VERIFY-ITEM-008 — railhmi-core 单元测试与可复现验证入口（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-008`（commit **cd1d888**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/tests/run_core_tests.sh`（单命令入口）、`app/tests/core/{main.cpp, test_util.cpp, test_util.h}`（harness/JUnit）、`app/tools/board_build.py`（TestRun 模式）、`app/build.sh`/`app/CMakeLists.txt`（CoreTests 档，ITEM-003 已验）；覆盖范围本体（test_config/stream/rules/persistence/platform）为 ITEM-004..007 已独立验证的既有交付，本轮作为入口覆盖声明核对
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-07**（ENV-A0：单命令入口、逐项 pass/fail 打印、退出码正确、无 Qt/GStreamer/相机依赖、文件可独立复现）
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（本范围独立证据：验证以交付入口本身复现 243/243 与实现者归档逐行一致；入口退出码负例 2/3 实测成立；JUnit XML 可独立解析且转义正确；扩展夹具 14/14 全绿；过程轮 v1/v2 的 4 处失败经自查均为验证侧期望构造错误，见 §4）
- 实现者归档核对: `README.md` §2 表述、log（243 PASS 行）、XML（243/0）、transcript 全部存在且内部一致；上传清单 66 文件与本地 `app/` 实树逐文件计数一致（主机复算）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**（主机）: ①`run_core_tests.sh`——参数解析（`--outdir/--stamp/--no-build`，未知参 exit 2）、构建失败 exit 3、二进制缺失 exit 3、测试真实退出码经 `PIPESTATUS[0]` 取值未被 tee 污染（run_core_tests.sh:58）、日志/XML 由入口统一归档；②`test_util.cpp`——`check()` 逐条记录打印、`write_junit` 实体转义（&/</>/"'/）+ 控制字符替换、`main.cpp` RESULT 行与计数器和退出码一致；③`board_build.py` TestRun 分支——上传走本地 `app/` 树、板上执行交付入口命令本身（非直接调 harness）、取回归档产物、`--keep-remote` 未设时按名清理、全步显式超时；④主机复算 `build.sh` 的上传过滤算法覆盖 `app/` 实树 = **66 文件**，与实现者 upload 步骤计数一致。
2. **板端独立复跑**（验证自有运行器 `verify_coretests_fixtures.py`，打包上传 HEAD `app/` 快照压缩 92 844 字节 → 板上独立目录 `/tmp/railhmi_verify008_board`，**直接执行交付入口** `bash tests/run_core_tests.sh --outdir core-evidence --stamp VERIFY-20261016-v3`，全超时 1200s）: `ENTRY_EXIT=0`；入口归档 log **243 passed / 0 failed**、`RESULT: PASS passed=243 failed=0`；**243 条 [PASS] 行与实现者归档 log 程序化逐行对比完全一致**（脚本对比，含顺序）；取回的 JUnit XML 独立解析 `tests=243 failures=0`（testcase 243、failure 0）。板端 SSH host 指纹与实现者 r1 transcript 一致（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）。CMake 日志确认 SQLite 走运行时库 + shim（`persistence enabled`），持久化测试实际执行非 SKIP。
3. **入口退出码负例**（板上）：`--bogus-flag → NEG_UNKNOWN_EXIT=2`（打印 `unknown arg`）；干净目录仅有 build.sh+tests 且未构建时 `--no-build → NOBIN_EXIT=3`（打印 `missing test binary`）。判据「打印逐项 pass/fail 且退出码正确」的 0/1/2/3 四条退出码路径均有实测（0=复现轮、1=夹具 v1/v2 过程轮的真实 FAIL 路径无意中亦实测成立、2/3=负例）。
4. **验证自有扩展夹具**（源码 `verify_coretests_ext.cpp` v3，g++ `-std=c++17 -O1 -Wall -Wextra` 零告警直编 `tests/core/test_util.cpp + src/core/src/version.cpp`，无 SQLite/Qt 链接）：**14 断言，14 通过 / 0 失败**。覆盖实现者第一方未覆盖的 harness 契约面：records() 地址稳定/逐条追加/计数器增量/字段保真/失败记录保 detail 文本；XML 实体转义逐字节核对（`name="fx xml a&amp;b&lt;c&gt;d&quot;e&apos;f"`）、独立解析器计数与记录数一致、声明行+根元素结构、totals 属性存在、套件名回写、处理失败记录后 exactly-1-failure 语义。
5. **config-check 回归守门**（同快照）：good→`CFG_EXIT=0`（6 cameras/4 roi/1 integrations 摘要）、bad（删 confirm_frames）→`CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`，与 ITEM-004/007 权威轮逐字一致。
6. **运行器安全版式**: known_hosts 指纹不匹配即中止（RejectPolicy）；connect/auth/banner/SFTP 全显式超时（10s/120s/1200s 级别）；凭据只读环境变量 `TRAIN_RK3588_SSH_CREDENTIAL` 不落日志；全部板上目录按名清理并在 transcript 留痕（cleanup 步骤 exit=2 为末尾 `ls` 对已删目录的确认输出，非失败）。

## 2. 判据逐条核对（工作项描述 / TC-CORE-07）

| 判据 | 结果 | 证据 |
|---|---|---|
| tests/core 覆盖六域（配置校验/六态状态机/规则与级别映射/抑制/持久化/保留策略） | ✅ | 入口 log 程序化分组：platform 16（版本/脱敏/原子文件/SQLite 真实打开）、config 32（含 10 类非法报具体字段 + 契约字段）、stream 38（六态+升级+classify）、rules 97（级别映射/抑制/track/ack/ROI）、persistence+timebase 60（9 表/WAL/SIGKILL/保留/缺口）合计 243——覆盖声明与 TEST_DESIGN §5 TC-CORE-01..06 对应，各域内容已在 ITEM-004..007 轮独立验证通过，本轮核对的是入口确实执行了这五组且 XML testcase 计数闭合 |
| 一条命令运行 | ✅ | `bash tests/run_core_tests.sh --outdir core-evidence --stamp <tag>` 板上实测 `ENTRY_EXIT=0`，构建+运行+归档由脚本内完成 |
| 打印逐项通过/失败 | ✅ | 243 条 `[PASS]` 行逐行可见；失败行格式 `[FAIL] name -- detail` 由 harness 实现（夹具 v1/v2 过程轮中真实打印过 4 条 [FAIL] 行，路径成立） |
| 退出码可归档（含 JUnit XML）| ✅ | JUnit XML 独立解析 243/0；RESULT 行 passed=243 failed=0；exit=0；log+XML 由入口归档并由验证运行器原样取回归档（`VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_*.log/.xml`） |
| 最小工具链（C++ 编译器 + SQLite），不依赖 Qt/GStreamer/相机 | ✅ | 板上 build.sh 依赖自检 `Qt=no gstreamer=yes librga=yes sqlite3=no`（sqlite3 dev 缺失）仍构建成功：Qt 链路不在 CoreTests 档、SQLite 走运行时库 `libsqlite3.so.0` + 项目 shim 头（`persistence enabled` 日志行）；g++ 直编扩展夹具零链接库同样编译运行成功 |
| 测试文件可独立复现 | ✅ | 验证未使用实现者构建目录：独立上传快照+独立目录构建，243/243 逐行一致；复现命令见 §5 |

## 3. 实现者归档核对（BUILD 证据 versus 实物）

| 项 | 实现 r1 归档 | 主机/板端核对 | 一致 |
|---|---|---|---|
| transcript 步骤 | mkdir/upload/testrun/download×2/artifacts/cleanup 全 exit=0 | transcript.json 逐项相符；指纹 SHA256:NzHF...（五轮历史一致） | ✅ |
| upload 计数 66 文件 | transcript | 主机按 board_build.py 同算法复算本地 `app/` 树 = 66 | ✅ |
| 完整测试 stdout | 归档 log 243 PASS 行 + RESULT + junit 行 | 板上复现 log 243 [PASS] 行程序化逐行一致 | ✅ |
| JUnit XML | tests=243 failures=0 | 独立解析相同；verifier XML 同为 243/0 | ✅ |
| 入口命令/参数 | `bash tests/run_core_tests.sh --outdir core-evidence --stamp 20261015-ITEM008-r1` | 验证以同级参数复跑，行为一致 | ✅ |

## 4. 验证侧自查更正（保持证据链诚实）

扩展夹具 v1 过程轮 11/14（留痕 `VERIFY-FIXTURES-VERIFY-20261016-v1.*`）；v2 过程轮 13/14（留痕 `*-v2.*`）。4 处失败全部为**验证侧期望构造错误**（对照 `test_util.cpp` 源码确认，非被测对象问题，v1/v2 留痕 §6）：
1. `TestRecord fields` 断言误写 `!records().back().pass`——probe 记录本身是 PASS 记录（v1 修）。
2. `testsuites root` 断言用 `find(...)==0` 忽略了首行 `<?xml ...?>` 声明行（v1 修为 `rfind("<?xml",0)==0 && find("<testsuites ")!=npos`）。
3. `independent count` 期望 `fails==1`，未计入 harness API 测试合法推入的探测失败记录（v1 实际 2 条失败记录 → v2 仍 2；v3 将探测记录用后 `pop_back()`，只做断言不入档，位相确定）。
4. `-Wsign-compare`/unused 告警（v1 直编可见）→ v3 `static_cast<int>` + 删除未用变量（零告警）。

## 5. 复现

```sh
# 验证运行器（板上独立目录 + 交付入口本身 + 退出码负例 + 扩展夹具 + config 好坏 + 按名清理）
bash docs/hmi/test_evidence/ITEM-008-coretests/run_verify_round.sh
#   内部调用:
#   python docs/hmi/test_evidence/ITEM-008-coretests/verify_coretests_fixtures.py \
#     --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
#     --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
#     --cpp docs/hmi/test_evidence/ITEM-008-coretests/verify_coretests_ext.cpp \
#     --outdir docs/hmi/test_evidence/ITEM-008-coretests --stamp VERIFY-20261016-v3
# 仅复现单命令入口（板端本机）:
#   bash tests/run_core_tests.sh --outdir core-evidence --stamp <tag>
# 仅复现实现者运行器口径:
#   python app/tools/board_build.py --host 192.168.0.103 --user admin \
#     --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
#     --outdir docs/hmi/test_evidence/ITEM-008-coretests --stamp <tag>
```

## 6. 证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v3.{log,transcript.json}` — 权威轮原始日志/逐命令 transcript（指纹一致、ENTRY_EXIT=0、负例 2/3、CFG 0/1、EXT 14/14、清理确认）
- `VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_core-tests-VERIFY-20261016-v3.{log,xml}` — 板上交付入口自身归档的完整 stdout 与 JUnit XML（243/0；与实现者 r1 逐行一致）
- `VERIFY-FIXTURES-VERIFY-20261016-v{1,2}.{log,transcript.json}` — 过程轮留痕（验证侧期望构造错误，§4）
- `verify_coretests_ext.cpp` / `verify_coretests_fixtures.py` / `run_verify_round.sh` — 验证自有夹具（v3）与运行器
- `build-TestRun-192_168_0_103-20261015-ITEM008-r1.*` — 实现者归档（实现轮产物，未修改）

## 7. 观察与边界（不改变 PASS 结论；交实现/后续项参考）

- **O-1（观察）**: JUnit XML 中 243 个 testcase 有 242 个唯一名——`offline cannot present as live` 重名（`test_stream.cpp:29` OFFLINE 初始态与 `:80` 达阈值后两处断言同名）。不影响 pass 判定（唯一键应为 classname+name 或顺序号），但对按名定位失败用例的 CI 消费略有干扰；属 ITEM-005 既有第一方用例，非本轮交付引入，建议实现侧后续轮为重名用例增加区分后缀（不构成本期判据缺口）。
- **边界**: 本入口只覆盖 railhmi-core/platform 逻辑（ENV-A0 等价面）；UI/取流/推理/相机行为不在本入口，也不在本证据范围。板端本机（aarch64）实测不代表主机可运行；主机 Windows 无 C/C++ 工具链（历史 E-00 缺口）未变。
- **环境事实**: 板端无 libsqlite3-dev（历史登记不变），SQLite 走运行时库 + shim 路径，构建 `persistence enabled` 确认持久化测试实际执行非 SKIP；若目标环境连运行时库也没有，入口仍可运行但持久化用例 `[SKIP]`——该退化口径未在本轮实测（板上存在运行时库），不影响当前判据。
