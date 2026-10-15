# railhmi — RK3588 车载监控 HMI 桌面应用

本目录是桌面监控应用的源码工程。当前实现进度对应实现任务清单
`docs/hmi/desktop_app_tasks.yaml` 的工作项，按序增量交付；**尚未交付完整应用**。

- 设计: `docs/hmi/DESKTOP_APP_DESIGN.md`（DesktopAppDesignArtifact）
- 交互/视觉: `docs/hmi/INTERACTION_DESIGN.md`、`docs/hmi/VISUAL_DESIGN.md`、`docs/hmi/design_tokens.yaml`
- 测试: `docs/hmi/TEST_DESIGN.md`、`docs/hmi/E2E_PLAN.md`

---

## 当前状态（增量）

| 工作项 | 内容 | 状态 |
|---|---|---|
| ITEM-001 | 目标构建/运行依赖基线与授权缺口登记 | **源码 + 板端实测已交付**：`tools/env_probe.py`、`tools/board_env_probe.py`；板端 E-00..E-13 已实际采集（六路相机/NVR 端口可达），三态缺口与替代结论见 `docs/hmi/build_env_check.md` |
| ITEM-002 | 渲染路径选型 spike（qmlglsink 主 / RGA+纹理回退） | **已在板端实测并选定路径**：`cpu`（SELECTED）；`gl` 因缺 `qmlglsink`/Qt QML 包不可用（保留可开启候选）；数据与证据见 `docs/hmi/build_env_check.md` §14 与 `spikes/render_path/spike_core.c` |
| ITEM-003 | 分层 CMake 工程骨架与三档构建入口（`build.sh`） | **已交付**：`railhmi_core`（纯 stdlib）/`railhmi_platform`（日志/文件存储+可选 SQLite）/`railhmi_media`（target GStreamer+librga）/`railhmi`（Qt/QML）；板端 `CoreTests` 构建成功且 **9/9 测试通过**、`MediaCheck` 媒体层编译成功；`Release`/`Preview` 缺 Qt5 Quick/Qml 开发包时**明确报错**。证据：`docs/hmi/test_evidence/ITEM-003-build/README.md` |
| ITEM-004 | `railhmi-core` 配置与状态契约层（cameras/app/thresholds/roi） | **已交付**：无依赖 YAML 解析 + 配置模型/枚举 + 校验器（报具体字段，不静默默认）；`app/config/*.yaml` 基线；启动校验入口 `railhmi-config-check`；板端 CoreTests **40/40 通过**，坏配置报 `thresholds.confirm_frames` 并非零退出。证据：`docs/hmi/test_evidence/ITEM-004-config/README.md` |
| ITEM-005.. | 主题、导航、注册表运行时、取流、页面、推理、报警、录像、回放、权限、恢复、打包 | 未实现 |

> 本文件是工程构建/运行入口说明；三档构建见 `build.sh`（`Release`/`Preview`/`CoreTests`，另 `MediaCheck` 媒体层检查）。UI/取流/页面在后续工作项接入。

---

## 目录

```
app/
  README.md               # 本文件：构建/运行入口说明
  CMakeLists.txt          # 分层工程（core/platform/media/ui 子目录）
  build.sh                # 构建入口: Release | Preview | CoreTests | MediaCheck | check
  src/
    core/                 # railhmi_core: 纯业务逻辑（无 Qt/GStreamer/相机/SQLite）
      include/railhmi/core/{version,yaml,config}.h
      tools/config_check.cpp   # 启动配置校验入口（railhmi-config-check）
    platform/             # railhmi_platform: 日志/文件存储 + 可选 SQLite(SqliteStore)
    media/                # railhmi_media: target 专用 GStreamer + librga
    ui/                   # railhmi: Qt/QML 应用（qml/main.qml；启动即校验配置）
  config/                 # 运行配置: cameras.yaml / app.yaml / thresholds.yaml / roi.yaml
  tests/core/             # railhmi-core-tests（最小工具链可跑；含配置域 TC-CORE-01）
  tools/
    env_probe.py          # 依赖/环境只读探针（标准库；目标板与主机均可运行）
    run_env_check.sh      # 探针运行入口（输出 md/raw/json 到 test_evidence）
    board_env_probe.py    # 经登记 SSH 入口的板端只读探针运行器（受信任 known_hosts + 全超时）
    board_build.py        # 经登记 SSH 入口的板端构建运行器（上传 app/ + build.sh 三档）
  spikes/render_path/     # ITEM-002 渲染路径选型 spike
    main.cpp GlView.qml CpuView.qml qml.qrc
    CMakeLists.txt        # 板端本机构建（Qt5 + GStreamer + librga）
    spike_core.c          # 无 Qt 核心管线测量
    analyze.py            # 选择判据（--selftest 主机可跑）
    run_render_spike.sh   # 驱动（--dry-run 可在无板环境校验入口）
```

