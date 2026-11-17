# railhmi 升级 / 回滚 / 现场验收手册

> 适用：RK3588 车载屏（Ubuntu 22.04 aarch64，10.1" 1280×800）。
> 本手册对应当前部署形态：**systemd 常驻**（`railhmi.service`）+ `/opt/railhmi` 运行目录。
> 板端即为编译机（本机 aarch64 gcc），**不做交叉编译**。

---

## 1. 目录与版本

| 路径 | 用途 | 升级是否覆盖 |
|---|---|---|
| `/opt/railhmi/bin/railhmi` | 可执行文件 | **是** |
| `/opt/railhmi/config/*.yaml` | 运行配置（相机/NVR/阈值/ROI） | **是**（来自仓库 `app/config/`） |
| `/opt/railhmi/config/*-credentials.env` | 私有凭据（0600，不进 Git） | 否（手工维护） |
| `/opt/railhmi/models/` | 模型文件（可选） | 否（模型独立投放） |
| `/opt/railhmi/data/` | 数据库/录像/片段 | **否**（升级不动） |
| `/opt/railhmi/logs/` | 运行日志 | **否** |
| `/opt/railhmi/VERSION` | 版本号 | 是 |
| `/etc/systemd/system/railhmi.service` | 主服务单元 | 是 |
| `/etc/systemd/system/railhmi-npu-sampler.service` | NPU 分核采样器（root） | 是 |

版本：`cat /opt/railhmi/VERSION`（源 `packaging/VERSION`）；界面顶栏/维护页也能看到
`railhmi 0.1.0 [mode=Target, simulation=off]`。

---

## 2. 升级前检查（2 分钟）

```sh
systemctl status railhmi --no-pager | head -5     # 当前是否 active
df -h /opt                                        # 至少留 2GB（日志+新二进制）
cat /opt/railhmi/VERSION                          # 记下当前版本，回滚要用
ls -l /opt/railhmi/config/*-credentials.env        # 凭据文件存在且 0600
md5sum /opt/railhmi/bin/railhmi                   # 记下当前二进制指纹
```

**备份当前二进制**（回滚入口）：

```sh
TAG=$(date +%Y%m%d-%H%M)
sudo mkdir -p /opt/railhmi/backup/$TAG
sudo cp -a /opt/railhmi/bin/railhmi /opt/railhmi/backup/$TAG/railhmi
sudo cp -a /opt/railhmi/config      /opt/railhmi/backup/$TAG/config
echo "backup tag: $TAG"
```

---

## 3. 标准升级流程

### 3.1 同步源码到板端

从开发机把 `app/`、`packaging/`、`docs/` 同步到 `/home/admin/railhmi-ui-fix/`
（沿用 `hmi_verify/roundN_sync.sh` 或 `tools/_rk3588.py --put`）。
注意：**不要用 tar 覆盖后丢失可执行权限**，同步完执行一次 `chmod +x app/build.sh`。

### 3.2 板端本机构建

```sh
cd /home/admin/railhmi-ui-fix/app
./build.sh CoreTests          # 单元测试（最小工具链，约 1 分钟）
./build-core/tests/railhmi-core-tests | tail -3     # 期望 RESULT: PASS
./build.sh Release            # 目标构建（Qt/GStreamer/librga），约 2-5 分钟
ls -l build-target/railhmi    # 期望 aarch64 ELF
```

### 3.3 安装 + 重启

```sh
cd /home/admin/railhmi-ui-fix
sudo bash packaging/install.sh          # 幂等：覆盖 bin/config/VERSION + 两个 unit
sudo systemctl daemon-reload
sudo systemctl restart railhmi
sudo systemctl restart railhmi-npu-sampler   # 一般无需重启，随 install 已 enable/start
```

> `install.sh` 会保留 `data/`、`logs/`、`models/`；不会改动 `cpu-performance.service`。
> 首次部署或凭据丢失时，补一次：
> `sudo install -m 0600 app/config/camera-credentials.env app/config/nvr-credentials.env /opt/railhmi/config/`

### 3.4 验收（必须逐条过，见第 5 节）

---

## 4. 只改配置 / 只换模型

- **只改 yaml（阈值、ROI、相机地址、AI 阈值）**：改仓库 `app/config/*.yaml` →
  `sudo bash packaging/install.sh` → `sudo systemctl restart railhmi`。**无需重新编译**。
- **只换模型**：把新 `.rknn` 放到 `/opt/railhmi/models/`（或改 `app.ai.models_dir` 指向目录）→
  改 `app/config/app.yaml` 的 `ai.models[].name/version` → 重启服务。
  模型名/版本只来自配置，代码不写死；换错模型会以 `MODEL_LOAD_FAILED` 显式报出，不会静默降级。
