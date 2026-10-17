#include "railhmi/media/camera_worker.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace railhmi {
namespace media {

namespace {
constexpr int kMaxPullMs = 200;  // 单次 pull 上限（视频设计 §4.2）
}

struct CameraWorker::Impl {
    CameraStreamConfig cfg;
    int pull_ms = 200;
    std::unique_ptr<CameraStream> stream;
    std::thread th;
    std::atomic<bool> running{false};
    std::atomic<bool> restart_req{false};
    std::atomic<bool> cut_req{false};
    std::atomic<bool> freeze_req{false};
    std::atomic<bool> resume_req{false};

    mutable std::mutex mtx;
    StreamStats snap;
    FrameLease lease;
    bool presentable = false;
    long long iterations = 0;
    long long events_total = 0;
    long long freezes = 0;
    long long reconnect_attempts = 0;
    long long escalations = 0;
    long long recoveries = 0;
    FrameCallback frame_cb;

    void loop() {
        while (running.load()) {
            if (restart_req.exchange(false)) {
                stream->stop();
                std::string e;
                stream->start(&e);  // 真正重建：generation+1、seq 归零
            }
            if (cut_req.exchange(false)) stream->simulate_cut();
            if (freeze_req.exchange(false)) stream->simulate_freeze();
            if (resume_req.exchange(false)) stream->simulate_resume();

            stream->pump(pull_ms);

            const std::vector<core::StreamEvent> evs = stream->drain_events();
            long long fz = 0, ra = 0, esc = 0, rec = 0;
            for (const auto& e : evs) {
                switch (e.kind) {
                    case core::StreamEventKind::Freeze: ++fz; break;
                    case core::StreamEventKind::ReconnectAttempt: ++ra; break;
                    case core::StreamEventKind::Escalated: ++esc; break;
                    case core::StreamEventKind::Recovered: ++rec; break;
                    default: break;
                }
            }
            StreamStats s = stream->stats();
            const bool pres = stream->presentable_now();
            FrameLease l = stream->latest_frame();

            FrameCallback cb;
            {
                std::lock_guard<std::mutex> lk(mtx);
                snap = s;
                lease = l;
                presentable = pres;
                ++iterations;
                events_total += static_cast<long long>(evs.size());
                freezes += fz;
                reconnect_attempts += ra;
                escalations += esc;
                recoveries += rec;
                cb = frame_cb;
            }
            if (cb && l.valid()) cb(l, pres);
        }
    }
};

CameraWorker::CameraWorker(CameraStreamConfig cfg, int pull_timeout_ms) : impl_(new Impl()) {
    impl_->cfg = std::move(cfg);
    impl_->pull_ms = pull_timeout_ms;
    if (impl_->pull_ms < 1) impl_->pull_ms = 1;
    if (impl_->pull_ms > kMaxPullMs) impl_->pull_ms = kMaxPullMs;
    impl_->stream.reset(new CameraStream(impl_->cfg));
}

CameraWorker::~CameraWorker() { stop(); }

bool CameraWorker::start(std::string* err) {
    if (impl_->running.load()) return true;
    std::string e;
    if (!impl_->stream->start(&e)) {
        if (err) *err = e;
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->snap = impl_->stream->stats();
    }
    impl_->running.store(true);
    impl_->th = std::thread([this]() { impl_->loop(); });
    return true;
}

void CameraWorker::stop() {
    const bool was_running = impl_->running.exchange(false);
    if (was_running && impl_->th.joinable()) impl_->th.join();
    impl_->stream->stop();
}

void CameraWorker::request_restart() { impl_->restart_req.store(true); }
void CameraWorker::request_cut() { impl_->cut_req.store(true); }
void CameraWorker::request_freeze() { impl_->freeze_req.store(true); }
void CameraWorker::request_resume() { impl_->resume_req.store(true); }

StreamStats CameraWorker::stats() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->snap;
}

FrameLease CameraWorker::latest_frame() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->lease;
}

bool CameraWorker::is_presentable() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->presentable;
}

long long CameraWorker::iterations() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->iterations;
}
long long CameraWorker::events_total() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->events_total;
}
long long CameraWorker::freezes() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->freezes;
}
long long CameraWorker::reconnect_attempts() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->reconnect_attempts;
}
long long CameraWorker::escalations() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->escalations;
}
long long CameraWorker::recoveries() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->recoveries;
}
long long CameraWorker::bus_state_changes() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    return impl_->snap.bus_state_changes;
}
int CameraWorker::pull_timeout_ms() const { return impl_->pull_ms; }

FrameDispatcher& CameraWorker::dispatcher() { return impl_->stream->dispatcher(); }

void CameraWorker::set_frame_callback(FrameCallback cb) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->frame_cb = std::move(cb);
}

}  // namespace media
}  // namespace railhmi
