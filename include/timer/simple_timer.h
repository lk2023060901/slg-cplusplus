#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "timer/timer_export.h"

namespace slg::timer {

class SLG_TIMER_API SimpleTimer {
public:
    using Callback = std::function<void()>;

    SimpleTimer();
    ~SimpleTimer();

    void Start(std::chrono::milliseconds delay,
               Callback callback,
               bool repeat = false,
               std::chrono::milliseconds interval = std::chrono::milliseconds{0});
    void Stop();
    bool Running() const noexcept;

private:
    void Worker(std::chrono::milliseconds delay,
                Callback callback,
                bool repeat,
                std::chrono::milliseconds interval);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace slg::timer

