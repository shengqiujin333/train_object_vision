// railhmi-media: 温度采样实现（视觉推理 ITEM-009）。只读 sysfs，有限候选。

#include "railhmi/media/thermal_sensor.h"

#include <cstdio>
#include <fstream>
#include <utility>

namespace railhmi {
namespace media {

namespace {

std::string zone_label(const std::string& path) {
    // 取 "<root>/thermal_zoneN/temp" 中的 thermal_zoneN 作为稳定标签。
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    const std::string dir = path.substr(0, slash);
    const std::size_t slash2 = dir.find_last_of('/');
    return slash2 == std::string::npos ? dir : dir.substr(slash2 + 1);
}

bool read_text(const std::string& path, std::string* out) {
    std::ifstream f(path);
    if (!f) return false;
    char buf[64] = {0};
    f.read(buf, sizeof(buf) - 1);
    if (!f && !f.eof()) return false;
    *out = std::string(buf);
    return true;
}

}  // namespace

ThermalSensor::ThermalSensor(ThermalSensorConfig cfg) : cfg_(std::move(cfg)) { discover(); }

void ThermalSensor::discover() {
    paths_.clear();
    if (!cfg_.zone_paths.empty()) {
        for (const auto& p : cfg_.zone_paths) paths_.push_back(p);
        return;
    }
    if (cfg_.max_zones <= 0) return;
    for (int i = 0; i < cfg_.max_zones; ++i) {
        const std::string p = cfg_.root + "/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream f(p);
        if (f.good()) paths_.push_back(p);
    }
}

railhmi::core::ThermalSample ThermalSensor::read(long long now_mono_ms) const {
    std::vector<std::pair<std::string, std::string>> zones;
    zones.reserve(paths_.size());
    std::string first_error;
    for (const auto& p : paths_) {
        std::string text;
        if (!read_text(p, &text)) {
            if (first_error.empty()) first_error = "unreadable zone";
            continue;
        }
        zones.emplace_back(zone_label(p), text);
    }
    railhmi::core::ThermalSample s =
        railhmi::core::pick_thermal_sample(zones, now_mono_ms, cfg_.limits);
    if (s.valid) {
        last_error_.clear();
    } else if (!first_error.empty()) {
        last_error_ = first_error;
    } else if (paths_.empty()) {
        last_error_ = "no thermal zone candidate under " + cfg_.root;
    } else {
        last_error_ = s.reason;
    }
    return s;
}

std::string ThermalSensor::source_token() const { return paths_.empty() ? "unavailable" : "sysfs"; }

}  // namespace media
}  // namespace railhmi
