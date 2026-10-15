# ITEM-003 软件验证 TestEvidence — 分层 CMake 工程骨架与三档构建入口

- Role-Capability: software_tester.software_verification
- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-003`
- 被验证提交: `5e7746a`（embedded_linux_engineer.desktop_app_implementation → IMPLEMENTED）
- 被验证实现: `app/CMakeLists.txt`、`app/build.sh`、`app/src/{core,platform,media,ui}`、`app/tests/core`、`app/tools/board_build.py`
- 判据来源: 任务文本内嵌验收 + `docs/hmi/desktop_app_tasks.yaml` 工作项 3；分层口径 `docs/hmi/DESKTOP_APP_DESIGN.md` §5.2；测试环境分层 `docs/hmi/TEST_DESIGN.md` §1（ENV-B / ENV-A0）
- 方法边界: 本次为**构建骨架独立验证**（ENV-B 板端本机构建 + 静态分层审查）。主机 Windows 无 C/C++ 工具链（E-00），ENV-A/ENV-A0 主机档未执行，结论不跨层外推。

## Outcome: TEST_PASS（范围内判据逐条满足；环境缺口如实标注，不改变判据）

## 1. 板端独立复现（登记 `rk3588_inspect` Paramiko 入口，四个模式全部独立复跑）

- 入口: `python app/tools/board_build.py --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode <mode> --outdir docs/hmi/test_evidence/ITEM-003-build --stamp verify-20261015-v1 [--run-tests]`
- 主机指纹: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现者全部 6 份 transcript 逐份一致，同一目标板）
- 板远端工作目录 `/tmp/railhmi_build` 每轮构建后清理（`rm -rf` + `cleaned`，本验证 5 轮均含 cleanup 步），不触碰安装/网络/进程以外范围。

| # | 模式 | 实现者权威运行 | 独立复跑（本轮） | 结论 |
|---|---|---|---|---|
| 1 | CoreTests | core-r3: 构建成功 + 9/9 测试通过（core-r1/r2 为修复历史） | `build-CoreTests-...-verify-20261015-v1`：`BUILD_EXIT=0`、`TEST_EXIT=0`、9 passed / 0 failed、产物 `build-core/tests/railhmi-core-tests`（41312 字节） | **一致，判据满足** |
| 2 | MediaCheck | media-r2: `librailhmi_media.a` 产出 | `build-MediaCheck-...-verify-20261015-v1`：`BUILD_EXIT=0`、`build-media/src/media/librailhmi_media.a`（11890 字节） | **一致，判据满足** |
| 3 | Release(target) | target-r1: 缺 Qt5 Quick/Qml → 明确报错，BUILD_EXIT=1 | `build-Release-...-verify-20261015-v1`：`MISSING BUILD DEPENDENCIES (mode=target): - Qt5 Quick/Qml dev ...` + `BUILD_EXIT=1` | **一致，判据满足**（无静默失败） |
| 4 | Preview | preview-r1: 缺 Qt5 Quick/Qml → 明确报错，BUILD_EXIT=1 | `build-Preview-...-verify-20261015-v1`：`MISSING BUILD DEPENDENCIES (mode=preview)` + `BUILD_EXIT=1` | **一致，判据满足**（无静默失败） |
| 5 | check（自检） | 未单独归档（并入 target/preview 失败前自检行） | `dependency check: Qt=no gstreamer=yes librga=yes sqlite3=no`，`check complete`，exit 0 | 自检入口可用 |

复跑 CoreTests 测试逐项输出（与本轮 transcript 原文一致）：
`version_string == 0.1.0`、`build_mode is a known mode`、`build_description contains railhmi`、
`redact_url hides credentials`、`redact_url leaves credential-free url unchanged`、
`FileStore::write_atomic`、`FileStore::read_all round-trip`、`FileStore::exists after write`、
`FileStore::exists false for missing`（[PASS]×9）；`[SKIP] sqlite tests (RAILHMI_HAVE_SQLITE not defined)`；
配置行 `-- railhmi: SQLite3 dev not found -> SqliteStore disabled (install libsqlite3-dev to enable)`（明确提示，非静默）。

## 2. 静态分层审查（对照任务拆分要求）

| 任务要求 | 落点 | 核对结果 |
|---|---|---|
| railhmi-core 仅 C++ 标准库，无 Qt/GStreamer/相机 | `app/src/core/`（`version.h/.cpp`）；Grep 仅命中注释声明 | **满足**：core 无任何 Qt/GStreamer/SQLite 头；`src/core/CMakeLists.txt` 不引入外部依赖 |
| railhmi-platform（SQLite/文件存储/日志） | `app/src/platform/`（log/file_store/sqlite_store） | **满足**：SQLite 为条件组件（`find_package(SQLite3 QUIET)` + `RAILHMI_HAVE_SQLITE`），缺失时明确 note |
| railhmi-media target 专用 Qt/GStreamer | `app/src/media/`（GStreamer 1.0 app/video + librga im2d） | **满足**：仅 `RAILHMI_BUILD_TARGET|MEDIA` 进入构建（`app/CMakeLists.txt:66-68`） |
| railhmi-ui（Qt/QML，target/preview） | `app/src/ui/` + `qml.qrc` | **满足**：Qt5 Quick/Qml 缺失时 `FATAL_ERROR` 可读提示 |
| 三档构建开关 + build.sh | `RAILHMI_BUILD_{TARGET,PREVIEW,CORE_TESTS,MEDIA}` 三/四开关 | **满足**：任一开关未选时 CMake 顶层给出可读 FATAL（`app/CMakeLists.txt:33-37`） |
| 无工具链→明确提示非静默失败 | `build.sh` MISSING 清单 + 退出 1 | **满足**：Release/Preview 两档实测逐条列出 `Qt5 Quick/Qml dev` 并 `BUILD_EXIT=1` |

## 3. 逐条判据核对（任务文本内嵌验收）

| 判据 | 结果 | 证据 |
|---|---|---|
| 在具备对应工具链的环境下 CoreTests 编译成功 | **通过**（板端 aarch64 gcc11.4+cmake3.22 即该最小工具链） | §1 行 1 复跑与 core-r3 |
| CoreTests 测试运行与通过 | **通过** | 9/9，`TEST_EXIT=0`，本轮独立复现 |
| 在具备对应工具链的环境下 Preview 编译成功 | **本环境未核验**：板端与 Windows 主机均缺 Qt5 Quick/Qml/开发包（登记缺口 E-01b/E-00），非实现缺陷——脚本/CMake 在缺依赖时按判据给出明确报错（已证）。归属：平台/rootfs 职责安装 `qtdeclarative5-dev qml-module-qtquick2` 等后即可进 UI 编译 | target-r1/preview-r1 与本轮复跑 |
| 板端 target 编译成功并产出二进制 | **本环境未达成（非实现缺陷）**：同一 E-01b Qt 开发包缺口阻塞 UI 层；`build.sh` 行为正确（明确 MISSING 清单 + BUILD_EXIT=1），媒体层（GST+librga）已证明可编译（MediaCheck 成功）。归属与动作同 E-01b | §1 行 2/3、`build_env_check.md` §3.1 |
| 无工具链时构建脚本给出明确缺失依赖提示而非静默失败 | **通过** | Release/Preview 两轮独立复跑：逐条 MISSING + 退出 1 + "未执行构建（非静默失败）" |

## 4. 限制与未核验项（不据此降级或升级结论）

1. 本轮全部构建结论都在**目标板本机**取得（ENV-B 层）；主机 Windows 无 C/C++ 工具链（E-00），主机 Preview/ENV-A0 未执行，结论不跨层。
2. `Release`/`Preview` 档的"编译成功"在当前板端不可观察：根因为已登记环境缺口 E-01b（Qt5 Quick/Qml 开发包缺失，板端 QML 运行模块亦不完整），已由 ITEM-001 先行登记并归属平台/rootfs 职责；软件验证不豁免该缺口，也不把缺口记为实现缺陷。
3. 骨架验证只证明**编译/链接可构建与入口契约**，不证明运行正确性；target 二进制运行行为归后续工作项（ITEM-004..024）各自验证。
4. SQLite 用例因板端缺 `libsqlite3-dev` 按条件跳过（SKIP 明确打印，`build_env_check.md` §2 同口径）；持久化层验收归 ITEM-007/008。
5. 板端远端 `/tmp/railhmi_build` 各轮结束已清理；无遗留进程或产物。

## 5. 复现

```sh
# 本轮复跑命令（与实现者同源不同 stamp；凭据经环境变量注入）
python app/tools/board_build.py --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --mode CoreTests --outdir docs/hmi/test_evidence/ITEM-003-build --stamp verify-20261015-v1 --run-tests
# 其它模式把 --mode 换为 MediaCheck / Release / Preview / check（对应日志同前缀）
```

## 6. 本轮新增证据文件

- `build-CoreTests-192_168_0_103-verify-20261015-v1.log` / `.transcript.json`
- `build-MediaCheck-192_168_0_103-verify-20261015-v1.log` / `.transcript.json`
- `build-Release-192_168_0_103-verify-20261015-v1.log` / `.transcript.json`
- `build-Preview-192_168_0_103-verify-20261015-v1.log` / `.transcript.json`
- 实现 `n005` 交付 `candidate`（E:\learn\restdream2\projects\train_vision_run2\runtime\resources\WF-4e5a1d78-8f3a-4677-a4af-ec277f6bc937\CALL-e577eb93-34fa-414c-8ae4-82e8a98439b2\artifact_implementation.yaml）——验收侧判据见本文件 §1-§3。
