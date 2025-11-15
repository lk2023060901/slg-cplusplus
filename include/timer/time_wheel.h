#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "timer/timer_export.h"

namespace slg::timer {

class SLG_TIMER_API TimeWheel {
public:
    using Task = std::function<void()>;
    using TaskId = std::uint64_t;

    TimeWheel(std::chrono::milliseconds tick_interval = std::chrono::milliseconds(100),
              std::size_t wheel_size = 512);
    ~TimeWheel();

    void Start();
    void Stop();

    TaskId Schedule(std::chrono::milliseconds delay, Task task);
    TaskId ScheduleEvery(std::chrono::milliseconds interval, Task task);
    bool Cancel(TaskId id);

private:
    struct TaskEntry {
        TaskId id;
        std::size_t remaining_rounds;
        Task task;
        bool repeat{false};
        std::chrono::milliseconds interval{0};
    };

    using Bucket = std::list<TaskEntry>;

    TaskId AddTask(std::chrono::milliseconds delay, Task task, bool repeat);
    void Run();

    std::chrono::milliseconds tick_interval_;
    std::size_t wheel_size_;
    std::vector<Bucket> buckets_;
    std::unordered_map<TaskId, std::pair<std::size_t, Bucket::iterator>> task_index_;
    std::size_t current_index_{0};

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::condition_variable cv_;
    std::mutex mutex_;
    std::atomic<TaskId> next_id_{1};
};

}  // namespace slg::timer

