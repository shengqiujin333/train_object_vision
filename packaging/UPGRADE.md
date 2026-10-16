# railhmi 升级与回滚（ITEM-025）

目标运行目录：`/opt/railhmi/{bin,config,models,resources,data,logs}`（`data`/`logs` 为运行期数据，升级不动）。

## 版本
- 当前版本见 `/opt/railhmi/VERSION`（源 `packaging/VERSION`）。
- 启动前自检入口 `railhmi-version-check`（`bin/` 内），核对版本/依赖并输出摘要。

## 升级
1. 备份当前版本：`cp -a /opt/railhmi/bin /opt/railhmi/bin.bak.$(date +%Y%m%d%H%M)`；`data`/`logs`/`config` 保留。
2. 在板端本机构建新版本：`cd <src>/app && bash build.sh Release`（板端原生 aarch64 构建，不交叉编译）。
3. 停应用：`systemctl stop railhmi`。
4. 覆盖二进制与配置：`bash packaging/install.sh`（幂等；仅覆盖 bin/config/resources/VERSION）。
5. 启动：`systemctl start railhmi`；确认 `systemctl status railhmi` 为 active，界面进入可用状态（启动恢复进度完成）。

## 回滚
1. `systemctl stop railhmi`。
2. 还原上一版本：`cp -a /opt/railhmi/bin.bak.<tag>/railhmi /opt/railhmi/bin/railhmi`（必要时同时还原 `config`）。
3. `systemctl start railhmi`；核对 `/opt/railhmi/VERSION` 与界面版本位。

## 约束
- **不修改 `cpu-performance.service`**；本应用不改 CPU governor、不重启整机、不改网络/驱动/相机设置。
- systemd 单元 `Restart=always`：崩溃/退出后自动拉起（间隔 3s）；异常持续时保留日志 `/opt/railhmi/logs/`。
- 数据库/录像位于 `data/`，升级与回滚均不删除；WAL 允许断电后恢复。
