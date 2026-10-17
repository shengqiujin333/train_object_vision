#include "railhmi/media/multi_stream.h"

namespace railhmi {
namespace media {

MultiStreamManager::MultiStreamManager(std::vector<ChannelSpec> specs) {
    ids_.reserve(specs.size());
    start_failure_.reserve(specs.size());
    workers_.reserve(specs.size());
    for (auto& s : specs) {
        ids_.push_back(s.camera_id);
        start_failure_.push_back(s.start_failure);
        // 每路独立 worker；构造即建好（未启动时 stats 为 offline 快照）。
        workers_.emplace_back(new CameraWorker(s.cfg, 200));
    }
    start_errors_.assign(workers_.size(), std::string());
    started_.assign(workers_.size(), false);
}

MultiStreamManager::~MultiStreamManager() { stop_all(); }

int MultiStreamManager::start_all(std::string* err) {
    int ok = 0;
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        if (start_failure_[i]) {
            start_errors_[i] = "FI: forced start failure";
            if (err) *err += ids_[i] + ": " + start_errors_[i] + "\n";
            continue;  // 该路保持离线，不启动 worker，不影响其余路
        }
        std::string e;
        if (workers_[i]->start(&e)) {
            started_[i] = true;
            ++ok;
        } else {
            start_errors_[i] = e;
            if (err) *err += ids_[i] + ": " + e + "\n";
        }
    }
    return ok;
}

void MultiStreamManager::stop_all() {
    for (auto& w : workers_) w->stop();
}

void MultiStreamManager::request_cut(std::size_t index) {
    if (valid(index)) workers_[index]->request_cut();
}
void MultiStreamManager::request_restart(std::size_t index) {
    if (valid(index)) workers_[index]->request_restart();
}
void MultiStreamManager::request_freeze(std::size_t index) {
    if (valid(index)) workers_[index]->request_freeze();
}
void MultiStreamManager::request_resume(std::size_t index) {
    if (valid(index)) workers_[index]->request_resume();
}

std::vector<StreamStats> MultiStreamManager::stats() const {
    std::vector<StreamStats> out;
    out.reserve(workers_.size());
    for (const auto& w : workers_) out.push_back(w->stats());
    return out;
}

FrameLease MultiStreamManager::latest_frame(std::size_t index) const {
    if (!valid(index)) return FrameLease();
    return workers_[index]->latest_frame();
}

FrameLease MultiStreamManager::latest_frame(const std::string& camera_id) const {
    const int i = index_of(camera_id);
    if (i < 0) return FrameLease();
    return workers_[static_cast<std::size_t>(i)]->latest_frame();
}

bool MultiStreamManager::is_presentable(std::size_t index) const {
    return valid(index) ? workers_[index]->is_presentable() : false;
}

std::string MultiStreamManager::start_error(std::size_t index) const {
    return valid(index) ? start_errors_[index] : std::string();
}

bool MultiStreamManager::started(std::size_t index) const {
    return valid(index) ? started_[index] : false;
}

int MultiStreamManager::index_of(const std::string& camera_id) const {
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == camera_id) return static_cast<int>(i);
    }
    return -1;
}

CameraWorker* MultiStreamManager::worker(std::size_t index) const {
    return valid(index) ? workers_[index].get() : nullptr;
}

}  // namespace media
}  // namespace railhmi
