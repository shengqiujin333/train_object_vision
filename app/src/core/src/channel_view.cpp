#include "railhmi/core/channel_view.h"

#include <cstdio>

namespace railhmi {
namespace core {

std::string channel_state_label(const std::string& state) {
    if (state == "live") return "实时";
    if (state == "connecting") return "连接中";
    if (state == "reconnecting") return "重连中";
    if (state == "frozen") return "画面冻结";
    if (state == "offline") return "离线";
    if (state == "degraded") return "降级";
    if (state == "unstarted" || state.empty()) return "未启动";
    return state;
}

std::string format_fps(double fps, double epsilon) {
    if (fps <= epsilon) return "—";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f fps", fps);
    return std::string(buf);
}

std::string format_last_frame(long long age_ms) {
    if (age_ms < 0) return "—";
    if (age_ms < 1000) return "<1 秒前";
    const long long s = age_ms / 1000;
    if (s < 60) return std::to_string(s) + " 秒前";
    const long long m = s / 60;
    const long long rem = s % 60;
    return std::to_string(m) + " 分 " + std::to_string(rem) + " 秒前";
}

std::vector<ChannelView> build_channel_views(const std::vector<CameraSlot>& registry,
                                             const std::vector<ChannelRuntime>& runtime,
                                             long long now_ms,
                                             const ChannelViewOptions& opts) {
    std::vector<ChannelView> out;
    out.reserve(registry.size());
    for (std::size_t i = 0; i < registry.size(); ++i) {
        const CameraSlot& c = registry[i];
        ChannelView v;
        v.id = c.id;
        v.name = c.name;
        v.position = to_string(c.position);
        v.badge = c.detection_badge;
        v.ai_enabled = c.ai_enabled;
        v.slot = static_cast<int>(i);

        if (i < runtime.size() && runtime[i].has_stream) {
            const ChannelRuntime& r = runtime[i];
            v.state = to_string(r.state);
            v.fps_text = format_fps(r.fps, opts.fps_epsilon);
            if (r.last_frame_ms >= 0) {
                const long long age = now_ms - r.last_frame_ms;
                v.last_frame_text = format_last_frame(age);
                v.stale = age > opts.stale_after_ms;
            } else {
                v.last_frame_text = "—";
            }
            if (r.reconnect_count > 0) {
                v.reconnect_text = "重连 " + std::to_string(r.reconnect_count) + " 次";
            }
            v.presentable = r.presentable;
        } else {
            v.state = "unstarted";
            v.fps_text = "—";
            v.last_frame_text = "—";
            v.presentable = false;
        }
        v.state_label = channel_state_label(v.state);
        v.placeholder = !v.presentable;
        out.push_back(v);
    }
    return out;
}

}  // namespace core
}  // namespace railhmi
