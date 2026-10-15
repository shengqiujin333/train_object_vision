#include "railhmi/media/multi_stream.h"

namespace railhmi {
namespace media {

MultiStreamManager::MultiStreamManager(std::vector<ChannelSpec> specs) {
    ids_.reserve(specs.size());
    streams_.reserve(specs.size());
    for (auto& s : specs) {
        ids_.push_back(s.camera_id);
        streams_.emplace_back(new CameraStream(s.cfg));
    }
}

MultiStreamManager::~MultiStreamManager() { stop_all(); }

int MultiStreamManager::start_all(std::string* err) {
    int ok = 0;
    for (std::size_t i = 0; i < streams_.size(); ++i) {
        std::string e;
        if (streams_[i]->start(&e)) {
            ++ok;
        } else if (err) {
            *err += ids_[i] + ": " + e + "\n";
        }
    }
    return ok;
}

int MultiStreamManager::pump_all(int timeout_ms) {
    int frames = 0;
    for (auto& s : streams_)
        if (s->pump(timeout_ms)) ++frames;
    return frames;
}

void MultiStreamManager::simulate_cut(std::size_t index) {
    if (index < streams_.size()) streams_[index]->simulate_cut();
}

std::vector<StreamStats> MultiStreamManager::stats() const {
    std::vector<StreamStats> out;
    out.reserve(streams_.size());
    for (const auto& s : streams_) out.push_back(s->stats());
    return out;
}

void MultiStreamManager::stop_all() {
    for (auto& s : streams_) s->stop();
}

}  // namespace media
}  // namespace railhmi
