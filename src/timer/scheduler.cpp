#include "timer/scheduler.h"

namespace slg::timer {

Scheduler::Scheduler(std::chrono::milliseconds tick_interval, std::size_t wheel_size)
    : wheel_(tick_interval, wheel_size) {}

Scheduler::~Scheduler() {
    Stop();
}

void Scheduler::Start() {
    wheel_.Start();
}

void Scheduler::Stop() {
    wheel_.Stop();
}

Scheduler::TaskId Scheduler::ScheduleAfter(std::chrono::milliseconds delay, Task task) {
    return wheel_.Schedule(delay, std::move(task));
}

Scheduler::TaskId Scheduler::ScheduleEvery(std::chrono::milliseconds interval, Task task) {
    return wheel_.ScheduleEvery(interval, std::move(task));
}

Scheduler::TaskId Scheduler::ScheduleAt(std::chrono::system_clock::time_point time_point,
                                        Task task) {
    auto now = std::chrono::system_clock::now();
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(time_point - now);
    if (delay.count() <= 0) {
        delay = std::chrono::milliseconds(1);
    }
    return ScheduleAfter(delay, std::move(task));
}

bool Scheduler::Cancel(TaskId id) {
    return wheel_.Cancel(id);
}

}  // namespace slg::timer

