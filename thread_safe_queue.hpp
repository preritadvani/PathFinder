#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace fx {

// An inbox many threads can push into, and one thread can pop from.
template <typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push(std::move(value));
        }
        cv_.notify_one();
    }

    // Waits until an item is available. Returns nullopt once the queue
    // is closed AND empty (the signal to stop).
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop();
        return v;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(m_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    bool closed_ = false;
};

}  // namespace fx