---

## 运行：依赖/环境核验（当前唯一可执行入口）

目标板（RK3588，Ubuntu 22.04 aarch64，待授权后执行）：

```sh
python3 app/tools/env_probe.py \
  --out  docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.md \
  --raw-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.raw.txt \
  --json-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.json
```

或使用入口脚本（默认输出目录 `docs/hmi/test_evidence/ENV-B-dependency`）：

```sh
sh app/tools/run_env_check.sh
# 相机/NVR 网络探测需授权，显式开启：
sh app/tools/run_env_check.sh docs/hmi/test_evidence/ENV-B-dependency --probe-cameras
```

- 只读探测：不安装、不修改系统、不写产品目录以外位置；无第三方依赖。
- 相机/NVR 端口探测**默认关闭**，需项目授权。
- 结果口径见 `docs/hmi/build_env_check.md` §0/§12：**主机结果不等于目标板结论**。

### 板端依赖基线（ITEM-001，经登记 SSH 入口）

```sh
python app/tools/board_env_probe.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --out-dir docs/hmi/test_evidence/ENV-B-dependency \
  --stamp <stamp> --connect-timeout 10 --command-timeout 90 --probe-cameras
```
- 使用受信任 `known_hosts`（默认 RejectPolicy），连接/命令/SFTP 全超时；凭据仅从环境变量读取，不落日志。
- 只读探测；写入仅限目标 `/tmp`。产出 `.md/.raw.txt/.json/.commands.json`（含主机指纹与逐条命令）。
- 结果与三态缺口见 `docs/hmi/build_env_check.md` §2/§3。

---

## 运行：渲染路径选型 spike（ITEM-002）

```sh
# 主机：只校验入口与判据（不构建、不访问设备）
python3 app/spikes/render_path/analyze.py --selftest
bash    app/spikes/render_path/run_render_spike.sh --dry-run

# 目标板：真实测量（需项目授权与可用相机）
cd app/spikes/render_path
./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot
# 无相机时用 ENV-C 替代源（预录文件/合成）完成协商与换算测量
./run_render_spike.sh --source file --file /path/to/clip.mp4 --duration 20 --snapshot
./run_render_spike.sh --source test --duration 10
```

结果与判据说明: `docs/hmi/render_path_spike.md`、`docs/hmi/build_env_check.md` §14。
未实测前**不选定主路径**。

---

## 构建（ITEM-003 已落地）

目标构建沿用项目既定模式：**板端本机编译，无交叉工具链**。分层：`railhmi_core`（纯 stdlib）/`railhmi_platform`（日志/文件存储 + 可选 SQLite）/`railhmi_media`（target GStreamer+librga）/`railhmi`（Qt/QML）。

```sh
# 目标板（aarch64 本机构建）——需 Qt5 Quick/Qml + GStreamer + librga
cd <app>
./build.sh Release                # RAILHMI_BUILD_TARGET
./build.sh MediaCheck             # 仅 railhmi_media（GStreamer+librga，无 Qt）编译检查

# 主机开发预览（x86_64 模拟输入）——需宿主 Qt5 Quick/Qml
./build.sh Preview                # RAILHMI_BUILD_PREVIEW + RAILHMI_SIMULATION

# 最小工具链单元测试（C++ 编译器 + CMake；不依赖 Qt/GStreamer/相机）
./build.sh CoreTests && ./build-core/tests/railhmi-core-tests

# 只做依赖自检
./build.sh check
```

经登记 SSH 入口的板端构建（代传 `app/` 并执行）：

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-003-build --stamp <tag> --run-tests
# 其它: --mode MediaCheck | Release | Preview | check
```

- 无对应工具链时 `build.sh` 逐项打印缺失依赖并退出 1，**不静默失败**。
- 依赖与版本以 `docs/hmi/build_env_check.md` 的板端核验结果为准；构建结果见 `docs/hmi/test_evidence/ITEM-003-build/README.md`。
- 主机编译不代表目标板运行。
