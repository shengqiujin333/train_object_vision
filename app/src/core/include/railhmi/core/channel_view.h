#pragma once

// railhmi-core: 全摄像头页每格展示视图（ITEM-015）。
// 由注册表槽位 + 每路运行时快照（fps/最近帧/重连/六态/presentable）派生显示字段：
// 摄像头名、连接状态角标（文字三重编码）、实时 fps、最近一帧时间、检测职责徽标。
// 顺序严格等于注册表顺序；某路断流/冻结只改变该格内容，不改变位置，网格不重排。
// 纯逻辑，无 Qt/GStreamer/相机依赖。

#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/stream_state.h"

namespace railhmi {
namespace core {

struct ChannelRuntime {
    bool has_stream = false;                 // false = 尚未启动取流
    StreamState state = StreamState::Offline;
    double fps = 0.0;
    long long last_frame_ms = -1;            // 最近一帧单调毫秒；-1 表示尚无帧
    int reconnect_count = 0;
    bool presentable = false;                // = 流层 may_present_as_live
    std::string last_error;
};

struct ChannelView {
    std::string id;
    std::string name;
    std::string position;
    std::string badge;
    bool ai_enabled = false;
    int slot = 0;                            // 稳定序号
    std::string state;                       // 运行时 token 或 "unstarted"
    std::string state_label;                 // 中文标签（三重编码的文字部分）
    std::string fps_text;                    // "25.0 fps" 或 "—"
    std::string last_frame_text;             // "1 秒前" / "<1 秒前" / "—"
    std::string reconnect_text;              // "重连 2 次" 或 ""
    bool presentable = false;
    bool placeholder = true;                 // !presentable -> 渲染占位层
    bool stale = false;                      // 有帧但超过新鲜阈值
};

struct ChannelViewOptions {
    double fps_epsilon = 0.05;               // fps 视为 0 的阈值
    long long stale_after_ms = 2000;         // 最近帧超过此值视为陈旧
};

// 状态 token -> 中文标签（未知 token 原样返回）。
std::string channel_state_label(const std::string& state);
std::string format_fps(double fps, double epsilon = 0.05);
std::string format_last_frame(long long age_ms);   // age_ms < 0 -> "—"

// 逐槽位构建展示视图；输出长度与顺序等于 registry，运行时缺省视为未启动（仍占位，不丢格）。
std::vector<ChannelView> build_channel_views(const std::vector<CameraSlot>& registry,
                                             const std::vector<ChannelRuntime>& runtime,
                                             long long now_ms,
                                             const ChannelViewOptions& opts = ChannelViewOptions());

}  // namespace core
}  // namespace railhmi
