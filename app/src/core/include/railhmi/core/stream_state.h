#pragma once

// railhmi-core: 流状态机与质量判定（ITEM-005）。
// 六态: CONNECTING / LIVE / FROZEN / OFFLINE / RECONNECTING / DEGRADED。
// 首帧超时、无新帧冻结、重连退避与阈值升级设备事件；非 LIVE 禁止把旧帧当实时。
// 纯逻辑 + 显式单调时钟（由调用方传入 now_ms），可用模拟时钟单元测试。
//
// 仅依赖 C++ 标准库；不含 Qt/GStreamer/相机。

#include <cstdint>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

enum class StreamState { Connecting, Live, Frozen, Offline, Reconnecting, Degraded };
const char* to_string(StreamState s);

// 质量标志（HLT-003）。颜色/绿帧伪影属红外旧口径，本期可见光不做。
struct QualityFlags {
    bool too_dark = false;
    bool too_bright = false;
    bool occluded = false;
    bool blurred = false;

    bool any() const { return too_dark || too_bright || occluded || blurred; }
    std::string describe() const;   // 例如 "too_dark,blurred" 或 "" 
};

// 帧统计（由媒体/质量层计算，core 只做判定，不处理图像）。
struct FrameStats {
    double mean_luma = -1.0;     // 平均亮度 [0,255]
    double stddev_luma = -1.0;   // 亮度标准差
    double lap_var = -1.0;       // 拉普拉斯方差（清晰度），-1 表示不可用
};

struct QualityThresholds {
    double dark_mean = 40.0;       // mean < dark_mean -> 过暗
    double bright_mean = 220.0;    // mean > bright_mean -> 过曝
    double min_stddev = 3.0;       // stddev < min_stddev -> 遮挡（近似均匀）
    double blur_lapvar = 30.0;     // lap_var < blur_lapvar -> 模糊（lap_var>0 时判定）
};

// 依据帧统计判定质量标志。无效统计（<0）不产生对应标志。
QualityFlags classify_quality(const FrameStats& s, const QualityThresholds& t);

enum class StreamEventKind {
    Started,          // 进入 CONNECTING
    FirstFrame,       // 首次出帧 -> LIVE/DEGRADED
    Freeze,           // LIVE/DEGRADED 超时无新帧 -> FROZEN
    ReconnectAttempt, // 发起一次重连尝试（计数 +1）
    Escalated,        // 达重连阈值 -> OFFLINE 并升级设备事件
    Recovered,        // 恢复出帧
    SourceError,      // 上游报错
    QualityDegraded,  // 出帧但质量降级 -> DEGRADED
};

const char* to_string(StreamEventKind k);

struct StreamEvent {
    std::string camera_id;
    StreamEventKind kind = StreamEventKind::Started;
    long long t_ms = 0;            // 单调毫秒
    int reconnect_count = 0;
    std::string detail;
};

struct StreamThresholds {
    int first_frame_timeout_ms = 5000;  // CONNECTING 内未出首帧 -> 失败
    int freeze_timeout_ms = 2000;       // 无新帧超时 -> FROZEN
    int reconnect_threshold = 3;        // 重连次数达阈值 -> OFFLINE + 升级
    int reconnect_backoff_ms = 1000;    // RECONNECTING 到下次 CONNECTING 的退避
};

// 单路相机流状态机。所有时间均为单调毫秒，由调用方传入。
class StreamStateMachine {
public:
    StreamStateMachine(std::string camera_id, StreamThresholds thresholds);

    // 启动/重启取流：-> CONNECTING。
    void start(long long now_ms);

    // 收到一帧（含质量标志）。恢复/首次出帧 -> LIVE 或 DEGRADED；重连计数清零。
    void on_frame(long long now_ms, const QualityFlags& quality);

    // 上游报错（网络/解码）。-> RECONNECTING 且计数 +1；达阈值 -> OFFLINE + 升级。
    void on_source_error(long long now_ms, const std::string& detail);

    // 显式推进一次重连尝试（由取流层在真正发起重连时调用）。
    void on_reconnect_attempt(long long now_ms);

    // 时间推进：处理首帧超时、冻结、重连退避、阈值升级等时间相关转换。
    void tick(long long now_ms);

    StreamState state() const { return state_; }
    int reconnect_count() const { return reconnect_count_; }
    const std::string& camera_id() const { return camera_id_; }
    long long last_frame_ms() const { return last_frame_ms_; }
    bool has_frame() const { return last_frame_ms_ >= 0; }

    // 是否正在出帧（LIVE 或 DEGRADED；二者都有新鲜帧，DEGRADED 计为降级可用）。
    bool is_streaming() const { return state_ == StreamState::Live || state_ == StreamState::Degraded; }

    // 硬约束接口：非 LIVE（含 DEGRADED 视为 LIVE 的降级）时禁止把旧帧当作实时。
    // 调用方渲染前必须检查此接口；false 时必须渲染状态占位 + 最近帧时间戳。
    bool may_present_as_live(long long now_ms) const;

    // 事件缓冲（调用方 drain 到事件/持久化层）。
    const std::vector<StreamEvent>& events() const { return events_; }
    std::vector<StreamEvent> drain_events();
    void clear_events() { events_.clear(); }

    StreamThresholds thresholds() const { return th_; }

private:
    void emit_event(StreamEventKind k, long long now_ms, const std::string& detail = "");
    void begin_reconnect(long long now_ms, const std::string& detail);  // 计数 +1，必要时升级
    void enter(StreamState s, long long now_ms);

    std::string camera_id_;
    StreamThresholds th_;
    StreamState state_ = StreamState::Offline;
    int reconnect_count_ = 0;
    long long last_frame_ms_ = -1;
    long long state_since_ms_ = 0;   // 进入当前状态的时间
    std::vector<StreamEvent> events_;
};

}  // namespace core
}  // namespace railhmi
