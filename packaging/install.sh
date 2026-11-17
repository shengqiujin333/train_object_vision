#!/usr/bin/env bash
# railhmi 板端安装脚本（ITEM-025）。
# 目标运行目录：/opt/railhmi/{bin,config,models,resources,data,logs}
# 幂等、非交互、有限步骤；安装/启用 systemd 单元，**不修改 cpu-performance.service**。
#
# 注意：本文件必须保持 LF 行尾。若从 Windows 编辑后带 CRLF，bash 会在空行报
# "$'\r': command not found" 并提前退出（2026-11-17 一次"装了旧二进制"就是因此）。
set -euo pipefail

PREFIX="${PREFIX:-/opt/railhmi}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_NAME="railhmi.service"

# 自查行尾：出现 CR 直接报错，避免静默半途退出。
if grep -q $'\r' "${BASH_SOURCE[0]}" 2>/dev/null; then
    echo "[railhmi-install] ERROR: install.sh has CRLF line endings; run: sed -i 's/\\r$//' install.sh" >&2
    exit 1
fi

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

# 安装后自校验：源/目标二进制指纹必须一致（避免"装了旧二进制"这种静默失败）。
if [ -x "$PREFIX/bin/railhmi" ]; then
    src_md5=$(md5sum "$SRC/app/build-target/railhmi" 2>/dev/null | cut -d' ' -f1)
    dst_md5=$(md5sum "$PREFIX/bin/railhmi" | cut -d' ' -f1)
    echo "[railhmi-install] binary md5 src=$src_md5 dst=$dst_md5"
    if [ -n "$src_md5" ] && [ "$src_md5" != "$dst_md5" ]; then
        echo "[railhmi-install] ERROR: installed binary differs from build output" >&2
        exit 1
    fi
fi

# systemd 单元：安装 + 启用（不启动，由现场决定启动时机；不触碰 cpu-performance.service）
install -m 0644 "$SRC/packaging/$UNIT_NAME" "/etc/systemd/system/$UNIT_NAME"
# NPU 分核负载采样器（root 读 debugfs -> /run/railhmi/npu-load，供 admin 运行的应用显示）
install -m 0644 "$SRC/packaging/railhmi-npu-sampler.service" \
    "/etc/systemd/system/railhmi-npu-sampler.service"
systemctl daemon-reload
systemctl enable "$UNIT_NAME" || true
systemctl enable railhmi-npu-sampler.service || true
systemctl start railhmi-npu-sampler.service || true
echo "[railhmi-install] enabled $UNIT_NAME（未修改 cpu-performance.service）"
echo "[railhmi-install] 启动：systemctl start $UNIT_NAME；状态：systemctl status $UNIT_NAME"
echo "[railhmi-install] 完成。回滚见 packaging/UPGRADE.md"
