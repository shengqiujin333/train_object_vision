#pragma once

// railhmi-media: 本地录像 provider 与回放媒体链路（视频 ITEM-013，target 专用）。
//
// 以真实可读/可定位/可解码媒体返回 querySegments/queryGaps/playSegment/exportSegment；
// play/seek 回报**实际解码 position**（非请求接受）；缩略图仅返回媒体生成的可读引用。
// NVR 未配置 -> unavailable/UNCONFIGURED，不生成模拟片段。
// 公开头不含 GStreamer；解码/封装细节在 .cpp 内部。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"
#include "railhmi/core/recording_provider.h"

namespace railhmi {
namespace media {

struct MediaProviderConfig {
    std::string export_dir;             // exportSegment 目标目录
    std::string thumbnail_dir;          // 缩略图输出目录
    int verdict_timeout_ms = 8000;      // 可读/可解码判定与封装的有限等待
    int playback_media_ms = 1200;       // 回放推进的媒体时长上界
    int seek_tolerance_ms = 1500;       // 实际 position 与请求的容差
};

class LocalMediaProvider {
public:
    LocalMediaProvider(std::vector<core::RecordingSegmentRow> segments,
                       std::vector<core::RecordingGapRow> gaps, MediaProviderConfig cfg);

    core::SegmentQueryResult query_segments(const core::ProviderRequest& req, long long now_mono_us);
    core::SegmentQueryResult query_gaps(const core::ProviderRequest& req, long long now_mono_us) const;
    // 解码回放：回报实际解码 position；请求接受不等于播放成功。
    core::PlaybackResult play_segment(const core::ProviderRequest& req, const std::string& path,
                                      long long from_ms, long long now_mono_us);
    core::ExportResult export_segment(const core::ProviderRequest& req, const std::string& path,
                                      long long now_mono_us);
    // 媒体生成的缩略图可读引用；失败返回空串。
    std::string thumbnail(const std::string& path, long long at_ms, const std::string& name);

    const MediaProviderConfig& config() const { return cfg_; }

private:
    bool file_usable(const std::string& path, long long* size_bytes, std::string* err) const;
    const core::RecordingSegmentRow* find_segment(const std::string& path) const;

    std::vector<core::RecordingSegmentRow> segments_;
    std::vector<core::RecordingGapRow> gaps_;
    MediaProviderConfig cfg_;
};

// NVR 未登记协议/账户/通道映射：全部返回 unavailable/UNCONFIGURED，不生成模拟片段。
class NvrProvider {
public:
    core::SegmentQueryResult query_segments(const core::ProviderRequest& req) const;
    core::PlaybackResult play_segment(const core::ProviderRequest& req) const;
    core::ExportResult export_segment(const core::ProviderRequest& req) const;
};

}  // namespace media
}  // namespace railhmi