- **改凭据**：直接编辑 `/opt/railhmi/config/{camera,nvr}-credentials.env`（保持 0600）→ 重启服务。

---

## 5. 验收清单

| # | 命令 | 期望 |
|---|---|---|
| 1 | `systemctl is-active railhmi` | `active` |
| 2 | `systemctl show railhmi -p NRestarts` | `0`（不是靠反复重启才活着） |
| 3 | `grep -c 'QML' /opt/railhmi/logs/railhmi.out.log` | `0`（无 QML 运行期错误） |
| 4 | `grep 'video workers started' /opt/railhmi/logs/railhmi.out.log \| tail -1` | `6 / 6` |
| 5 | `grep 'ai: worker started' … \| tail -1` | `lanes=3 load_error=none detect_ready=true` |
| 6 | `grep 'nvr-status' … \| tail -1` | `NVR正常 (tcp connected)` |
| 7 | `grep 'I/O mode' … \| tail -1` | `hci-raw`（门磁）；无 CAP 时 `bluez-fallback` 并告警 |
| 8 | `cat /run/railhmi/npu-load` | `NPU load: Core0: …%`（分核可读） |
| 9 | 界面 | P1 三路实时、P4 六路、顶栏 `6/6 路实时 / AI模型 接入 / 录像 NVR正常` |
| 10 | P6「故障自检」 | 视频/AI/NVR/门磁/WiFi 五段全绿；WiFi 行含「驱动正常、已连接」 |

一键脚本（含静态守卫、CoreTests、构建、离屏截图、日志断言）：
`bash hmi_verify/round7_verify.sh`（脚本用 `systemctl stop/start`，不会误杀生产实例）。

---

## 6. 回滚

### 6.1 回滚二进制（最常见）

```sh
sudo systemctl stop railhmi
sudo cp -a /opt/railhmi/backup/<TAG>/railhmi /opt/railhmi/bin/railhmi
sudo systemctl start railhmi
sudo systemctl show railhmi -p NExecStart   # 确认启动时间已更新
md5sum /opt/railhmi/bin/railhmi             # 与备份一致
```

### 6.2 同时回滚配置

```sh
sudo systemctl stop railhmi
sudo cp -a /opt/railhmi/backup/<TAG>/config/*.yaml /opt/railhmi/config/
sudo systemctl start railhmi
```

### 6.3 整机回滚

```sh
sudo systemctl disable --now railhmi railhmi-npu-sampler
sudo rm -rf /opt/railhmi          # data/ 里有录像，先备份再删！
```

> systemd 单元里 `Restart=always` + `StartLimitIntervalSec=0`：**只要 X 起来就一定会被拉起**；
> 若界面长时间不出现，先看 `journalctl -b -u railhmi` 而不是反复 `systemctl restart`。

---

## 7. 常见问题

| 现象 | 排查 |
|---|---|
| 重启后屏上没程序 | `systemctl is-active railhmi`；`journalctl -b -u railhmi \| tail -20`；确认 `DISPLAY=:0` 的桌面已登录（lightdm 自动登录） |
| 有进程但黑屏 | `grep -i qml /opt/railhmi/logs/railhmi.out.log`；确认 `QT_QPA_PLATFORM=xcb` 且 :0 存在 |
| 六路不全 | 看 `video channel "<CAM-xx>"` 状态与 `last_error`；多为相机离线/凭据缺失（`credential_env`） |
| 顶栏 AI 未接入 | `grep 'ai: worker started'`：`load_error` 非空即模型路径/版本不对；`detect_ready=false` 看 reason |
| 历史录像报认证失败 | 先看 `grep nvr-session` —— 已有会话过期自动重登；仍失败才是账号/权限问题 |
| 门磁不刷新 | `getcap /opt/railhmi/bin/railhmi` 应有 `cap_net_admin,cap_net_raw=ep`（unit 的 ExecStartPre 会补） |
| NPU 显示「分核不可读」 | `systemctl status railhmi-npu-sampler`；`cat /run/railhmi/npu-load` |

---

## 8. 升级记录（每次升级补一行）

| 日期 | 版本 | 变更摘要 | 验收 | 回滚点 |
|---|---|---|---|---|
| 2026-11-17 | 0.1.0 | 视觉推理接入（三路共用检测模型 + 车前方两阶段）、P1 检测框/闪烁红框、P5 换模型、P6 日志+五项操作、车前方 1:1 裁剪推理、NVR 会话自动重登、systemd 常驻 + NPU 分核采样、WiFi 自检 | PASS（CoreTests 1544/0；六路/AI/NVR/门磁/QML 0 错误） | 无（首次服务化） |
