#pragma once

// railhmi-media: 多路显示分发注册表（视频 ITEM-009）。
// 每相机单一来源（DisplayDistributor）；页面/视图只按 camera_id 查询快照与 presentable，
// 不创建新的取流会话；页面切换只改变查询集合，不重建会话。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "railhmi/media/display_distributor.h"
#include "railhmi/media/multi_stream.h"

namespace railhmi {
namespace media {

class DisplayRegistry {
public:
    DisplayRegistry(std::vector<ChannelSpec> specs, RenderPath path);
    ~DisplayRegistry();

    DisplayRegistry(const DisplayRegistry&) = delete;
    DisplayRegistry& operator=(const DisplayRegistry&) = delete;

    int start_all(std::string* err = nullptr);
    void stop_all();

    bool snapshot(const std::string& camera_id, FrameSnapshot* out) const;
    bool presentable(const std::string& camera_id) const;
    std::uint64_t updates(const std::string& camera_id) const;
    unsigned long long generation(const std::string& camera_id) const;

    const RenderPathStatus& render_path_status() const { return path_status_; }
    MultiStreamManager& manager() { return manager_; }
    std::size_t size() const { return manager_.size(); }
    const std::vector<std::string>& ids() const { return manager_.ids(); }

private:
    MultiStreamManager manager_;
    std::vector<std::unique_ptr<DisplayDistributor>> dist_;
    RenderPathStatus path_status_;
};

}  // namespace media
}  // namespace railhmi
