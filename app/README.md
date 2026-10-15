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
| ITEM-003 | 工程骨架与板端本机构建脚本（Qt QML 目标 + 预览开关） | 未实现 |
| ITEM-004.. | 主题、导航、注册表、取流、页面、推理、报警、录像、回放、权限、恢复、打包 | 未实现 |

> 本文件是工程构建/运行入口说明；随着 ITEM-003 落地，将补充 CMake 目标与 `build.sh`/`deploy.sh`。

---

## 目录

```
app/
  README.md               # 本文件：构建/运行入口说明
  tools/
    env_probe.py          # 依赖/环境只读探针（标准库；目标板与主机均可运行）
    run_env_check.sh      # 探针运行入口（输出 md/raw/json 到 test_evidence）
    board_env_probe.py    # 经登记 SSH 入口的板端只读探针运行器（受信任 known_hosts + 全超时）
  spikes/render_path/     # ITEM-002 渲染路径选型 spike
    main.cpp GlView.qml CpuView.qml qml.qrc
    CMakeLists.txt        # 板端本机构建（Qt5 + GStreamer + librga）
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

## 构建（计划，尚未实现 — ITEM-003）

目标构建沿用项目既定模式：**板端本机编译，无交叉工具链**。

```sh
# 目标板（aarch64 本机构建）——待 ITEM-003 落地
cd /home/admin/railhmi/src/app
./build.sh Release
./deploy.sh                       # 安装到 /home/admin/railhmi

# 主机开发预览（x86_64 模拟输入）——待 ITEM-003 落地
./build.sh Preview                # -DRAILHMI_SIMULATION=ON
```

依赖与版本以 `docs/hmi/build_env_check.md` 的板端核验结果为准，不在此写死。
