#include "coroutine/timer_bridge.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace slg::coroutine {

namespace {
std::chrono::milliseconds ClampToMilliseconds(std::chrono::steady_clock::duration duration) {
    if (duration <= std::chrono::steady_clock::duration::zero()) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}
}  // namespace

CoroutineTimerBridge::CoroutineTimerBridge(CoroutineScheduler& scheduler, timer::Scheduler& timer)
    : scheduler_(scheduler), timer_(timer) {}

CoroutineTimerBridge::ScheduledFuture CoroutineTimerBridge::SleepFor(std::chrono::milliseconds delay) {
    return ScheduleAfter(delay);
}

CoroutineTimerBridge::ScheduledFuture CoroutineTimerBridge::SleepUntil(
    std::chrono::steady_clock::time_point time_point) {
    const auto now = std::chrono::steady_clock::now();
    const auto delay = ClampToMilliseconds(time_point - now);
    return ScheduleAfter(delay);
}

bool CoroutineTimerBridge::Cancel(TaskId id) {
    std::shared_ptr<SleepState> state;
    {
        std::lock_guard<std::mutex> lock(states_mutex_);
        auto it = states_.find(id);
        if (it != states_.end()) {
            state = it->second.lock();
            states_.erase(it);
        }
    }
    const bool cancelled = timer_.Cancel(id);
    if (cancelled && state) {
        Fail(id, state, std::make_exception_ptr(std::runtime_error("fiber sleep cancelled")));
    }
    return cancelled;
}

CoroutineTimerBridge::ScheduledFuture CoroutineTimerBridge::ScheduleAfter(std::chrono::milliseconds delay) {
    auto state = std::make_shared<SleepState>();
    ScheduledFuture result;
    result.future = state->promise.get_future();

    auto id_holder = std::make_shared<TaskId>(0);
    auto callback = [this, state, id_holder]() {
        const auto id = *id_holder;
        Fulfill(id, state);
    };

    const auto id = timer_.ScheduleAfter(delay, callback);
    *id_holder = id;

    {
        std::lock_guard<std::mutex> lock(states_mutex_);
        states_.emplace(id, state);
    }

    result.id = id;
    return result;
}

void CoroutineTimerBridge::Fulfill(TaskId id, const std::shared_ptr<SleepState>& state) {
    scheduler_.Schedule([this, id, state]() {
        {
            std::lock_guard<std::mutex> lock(states_mutex_);
            states_.erase(id);
        }
        bool expected = false;
        if (state && state->completed.compare_exchange_strong(expected, true)) {
            state->promise.set_value();
        }
    });
}

void CoroutineTimerBridge::Fail(TaskId id,
                                const std::shared_ptr<SleepState>& state,
                                std::exception_ptr error) {
    scheduler_.Schedule([this, id, state, error]() {
        {
            std::lock_guard<std::mutex> lock(states_mutex_);
            states_.erase(id);
        }
        bool expected = false;
        if (state && state->completed.compare_exchange_strong(expected, true)) {
            state->promise.set_exception(error);
        }
    });
}

}  // namespace slg::coroutine
