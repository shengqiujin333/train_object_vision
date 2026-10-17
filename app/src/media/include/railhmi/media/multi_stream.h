#pragma once

// railhmi-media: 多路并发取流管理（视频 ITEM-007；取代旧 ITEM-015 顺序 pump 版）。
//
// 每路一个独立 CameraWorker（专用线程）；并发推进，单路启动失败/断流/重建
// 不影响其余路；槽位顺序 = 构造顺序（不重排、不因单路失败丢格）。
// pump 在各自 worker 线程执行，调用方（含 GUI）只读取线程安全快照。
// target 专用（依赖 GStreamer/librga）。

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/media/camera_stream.h"
#include "railhmi/media/camera_worker.h"
#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

struct ChannelSpec {
    std::string camera_id;
    CameraStreamConfig cfg;
    bool start_failure = false;  // 故障注入（测试）：模拟该路启动失败，不启动其 worker
};

class MultiStreamManager {
public:
    explicit MultiStreamManager(std::vector<ChannelSpec> specs);
    ~MultiStreamManager();

    MultiStreamManager(const MultiStreamManager&) = delete;
    MultiStreamManager& operator=(const MultiStreamManager&) = delete;

    // 启动所有路（每路独立 worker 线程）；逐路错误写入 err，不因单路失败中止。
    // 返回成功启动的路数。
    int start_all(std::string* err = nullptr);

    void stop_all();

    // 单路请求：只作用于该路 worker（不重启其他路）。
    void request_cut(std::size_t index);
    void request_restart(std::size_t index);
    void request_freeze(std::size_t index);
    void request_resume(std::size_t index);
    void simulate_cut(std::size_t index) { request_cut(index); }  // 兼容旧名

    // 线程安全快照（顺序 = 构造顺序，槽位不变）。
    std::vector<StreamStats> stats() const;
    FrameLease latest_frame(std::size_t index) const;
    FrameLease latest_frame(const std::string& camera_id) const;
    bool is_presentable(std::size_t index) const;
    std::string start_error(std::size_t index) const;
    bool started(std::size_t index) const;

    std::size_t size() const { return workers_.size(); }
    const std::vector<std::string>& ids() const { return ids_; }
    int index_of(const std::string& camera_id) const;  // -1 = 未找到
    CameraWorker* worker(std::size_t index) const;      // 逐路 worker（可空）

    static long long now_ms() { return CameraStream::now_ms(); }

private:
    bool valid(std::size_t i) const { return i < workers_.size(); }

    std::vector<std::string> ids_;
    std::vector<bool> start_failure_;
    std::vector<std::unique_ptr<CameraWorker>> workers_;
    std::vector<std::string> start_errors_;
    std::vector<bool> started_;
};

}  // namespace media
}  // namespace railhmi
