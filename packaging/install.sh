#!/usr/bin/env bash
# railhmi 板端安装脚本（ITEM-025）。
# 目标运行目录：/opt/railhmi/{bin,config,models,resources,data,logs}
# 幂等、非交互、有限步骤；安装/启用 systemd 单元，**不修改 cpu-performance.service**。
set -euo pipefail

PREFIX="${PREFIX:-/opt/railhmi}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_NAME="railhmi.service"

echo "[railhmi-install] prefix=$PREFIX src=$SRC"

for d in bin config models resources data logs; do
    install -d -m 0755 "$PREFIX/$d"
done

# 配置与资源（无凭据）；模型目录由模型/视觉职责提供，缺失时明确提示不阻断安装
install -m 0644 "$SRC"/app/config/*.yaml "$PREFIX/config/"
if [ -d "$SRC/resources" ]; then install -m 0644 "$SRC"/resources/* "$PREFIX/resources/" 2>/dev/null || true; fi
if [ -d "$SRC/models" ]; then install -m 0644 "$SRC"/models/* "$PREFIX/models/" 2>/dev/null || true; else
    echo "[railhmi-install] WARN: $SRC/models 不存在；模型文件需由模型职责提供到 $PREFIX/models"
fi

# 二进制（若已构建）与版本信息
if [ -x "$SRC/app/build-target/railhmi" ]; then
    install -m 0755 "$SRC/app/build-target/railhmi" "$PREFIX/bin/railhmi"
    echo "[railhmi-install] installed binary -> $PREFIX/bin/railhmi"
else
    echo "[railhmi-install] WARN: 未找到构建产物 app/build-target/railhmi；请先在板端执行 app/build.sh Release"
fi
install -m 0644 "$SRC/packaging/VERSION" "$PREFIX/VERSION"

# systemd 单元：安装 + 启用（不启动，由现场决定启动时机；不触碰 cpu-performance.service）
install -m 0644 "$SRC/packaging/$UNIT_NAME" "/etc/systemd/system/$UNIT_NAME"
systemctl daemon-reload
systemctl enable "$UNIT_NAME" || true
echo "[railhmi-install] enabled $UNIT_NAME（未修改 cpu-performance.service）"
echo "[railhmi-install] 启动：systemctl start $UNIT_NAME；状态：systemctl status $UNIT_NAME"
echo "[railhmi-install] 完成。回滚见 packaging/UPGRADE.md"
