#pragma once

// railhmi-media: 单路取流与流状态机接入（ITEM-013）。
// 取流管线由 pipeline_factory（ITEM-002）统一构建：rtsp(强制 UDP)/file/test 三类源
// 复用同一条 “源→解码→video/x-raw,format=NV12” 管线；凭据经 rtspsrc 属性传入且日志脱敏。
// 逐帧发布 → 接入 railhmi-core 六态与质量标志；统计 fps / 最近帧时间 / 重连次数；
// 非 LIVE 不得把旧帧当作实时（presentable）。target 专用（依赖 GStreamer/librga）。

#include <deque>
#include <string>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/stream_state.h"
#include "railhmi/core/timebase.h"
#include "railhmi/media/frame_lease.h"
#include "railhmi/media/frame_store.h"
#include "railhmi/media/pipeline_factory.h"

namespace railhmi {
namespace media {

enum class SourceKind { Test, File, Rtsp };

struct CameraStreamConfig {
    std::string camera_id = "CAM-01";
    SourceKind source = SourceKind::Test;
    std::string url;               // source=rtsp；可含 user:pass@（工厂拆为认证属性）
    std::string username;          // source=rtsp 可选显式认证
    std::string password;          // 仅内存使用，日志/错误整串脱敏
    std::string file;              // source=file
    int latency_ms = 500;
    int timeout_us = 5000000;      // rtspsrc timeout（微秒）
    int udp_buffer_bytes = 4194304;
    DecodeMode decode = DecodeMode::Auto;   // auto(decodebin) | h264 | h265 显式固化
    bool test_is_live = true;      // source=test
    int test_width = 1280;
    int test_height = 720;
    int test_fps = 25;
    const railhmi::core::TimeBase* timebase = nullptr;  // 用于 capture_wall_ms；空/未校准则 -1
    std::string session_id;         // 应用启动不透明串；空则自动生成（跨相机共享）
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
    // 帧身份与时间（视频 ITEM-003）
    std::string session_id;
    unsigned long long generation = 0;
    unsigned long long last_seq = 0;
    long long capture_wall_ms = -1;   // 已校准 UTC；-1=不可用
    long long pts_ns = -1;            // 相机 PTS；-1=无/无效
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

    // 最近一帧的身份+时间（视频 ITEM-003）；未出帧时 session/generation 已就绪、seq=0。
    railhmi::core::FrameMeta last_frame_meta() const;
    unsigned long long generation() const;
    long long frames_emitted() const;

    // 最新帧租约（视频 ITEM-004）；无帧返回空租约（status()==NOT_READY）。
    FrameLease latest_frame() const;
    // 最新帧仓库：供上层设置 release 回调与读取发布/拒绝计数。
    FrameStore& frame_store();

    static long long now_ms();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace media
}  // namespace railhmi
