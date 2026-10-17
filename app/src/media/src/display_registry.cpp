#include "railhmi/media/display_registry.h"

#include <utility>

namespace railhmi {
namespace media {

DisplayRegistry::DisplayRegistry(std::vector<ChannelSpec> specs, RenderPath path)
    : manager_(std::move(specs)) {
    path_status_ = probe_render_path(path);
    dist_.reserve(manager_.size());
    for (std::size_t i = 0; i < manager_.size(); ++i) {
        dist_.emplace_back(new DisplayDistributor(manager_.ids()[i], path));
        DisplayDistributor* d = dist_.back().get();
        CameraWorker* w = manager_.worker(i);
        if (w) {
            w->set_frame_callback([d](const FrameLease& l, bool pres) { d->update(l, pres); });
        }
    }
}

DisplayRegistry::~DisplayRegistry() { manager_.stop_all(); }

int DisplayRegistry::start_all(std::string* err) { return manager_.start_all(err); }
void DisplayRegistry::stop_all() { manager_.stop_all(); }

bool DisplayRegistry::snapshot(const std::string& camera_id, FrameSnapshot* out) const {
    const int i = manager_.index_of(camera_id);
    if (i < 0) return false;
    return dist_[static_cast<std::size_t>(i)]->snapshot(out);
}

bool DisplayRegistry::presentable(const std::string& camera_id) const {
    const int i = manager_.index_of(camera_id);
    if (i < 0) return false;
    return dist_[static_cast<std::size_t>(i)]->presentable();
}

std::uint64_t DisplayRegistry::updates(const std::string& camera_id) const {
    const int i = manager_.index_of(camera_id);
    if (i < 0) return 0;
    return dist_[static_cast<std::size_t>(i)]->updates();
}

unsigned long long DisplayRegistry::generation(const std::string& camera_id) const {
    const int i = manager_.index_of(camera_id);
    if (i < 0) return 0;
    const std::vector<StreamStats> s = manager_.stats();
    return s[static_cast<std::size_t>(i)].generation;
}

}  // namespace media
}  // namespace railhmi
