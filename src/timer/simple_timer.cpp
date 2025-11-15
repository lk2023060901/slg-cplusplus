#include "timer/simple_timer.h"

namespace slg::timer {

SimpleTimer::SimpleTimer() = default;

SimpleTimer::~SimpleTimer() {
    Stop();
}

bool SimpleTimer::Running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

void SimpleTimer::Start(std::chrono::milliseconds delay,
                        Callback callback,
                        bool repeat,
                        std::chrono::milliseconds interval) {
    if (!callback) {
        return;
    }
    Stop();
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&SimpleTimer::Worker, this, delay, std::move(callback), repeat,
                          interval.count() == 0 ? delay : interval);
}

void SimpleTimer::Stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    } else if (thread_.joinable()) {
        thread_.join();
    }
}

void SimpleTimer::Worker(std::chrono::milliseconds delay,
                         Callback callback,
                         bool repeat,
                         std::chrono::milliseconds interval) {
    auto current_delay = delay.count() <= 0 ? std::chrono::milliseconds(1) : delay;
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load(std::memory_order_acquire)) {
        if (cv_.wait_for(lock, current_delay, [this]() { return !running_.load(); })) {
            break;
        }
        lock.unlock();
        try {
            callback();
        } catch (...) {
        }
        lock.lock();
        if (!repeat) {
            break;
        }
        current_delay = interval.count() <= 0 ? std::chrono::milliseconds(1) : interval;
    }
    running_.store(false, std::memory_order_release);
}

}  // namespace slg::timer

