#include "railhmi/core/metrics.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace railhmi {
namespace core {

const char* to_string(SourceClass s) {
    switch (s) {
        case SourceClass::Rtsp: return "rtsp";
        case SourceClass::File: return "file";
        case SourceClass::Simulation: return "simulation";
    }
    return "?";
}

const char* to_string(MeasurementStatus s) {
    switch (s) {
        case MeasurementStatus::Recorded: return "recorded";
        case MeasurementStatus::ToBeExecuted: return "to_be_executed";
        case MeasurementStatus::NotApplicable: return "not_applicable";
    }
    return "?";
}

LatencyHistogram::LatencyHistogram(std::size_t max_samples)
    : max_(max_samples ? max_samples : 1) {}

void LatencyHistogram::add(long long us) {
    const auto it = std::lower_bound(samples_.begin(), samples_.end(), us);
    samples_.insert(it, us);
    if (samples_.size() > max_) samples_.erase(samples_.begin());
}

void LatencyHistogram::clear() { samples_.clear(); }

long long LatencyHistogram::min_us() const { return samples_.empty() ? -1 : samples_.front(); }
long long LatencyHistogram::max_us() const { return samples_.empty() ? -1 : samples_.back(); }

double LatencyHistogram::mean_us() const {
    if (samples_.empty()) return -1.0;
    long double sum = 0;
    for (long long v : samples_) sum += v;
    return static_cast<double>(sum / samples_.size());
}

long long LatencyHistogram::percentile(double p) const {
    if (samples_.empty()) return -1;
    if (p <= 0) return samples_.front();
    if (p >= 100) return samples_.back();
    // 最近秩（nearest-rank）：rank = ceil(p/100 * n)。用 p*n/100 降低浮点舍入。
    long double rank = std::ceil((static_cast<long double>(p) * static_cast<long double>(samples_.size())) /
                                     100.0L - 1e-9L);
    if (rank < 1) rank = 1;
    if (rank > static_cast<long double>(samples_.size())) rank = static_cast<long double>(samples_.size());
    return samples_[static_cast<std::size_t>(rank) - 1];
}

namespace {

std::string num(double v) {
    if (v < 0) return "-1";
    std::ostringstream os;
    os.precision(6);
    os << std::fixed << v;
    return os.str();
}

std::string lat_json(const char* name, const LatencyHistogram& h) {
    std::ostringstream os;
    os << "\"" << name << "\":{\"n\":" << h.count() << ",\"min\":" << h.min_us()
       << ",\"p50\":" << h.percentile(50) << ",\"p90\":" << h.percentile(90)
       << ",\"p99\":" << h.percentile(99) << ",\"max\":" << h.max_us()
       << ",\"mean\":" << num(h.mean_us()) << "}";
    return os.str();
}

}  // namespace

std::string measurement_json(const MeasurementRecord& r) {
    std::ostringstream os;
    os << "{";
    os << "\"source_class\":\"" << to_string(r.source_class) << "\",";
    os << "\"status\":\"" << to_string(r.status) << "\",";
    os << "\"reason\":\"" << r.reason << "\",";
    os << "\"label\":\"" << r.label << "\",";
    os << "\"window_s\":" << num(r.window_s) << ",";
    os << "\"throughput\":{\"source_fps\":" << num(r.throughput.source_fps)
       << ",\"display_fps\":" << num(r.throughput.display_fps)
       << ",\"cpu_pct\":" << num(r.throughput.cpu_pct)
       << ",\"rss_kb\":" << r.throughput.rss_kb
       << ",\"npu_pct\":" << num(r.throughput.npu_pct)
       << ",\"temp_c\":" << num(r.throughput.temp_c)
       << ",\"npu_note\":\"" << r.throughput.npu_note << "\"},";
    os << "\"latency\":{" << lat_json("arrival_decode", r.arrival_decode) << ","
       << lat_json("decode_convert", r.decode_convert) << ","
       << lat_json("convert_present", r.convert_present) << ","
       << lat_json("end_to_end", r.end_to_end) << "},";
    os << "\"sync\":{\"available\":" << (r.sync.available ? "true" : "false")
       << ",\"in_sync\":" << (r.sync.in_sync ? "true" : "false")
       << ",\"max_deviation_ms\":" << r.sync.max_deviation_ms << "},";
    os << "\"storage\":{\"measured_bitrate_bps\":" << num(r.storage.measured_bitrate_bps)
       << ",\"clip_bytes\":" << r.storage.clip_bytes
       << ",\"free_bytes\":" << r.storage.free_bytes << ",\"total_bytes\":" << r.storage.total_bytes
       << "},";
    os << "\"recovery\":{\"attempts\":" << r.recovery.attempts
       << ",\"last_recovery_ms\":" << r.recovery.last_recovery_ms << "}";
    os << "}";
    return os.str();
}

std::string measurements_archive_json(const std::vector<MeasurementRecord>& records) {
    std::ostringstream os;
    os << "{\"schema\":\"railhmi.measure.v1\",\"records\":[";
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i) os << ",";
        os << measurement_json(records[i]);
    }
    os << "]}";
    return os.str();
}

}  // namespace core
}  // namespace railhmi
