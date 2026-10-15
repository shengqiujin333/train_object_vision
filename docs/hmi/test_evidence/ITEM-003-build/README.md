# ITEM-003 构建证据 — 分层 CMake 工程骨架与三档构建入口

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-003`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §5.2（分层/目录）、§15.1（构建入口）；`docs/hmi/TEST_DESIGN.md` §1（ENV-B）/§5（ENV-A0）
- 源码入口: `app/CMakeLists.txt`、`app/build.sh`
- 运行器: `app/tools/board_build.py`（经登记 SSH 入口上传并构建）

## 0. 环境与授权（本轮实测）

- 目标板 RK3588（经 `rk3588_inspect` Paramiko 入口，受信任 `known_hosts`，全超时）：
  `uname`=aarch64 Linux 6.1.141；`c++`=GNU 11.4.0；`cmake` 3.22.1；`pkg-config` 0.29.2。
- 依赖实测：GStreamer 1.20.3 dev + gstreamer-app/video ✓；librga dev（im2d.h + pkg-config librga 2.1.0）✓；
  **Qt5 Quick/Qml 开发包缺失**（无 `Qt5QuickConfig.cmake`）；**SQLite3 dev 缺失**（无 `sqlite3.h`/`libsqlite3.so`）。
- 主机（Windows）**无 C/C++ 工具链**（E-00，见 `../../build_env_check.md`），故 ENV-A/ENV-A0 主机档本轮不可执行；
  本轮 ENV-A0 等价面在**板端本机**执行（同一份 core 代码、同一最小工具链：C++ 编译器 + CMake）。

> 板端本机构建证明目标 ABI 可编译链接；不证明运行正确性。主机可运行不等于目标可运行。

## 1. 三档构建开关（+ media 层检查）

| 开关 | build.sh | CMake 变量 | 依赖 | 本轮板端结果 |
|---|---|---|---|---|
| target（aarch64 板端） | `./build.sh Release` | `RAILHMI_BUILD_TARGET=ON` | Qt5 Quick/Qml + GStreamer + librga（SQLite 可选） | **依赖缺失 → 明确报错**（Qt5 Quick/Qml） |
| preview（x86_64 模拟） | `./build.sh Preview` | `RAILHMI_BUILD_PREVIEW=ON` + `RAILHMI_SIMULATION=ON` | 宿主 Qt5 Quick/Qml | **依赖缺失 → 明确报错**（Qt5 Quick/Qml） |
| core（单元测试） | `./build.sh CoreTests` | `RAILHMI_BUILD_CORE_TESTS=ON` | C++ 编译器 + CMake（SQLite 可选） | **构建成功 + 测试 9/9 通过** |
| media（target 层检查） | `./build.sh MediaCheck` | `RAILHMI_BUILD_MEDIA=ON` | GStreamer + librga（无 Qt） | **构建成功** |

分层目录：`app/src/core`（纯 stdlib）、`app/src/platform`（日志/文件存储 + 可选 SQLite）、
`app/src/media`（target 专用 GStreamer+librga）、`app/src/ui`（Qt/QML）、`app/tests/core`。

## 2. 实测结果

| 运行 | 模式 | 构建退出 | 结果 | 证据文件（本目录） |
|---|---|---|---|---|
| core-r3 | CoreTests | 0 | **9 passed / 0 failed**，`build-core/tests/railhmi-core-tests` 产出 | `build-CoreTests-...-core-r3.{log,transcript.json}` |
| media-r2 | MediaCheck | 0 | `librailhmi_media.a`（GStreamer 1.20.3 + librga）产出 | `build-MediaCheck-...-media-r2.{log,transcript.json}` |
| target-r1 | Release | 1 | `MISSING BUILD DEPENDENCIES (mode=target): Qt5 Quick/Qml dev …` | `build-Release-...-target-r1.{log,transcript.json}` |
| preview-r1 | Preview | 1 | `MISSING BUILD DEPENDENCIES (mode=preview): Qt5 Quick/Qml dev …` | `build-Preview-...-preview-r1.{log,transcript.json}` |

CoreTests 断言（core-r3 原文）：`version_string`、`build_mode`、`build_description`、
`redact_url`（凭据脱敏 ×2）、`FileStore::write_atomic/read_all/exists`（×4）；
SQLite 用例按 `RAILHMI_HAVE_SQLITE` 条件跳过（板端缺 sqlite3 dev，见 §4）。

## 3. 修复历史（对照早期尝试，不重复被证伪的修法）

| 运行 | 现象 | 根因 | 修复 | 复测 |
|---|---|---|---|---|
| core-r1 | 构建成功但测试二进制找不到 | 测试可执行文件落在 `build-core/tests/core/`，与约定路径不符 | `set_target_properties(... RUNTIME_OUTPUT_DIRECTORY .../tests)` | core-r2 |
| core-r2 | 构建成功，3 项 FileStore 断言失败 | `ensure_parent_dir` 拼前缀时丢了根 `/`，`mkdir` 目标变成相对路径 | 改为按 `/` 前缀逐级 `mkdir` | core-r3（9/9 绿） |
| media-r1 | 构建成功但产物清单未列出 | 运行器产物路径未按模式区分 | 运行器按模式列产物 | media-r2 |

## 4. 依赖缺口与责任（不阻塞 core/media 层）

1. **Qt5 Quick/Qml 开发包缺失**（`Qt5QuickConfig.cmake`）：阻塞 target/preview 的 UI 层与 target 应用二进制。
   由**平台/rootfs 职责**按授权安装 `qtdeclarative5-dev qml-module-qtquick2 qml-module-qtquick-controls2 qml-module-qtquick-layouts`；
   桌面应用不自行安装系统包。安装后 `./build.sh Release`/`Preview` 即可进入 UI 编译。
2. **SQLite3 开发包缺失**：`SqliteStore` 被 CMake 条件跳过（打印明确 note，不静默），
   core/media 层仍可构建；持久化（ITEM-007/008）需先安装 `libsqlite3-dev`。
3. **主机 C/C++ 工具链缺失**：ENV-A/ENV-A0 主机档待补齐；本轮以板端同源 core 测试作为 ENV-A0 等价证据，
   结论分开记录，不合并。

## 5. 复现

```sh
# 目标板本机（经授权入口；本运行器代传 app/ 并执行）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-003-build --stamp <tag> --run-tests
# 其它: --mode MediaCheck | Release | Preview | check

# 直接在板上（已上传 app/ 后）
cd app && ./build.sh CoreTests && ./build-core/tests/railhmi-core-tests
```
