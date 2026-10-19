#pragma once

// railhmi-media: 温度采样（视觉推理 ITEM-009）。
//
// 从 sysfs 的 thermal zone 读取**实测**温度（毫摄氏度），交 `core::pick_thermal_sample`
// 解析并取多 zone 最大值；采样源不存在/内容非法/超范围时返回 `valid=false` + 稳定 reason，
// **不伪造温度、不把未知当作低温**。注入值只能经 `core::injected_thermal_sample` 显式构造，
// 本类不提供注入通道（注入由探针/测试直接构造样本并标记 injected）。
//
// 只读、有限次数打开（最多 max_zones 个候选）；不安装/不修改系统。

#include <string>
#include <vector>

#include "railhmi/core/vision_worker_schedule.h"

namespace railhmi {
namespace media {

struct ThermalSensorConfig {
    // 显式 zone 文件路径（如 /sys/class/thermal/thermal_zone0/temp）；空则按 root 探测 0..max_zones-1。
    std::vector<std::string> zone_paths;
    std::string root = "/sys/class/thermal";
    int max_zones = 16;
    railhmi::core::ThermalLimits limits;
};

class ThermalSensor {
public:
    explicit ThermalSensor(ThermalSensorConfig cfg = ThermalSensorConfig());

    // 采样一次；now_mono_ms 为调用方单调时钟（写入样本用于年龄判定）。
    railhmi::core::ThermalSample read(long long now_mono_ms) const;

    // 已探测到的 zone 路径（证据用）。
    const std::vector<std::string>& zones() const { return paths_; }
    // 最近一次采样失败说明（脱敏；成功时为空串）。
    const std::string& last_error() const { return last_error_; }
    // 采样源类别："sysfs"（存在至少一个候选）或 "unavailable"。
    std::string source_token() const;

private:
    void discover();

    ThermalSensorConfig cfg_;
    std::vector<std::string> paths_;
    mutable std::string last_error_;
};

}  // namespace media
}  // namespace railhmi
