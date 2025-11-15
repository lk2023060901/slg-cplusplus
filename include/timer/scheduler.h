#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

#include "timer/time_wheel.h"

namespace slg::timer {

class SLG_TIMER_API Scheduler {
public:
    using TaskId = TimeWheel::TaskId;
    using Task = TimeWheel::Task;

    Scheduler(std::chrono::milliseconds tick_interval = std::chrono::milliseconds(100),
              std::size_t wheel_size = 512);
    ~Scheduler();

    void Start();
    void Stop();

    TaskId ScheduleAfter(std::chrono::milliseconds delay, Task task);
    TaskId ScheduleEvery(std::chrono::milliseconds interval, Task task);
    TaskId ScheduleAt(std::chrono::system_clock::time_point time_point, Task task);
    bool Cancel(TaskId id);

private:
    TimeWheel wheel_;
};

}  // namespace slg::timer

