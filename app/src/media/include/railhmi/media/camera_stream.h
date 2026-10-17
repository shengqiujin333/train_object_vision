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
#include "railhmi/media/frame_dispatcher.h"
#include "railhmi/media/frame_store.h"
#include "railhmi/media/pipeline_factory.h"
#include "railhmi/media/rga_convert.h"

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
    bool convert_bgr = false;       // 在取流 worker 内做 NV12→BGR（librga BT601_FULL）
    int ai_queue_depth = 2;         // 推理有界丢旧帧队列深度（app.ai.queue_depth）
    std::size_t recording_queue_frames = 30;             // 录像分支条数上限
    std::size_t recording_queue_bytes = 0;               // 录像分支字节上限（0=只按条数）
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
    std::string quality_flags;        // 最近帧质量标志 describe()
    long long bus_state_changes = 0;  // GstBus STATE_CHANGED 计数
    unsigned long long display_dropped = 0;   // 显示 latest-only 丢帧（ITEM-008）
    unsigned long long ai_dropped = 0;        // 推理有界队列丢帧
    unsigned long long recording_overflow = 0;  // 录像分支溢出（缺口）
    unsigned long long ai_depth = 0;
    unsigned long long recording_depth = 0;
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

    // 模拟冻结（FI-02）：暂停管线停发新帧且不报源错误，交由 tick 判定 FROZEN。
    void simulate_freeze();

    // 解除冻结：恢复管线 PLAYING；同一会话内出帧后应触发 Recovered 且计数清零。
    void simulate_resume();

    // 推进一次：有限等待拉帧、处理 bus 错误、驱动时间相关状态转换。返回本次是否收到帧。
    bool pump(int timeout_ms = 200);

    StreamStats stats() const;

    // 降级/状态事件（由六态机累积）；worker 每轮 drain 并统计。
    std::vector<railhmi::core::StreamEvent> drain_events();

    // 渲染前判定：非 LIVE 且没有新鲜帧、或租约不属于当前 generation 时不得当实时。
    bool presentable_now() const;
    bool lease_presentable(const FrameLease& lease) const;
    long long bus_state_changes() const;

    // 最近一帧的身份+时间（视频 ITEM-003）；未出帧时 session/generation 已就绪、seq=0。
    railhmi::core::FrameMeta last_frame_meta() const;
    unsigned long long generation() const;
    long long frames_emitted() const;

    // 最新帧租约（视频 ITEM-004）；无帧返回空租约（status()==NOT_READY）。
    FrameLease latest_frame() const;
    // 最新帧仓库：供上层设置 release 回调与读取发布/拒绝计数。
    FrameStore& frame_store();
    // 三类消费者有界队列（显示/推理/录像）（视频 ITEM-008）。
    FrameDispatcher& dispatcher();

    // 最近一次 NV12→BGR 转换结果（视频 ITEM-005，需 cfg.convert_bgr）。失败不丢帧。
    const BgrImage& last_bgr() const;
    long long bgr_converted() const;
    long long bgr_failed() const;
    const std::string& bgr_last_error() const;

    static long long now_ms();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace media
}  // namespace railhmi
