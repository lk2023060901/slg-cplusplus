#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>

#include <boost/fiber/future.hpp>

#include "coroutine/coroutine_export.h"
#include "coroutine/scheduler.h"
#include "timer/scheduler.h"

namespace slg::coroutine {

class SLG_COROUTINE_API CoroutineTimerBridge {
public:
    using TaskId = timer::Scheduler::TaskId;

    struct ScheduledFuture {
        TaskId id{};
        boost::fibers::future<void> future;
    };

    CoroutineTimerBridge(CoroutineScheduler& scheduler, timer::Scheduler& timer);

    ScheduledFuture SleepFor(std::chrono::milliseconds delay);
    ScheduledFuture SleepUntil(std::chrono::steady_clock::time_point time_point);
    bool Cancel(TaskId id);

private:
    struct SleepState {
        boost::fibers::promise<void> promise;
        std::atomic<bool> completed{false};
    };

    ScheduledFuture ScheduleAfter(std::chrono::milliseconds delay);
    void Fulfill(TaskId id, const std::shared_ptr<SleepState>& state);
    void Fail(TaskId id, const std::shared_ptr<SleepState>& state, std::exception_ptr error);

    CoroutineScheduler& scheduler_;
    timer::Scheduler& timer_;

    std::mutex states_mutex_;
    std::unordered_map<TaskId, std::weak_ptr<SleepState>> states_;
};

}  // namespace slg::coroutine
