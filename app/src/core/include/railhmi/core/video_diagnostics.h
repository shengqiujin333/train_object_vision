#pragma once

// railhmi-core: 视频每路结构化诊断与整条脱敏（视频 ITEM-016）。
//
// 每路输出结构化指标：state/fps/最近帧单调与墙钟/重连次数/质量标志/display_dropped/
// ai_dropped/recording_overflow/gap_count/实际 attempt_id/last_error（已脱敏）。
// 诊断三态：在线(Online)/未核验(Unverified)/缺口(Gap)；结构化单行 JSON 可经 health 日志按日滚动。
// 凭据、含鉴权 URL 与 pipeline/bus 原文绝不进入日志/DB/工件/命令行——redact_error 整条脱敏。
// 纯逻辑 + stdlib；实际指标由 media 从 StreamStats 采集，能力/版本由 video_capability 提供。

#include <string>

#include "railhmi/core/health.h"

namespace railhmi {
namespace core {

// 每路视频结构化指标。
struct VideoChannelMetrics {
    std::string camera_id;
    std::string state;                 // 六态名（CONNECTING/LIVE/FROZEN/OFFLINE/RECONNECTING/DEGRADED）
    double fps = 0.0;
    long long last_frame_mono_ms = 0;  // 最近帧单调毫秒（0=尚无）
    long long last_frame_wall_ms = -1; // 最近帧墙钟毫秒（-1=未校准/不可用）
    long long reconnect_count = 0;
    std::string quality_flags;         // 质量标志 describe()
    long long display_dropped = 0;
    long long ai_dropped = 0;
    long long recording_overflow = 0;
    long long gap_count = 0;
    std::string attempt_id;            // 实际恢复 attempt 标识（无则 "none"）
    std::string last_error;            // 已整条脱敏
};

// 诊断三态：在线 / 未核验 / 缺口。
enum class DiagnosticPresence { Online, Unverified, Gap };
const char* to_string(DiagnosticPresence p);
const char* presence_label(DiagnosticPresence p);   // 在线 / 未核验 / 缺口

// 分类：
//  - gap_count>0 或 state=FROZEN -> 缺口（已知缺口优先暴露）；
//  - 有新鲜可呈现帧（presentable 由 state 判）-> 在线；
//  - 其余（未连接/未观测）-> 未核验。
DiagnosticPresence classify_presence(const VideoChannelMetrics& m);

// 结构化日志记录/单行 JSON（source="video"）。
LogRecord video_metric_log_record(const VideoChannelMetrics& m, long long ts_wall_ms);
std::string video_metric_json_line(const VideoChannelMetrics& m, long long ts_wall_ms);

// 实际恢复 attempt 标识：camera + generation（无 -> "none"）。
std::string make_attempt_id(const std::string& camera_id, unsigned long long generation);

// 整条脱敏：移除 RTSP userinfo、password/token 值、location 值；若仍含 pipeline/bus 原文
// （rtspsrc/GstPipeline/GST_MESSAGE/pipeline0）则整条替换为 <redacted>。绝不返回凭据/鉴权 URL/管线原文。
std::string redact_error(const std::string& text);
// 便捷判定：脱敏后文本是否仍疑似含凭据/鉴权 URL/管线原文。
bool contains_credentials(const std::string& text);

}  // namespace core
}  // namespace railhmi
