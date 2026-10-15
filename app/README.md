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
| ITEM-001 | 目标构建/运行依赖探针与基线核验入口 | 源码已交付（`tools/env_probe.py`）；**板端核验未执行**（设备工具禁用） |
| ITEM-002 | 渲染路径选型 spike（qmlglsink 主 / RGA+纹理回退） | 未实现 |
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
