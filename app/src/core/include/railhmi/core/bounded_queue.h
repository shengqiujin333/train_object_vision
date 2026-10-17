#pragma once

// railhmi-core: 有界队列（满时丢最旧）——三类消费者背压的纯逻辑（视频 ITEM-008）。
//
// 非阻塞、线程安全：满时按“丢最旧”淘汰队首保留最新；可选字节上限（用于录像分支预算）。
// 显示=容量 1、推理=容量 ai_queue_depth、录像=独立容量/字节上限，三者各自计数。
// 纯标准库（可最小工具链单测）。

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace railhmi {
namespace core {

template <typename T>
class BoundedQueue {
public:
    // max_items>0 为条数上限；max_bytes>0 时同时受字节上限约束。
    explicit BoundedQueue(std::size_t max_items, std::size_t max_bytes = 0)
        : max_items_(max_items ? max_items : 1), max_bytes_(max_bytes) {}

    // 非阻塞入队；满时先淘汰最旧并计数，始终保留最新（返回 true）。
    bool push(const T& v, std::size_t bytes = 0) {
        std::lock_guard<std::mutex> lk(mtx_);
        q_.push_back(Entry{v, bytes});
        bytes_ += bytes;
        ++pushed_;
        while (q_.size() > max_items_ || (max_bytes_ > 0 && bytes_ > max_bytes_)) {
            bytes_ -= q_.front().bytes;
            q_.pop_front();
            ++dropped_;
        }
        return true;
    }

    // 非阻塞出队；空返回 false。
    bool pop(T* out) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty()) return false;
        if (out) *out = q_.front().v;
        bytes_ -= q_.front().bytes;
        q_.pop_front();
        ++popped_;
        return true;
    }

    // 读取最新（队尾）但不移除。
    bool latest(T* out) const {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty()) return false;
        if (out) *out = q_.back().v;
        return true;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }
    std::size_t bytes() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return bytes_;
    }
    std::size_t capacity() const { return max_items_; }
    std::size_t byte_capacity() const { return max_bytes_; }
    bool empty() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.empty();
    }
    std::uint64_t pushed() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return pushed_;
    }
    std::uint64_t dropped() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return dropped_;
    }
    std::uint64_t popped() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return popped_;
    }

private:
    struct Entry {
        T v;
        std::size_t bytes;
    };
    mutable std::mutex mtx_;
    std::deque<Entry> q_;
    std::size_t max_items_;
    std::size_t max_bytes_;
    std::size_t bytes_ = 0;
    std::uint64_t pushed_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t popped_ = 0;
};

}  // namespace core
}  // namespace railhmi
