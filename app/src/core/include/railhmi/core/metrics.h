#pragma once

// railhmi-core: 预算测量装置（视频 ITEM-017）。
//
// 可复用埋点与计数器：吞吐（源帧率/显示帧率/CPU/RSS/NPU/温度）、端到端延迟分位
// （到达->解码完成->转换完成->呈现）、同步偏差、存储（实测 bitrate/片段实际大小/free bytes）、
// 断流恢复耗时。按三类源（真实 RTSP/文件回放/模拟输入）分别产出可归档记录，不互相替代；
// 缺授权/未接设备如实记为 to_be_executed，不当作通过。纯逻辑 + stdlib。

#include <cstdint>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

// 三类源：真实 RTSP / 文件回放 / 模拟输入（分别留证，不互相替代）。
enum class SourceClass { Rtsp, File, Simulation };
const char* to_string(SourceClass s);

// 记录状态：已实测 / 待执行（缺授权或缺设备）/ 不适用。
enum class MeasurementStatus { Recorded, ToBeExecuted, NotApplicable };
const char* to_string(MeasurementStatus s);

// 延迟分位直方图（微秒）。有界窗口，超出丢弃最旧。
class LatencyHistogram {
public:
    explicit LatencyHistogram(std::size_t max_samples = 200000);
    void add(long long us);
    void clear();
    std::size_t count() const { return samples_.size(); }
    long long min_us() const;
    long long max_us() const;
    double mean_us() const;
    // p ∈ [0,100]；样本为空返回 -1。
    long long percentile(double p) const;

private:
    std::size_t max_;
    std::vector<long long> samples_;   // 插入时保持有序（样本量小，二分插入）
};

struct ThroughputSample {
    double source_fps = 0.0;
    double display_fps = 0.0;
    double cpu_pct = -1.0;      // -1=未测
    long long rss_kb = -1;
    double npu_pct = -1.0;      // -1=不可用（如需要 root 的 rknpu load）
    double temp_c = -1.0;
    std::string npu_note;       // 不可用时说明（不伪造）
};

struct StorageSample {
    double measured_bitrate_bps = -1.0;
    long long clip_bytes = 0;
    long long free_bytes = -1;
    long long total_bytes = -1;
};

struct RecoverySample {
    int attempts = 0;
    long long last_recovery_ms = -1;
};

struct SyncSample {
    bool available = false;
    bool in_sync = false;
    long long max_deviation_ms = 0;
};

struct MeasurementRecord {
    SourceClass source_class = SourceClass::Simulation;
    MeasurementStatus status = MeasurementStatus::ToBeExecuted;
    std::string reason;             // 例如 CREDENTIAL_MISSING / recorded
    std::string label;              // 源描述（不含凭据）
    double window_s = 0.0;
    ThroughputSample throughput;
    LatencyHistogram arrival_decode;
    LatencyHistogram decode_convert;
    LatencyHistogram convert_present;
    LatencyHistogram end_to_end;
    SyncSample sync;
    StorageSample storage;
    RecoverySample recovery;
};

// 单条可归档 JSON。
std::string measurement_json(const MeasurementRecord& r);
// 多类源汇总 JSON（含三类各自状态）。
std::string measurements_archive_json(const std::vector<MeasurementRecord>& records);

}  // namespace core
}  // namespace railhmi
