#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace slg::coroutine {

// Simple bounded mailbox that supports multiple producers and a single
// consumer. The implementation relies on a mutex-protected deque with two
// condition variables so that producers and consumers can block when the buffer
// is full or empty. This can be replaced with a lock-free ring buffer later if
// profiling shows contention in hot paths.
template <typename T>
class Mailbox {
public:
    explicit Mailbox(std::size_t capacity = 1024)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    bool Push(const T& value) {
        return Emplace(value);
    }

    bool Push(T&& value) {
        return Emplace(std::move(value));
    }

    bool TryPop(T& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
        return true;
    }

    bool WaitPop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [this]() { return stopped_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
        return true;
    }

    template <typename Rep, typename Period>
    bool WaitPop(T& out, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!not_empty_cv_.wait_for(lock, timeout, [this]() { return stopped_ || !queue_.empty(); })) {
            return false;
        }
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        not_full_cv_.notify_one();
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    bool Stopped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopped_;
    }

private:
    template <typename V>
    bool Emplace(V&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this]() { return stopped_ || queue_.size() < capacity_; });
        if (stopped_) {
            return false;
        }
        queue_.emplace_back(std::forward<V>(value));
        not_empty_cv_.notify_one();
        return true;
    }

    const std::size_t capacity_;
    std::deque<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    bool stopped_{false};
};

}  // namespace slg::coroutine
