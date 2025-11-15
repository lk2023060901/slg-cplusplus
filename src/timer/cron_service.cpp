#include "timer/cron_service.h"

#include <stdexcept>

namespace slg::timer {

CronService::CronService(Scheduler& scheduler) : scheduler_(scheduler) {}

CronService::TaskId CronService::Schedule(const std::string& expression, TimeWheel::Task task) {
    if (!task) {
        throw std::invalid_argument("cron task must have callback");
    }
    auto cron_task = std::make_shared<CronTask>();
    try {
        cron_task->expression = cron::make_cron(expression);
    } catch (const cron::bad_cronexpr& ex) {
        throw std::invalid_argument(ex.what());
    }
    cron_task->task = std::move(task);
    cron_task->id = next_id_.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_[cron_task->id] = cron_task;
    }

    ScheduleNext(cron_task);
    return cron_task->id;
}

bool CronService::Cancel(TaskId id) {
    std::shared_ptr<CronTask> cron_task;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto iter = tasks_.find(id);
        if (iter == tasks_.end()) {
            return false;
        }
        cron_task = iter->second;
        tasks_.erase(iter);
    }
    cron_task->cancelled.store(true, std::memory_order_release);
    scheduler_.Cancel(cron_task->scheduled_id);
    return true;
}

void CronService::ScheduleNext(const std::shared_ptr<CronTask>& cron_task) {
    if (cron_task->cancelled.load(std::memory_order_acquire)) {
        return;
    }
    auto next_time = cron::cron_next(cron_task->expression, std::chrono::system_clock::now());
    cron_task->scheduled_id = scheduler_.ScheduleAt(
        next_time, [this, cron_task]() {
            if (cron_task->cancelled.load(std::memory_order_acquire)) {
                return;
            }
            try {
                cron_task->task();
            } catch (...) {
            }
            ScheduleNext(cron_task);
        });
}

}  // namespace slg::timer
