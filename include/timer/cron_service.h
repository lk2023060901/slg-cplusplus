#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "croncpp.h"
#include "timer/scheduler.h"

namespace slg::timer {

class SLG_TIMER_API CronService {
public:
    using TaskId = std::uint64_t;

    explicit CronService(Scheduler& scheduler);

    TaskId Schedule(const std::string& expression, TimeWheel::Task task);
    bool Cancel(TaskId id);

private:
    struct CronTask {
        TaskId id{0};
        cron::cronexpr expression;
        TimeWheel::Task task;
        std::atomic<bool> cancelled{false};
        Scheduler::TaskId scheduled_id{0};
    };

    void ScheduleNext(const std::shared_ptr<CronTask>& cron_task);

    Scheduler& scheduler_;
    std::mutex mutex_;
    std::unordered_map<TaskId, std::shared_ptr<CronTask>> tasks_;
    std::atomic<TaskId> next_id_{1};
};

}  // namespace slg::timer
