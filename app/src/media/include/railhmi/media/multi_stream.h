#pragma once

// railhmi-media: 六路并发取流管理（ITEM-015）。
// 同时持有并推进多路 CameraStream（每路独立六态机），提供逐路统计快照与单路断流注入。
// target 专用（依赖 GStreamer/librga）。可在专用工作线程中 pump_all()，UI 只读取 stats()。
// 单路失败不影响其余路：start_all 记录逐路启动错误，其它路照常推进。

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/media/camera_stream.h"

namespace railhmi {
namespace media {

struct ChannelSpec {
    std::string camera_id;
    CameraStreamConfig cfg;
};

class MultiStreamManager {
public:
    explicit MultiStreamManager(std::vector<ChannelSpec> specs);
    ~MultiStreamManager();

    MultiStreamManager(const MultiStreamManager&) = delete;
    MultiStreamManager& operator=(const MultiStreamManager&) = delete;

    // 启动所有路；逐路错误写入 err（不因单路失败中止）。返回成功启动数。
    int start_all(std::string* err = nullptr);

    // 推进一轮：依次有限等待拉帧并驱动各状态机；返回本轮收到帧的总数。
    int pump_all(int timeout_ms = 150);

    // 模拟断开第 index 路（FI-01）；不影响其余路。
    void simulate_cut(std::size_t index);

    // 各路口径一致的统计快照（顺序 = 构造顺序）。
    std::vector<StreamStats> stats() const;

    void stop_all();

    std::size_t size() const { return streams_.size(); }
    const std::vector<std::string>& ids() const { return ids_; }

    static long long now_ms() { return CameraStream::now_ms(); }

private:
    std::vector<std::string> ids_;
    std::vector<std::unique_ptr<CameraStream>> streams_;
};

}  // namespace media
}  // namespace railhmi
