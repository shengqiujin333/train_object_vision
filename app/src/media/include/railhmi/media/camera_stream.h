#pragma once

// railhmi-media: 单路取流与流状态机接入（ITEM-013）。
// 原生 GStreamer（强制 UDP）逐帧发布 → 接入 railhmi-core 六态与质量标志；
// 统计 fps / 最近帧时间 / 重连次数；非 LIVE 不得把旧帧当作实时（presentable）。
// 无相机时可用 test/file 源运行同一管线。target 专用（依赖 GStreamer/librga）。

#include <deque>
#include <string>

#include "railhmi/core/stream_state.h"

namespace railhmi {
namespace media {

enum class SourceKind { Test, File, Rtsp };

struct CameraStreamConfig {
    std::string camera_id = "CAM-01";
    SourceKind source = SourceKind::Test;
    std::string url;      // source=rtsp（不含凭据的 URL；凭据由调用方在内存中安全构造）
    std::string file;     // source=file
    int latency_ms = 500;
    int udp_buffer_bytes = 4194304;
    railhmi::core::StreamThresholds thresholds;
    railhmi::core::QualityThresholds quality;
};

struct StreamStats {
    railhmi::core::StreamState state = railhmi::core::StreamState::Offline;
    double fps = 0.0;
    long long last_frame_ms = 0;   // 最近一帧的单调时间；0 表示尚无帧
    int reconnect_count = 0;
    bool presentable = false;      // 非 LIVE（含 DEGRADED 新鲜帧）时为 false
    std::string last_error;
};

// 单路取流：start() 建管线进入 PLAYING；pump() 推进（拉帧/驱动状态机）；stop() 收尾。
class CameraStream {
public:
    explicit CameraStream(CameraStreamConfig cfg);
    ~CameraStream();

    CameraStream(const CameraStream&) = delete;
    CameraStream& operator=(const CameraStream&) = delete;

    bool start(std::string* err = nullptr);
    void stop();

    // 模拟断流（FI-01）：停发新帧并触发一次源错误，交由六态机进入 FROZEN/RECONNECTING/升级。
    void simulate_cut();

    // 推进一次：有限等待拉帧、处理 bus 错误、驱动时间相关状态转换。返回本次是否收到帧。
    bool pump(int timeout_ms = 200);

    StreamStats stats() const;

    static long long now_ms();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace media
}  // namespace railhmi
