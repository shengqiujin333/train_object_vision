#pragma once

// railhmi-core: 录像 provider 状态机与覆盖分类（视频 ITEM-013 / 接口契约 §8 IF-07）。
//
// IRecordingProvider 方法族：querySegments/queryGaps/playSegment/exportSegment，
// 每次带 request_id 与 deadline_mono_us，返回 status=ok|partial|unavailable|error 及稳定 reason。
// 覆盖按**真实可用片段**的实际区间计算：无覆盖 -> ok 空列表（不称可播放）；部分覆盖 -> partial。
// 不可用原因区分 UNCONFIGURED/UNVERIFIED/NOT_FOUND/AUTH_FAILED/TIMEOUT/UNSUPPORTED/STORAGE_ERROR。
// 纯逻辑 + stdlib；真实文件可读/可解码验证与解码播放由 media::LocalMediaProvider 完成。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

enum class ProviderStatus { Ok, Partial, Unavailable, Error };
const char* to_string(ProviderStatus s);
bool parse_provider_status(const std::string& s, ProviderStatus* out);

// 稳定 reason token（可空串=无异常）。
namespace provider_reason {
inline const char* kNone = "";
inline const char* kUnconfigured = "UNCONFIGURED";
inline const char* kUnverified = "UNVERIFIED";
inline const char* kNotFound = "NOT_FOUND";
inline const char* kAuthFailed = "AUTH_FAILED";
inline const char* kTimeout = "TIMEOUT";
inline const char* kUnsupported = "UNSUPPORTED";
inline const char* kStorageError = "STORAGE_ERROR";
}  // namespace provider_reason

struct ProviderRequest {
    std::string request_id;
    std::string camera_id;
    long long t0_ms = 0;
    long long t1_ms = 0;
    long long deadline_mono_us = 0;   // 0 = 无 deadline
    long long duration_ms() const { return t1_ms - t0_ms; }
};

// deadline 是否已过（deadline<=0 视为无期限）。
bool provider_deadline_expired(const ProviderRequest& r, long long now_mono_us);

// 供覆盖分类使用的片段引用（usable 由 media 层验证文件可读/可解码后给出）。
struct SegmentRef {
    std::string camera_id;
    long long t0_ms = 0;
    long long t1_ms = 0;
    std::string path;
    SegmentKind kind = SegmentKind::Continuous;
    long long size_bytes = 0;
    bool usable = false;
    bool complete = false;
};

// [t0,t1) 内可用片段覆盖的综合毫秒数（并集，去重叠）。
long long covered_ms_in_window(long long t0_ms, long long t1_ms, const std::vector<SegmentRef>& segments);

struct SegmentQueryResult {
    std::string request_id;
    std::string camera_id;
    long long window_t0_ms = 0;
    long long window_t1_ms = 0;
    ProviderStatus status = ProviderStatus::Unavailable;
    std::string reason;
    bool source_configured = true;
    std::vector<SegmentRef> segments;      // 与窗口重叠者（含 unusable，供诊断）
    std::vector<RecordingGapRow> gaps;     // 与窗口重叠的缺口
    long long covered_ms = 0;
    bool coverage_complete = false;
};

// 选择与窗口重叠的片段 + 缺口，并按可用覆盖分类状态。
//  !source_configured           -> unavailable / UNCONFIGURED（不生成模拟片段）
//  无重叠片段                    -> ok / ""（空列表=查询成功无覆盖，不等于可播放）
//  有重叠但无可用片段            -> unavailable / NOT_FOUND
//  可用覆盖 >= 窗口              -> ok / ""，coverage_complete=true
//  部分可用覆盖                  -> partial / ""
SegmentQueryResult classify_segments(const ProviderRequest& req,
                                     const std::vector<SegmentRef>& candidates,
                                     const std::vector<RecordingGapRow>& all_gaps,
                                     bool source_configured);

struct PlaybackResult {
    std::string request_id;
    ProviderStatus status = ProviderStatus::Error;
    std::string reason;
    long long requested_pos_ms = 0;
    long long actual_pos_ms = -1;     // 实际解码 position；-1=未到达
    bool reached = false;
    long long decoded_frames = 0;
};

struct ExportResult {
    std::string request_id;
    ProviderStatus status = ProviderStatus::Error;
    std::string reason;
    std::string path;
    long long size_bytes = 0;
    bool complete = false;
};

}  // namespace core
}  // namespace railhmi
