#include "timer/time_wheel.h"

namespace slg::timer {

TimeWheel::TimeWheel(std::chrono::milliseconds tick_interval, std::size_t wheel_size)
    : tick_interval_(tick_interval.count() <= 0 ? std::chrono::milliseconds(1) : tick_interval),
      wheel_size_(wheel_size == 0 ? 512 : wheel_size),
      buckets_(wheel_size_) {}

TimeWheel::~TimeWheel() {
    Stop();
}

void TimeWheel::Start() {
    bool expected = false;
    if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        worker_ = std::thread(&TimeWheel::Run, this);
    }
}

void TimeWheel::Stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        task_index_.clear();
        for (auto& bucket : buckets_) {
            bucket.clear();
        }
    }
}

TimeWheel::TaskId TimeWheel::Schedule(std::chrono::milliseconds delay, Task task) {
    return AddTask(delay, std::move(task), false);
}

TimeWheel::TaskId TimeWheel::ScheduleEvery(std::chrono::milliseconds interval, Task task) {
    return AddTask(interval, std::move(task), true);
}

bool TimeWheel::Cancel(TaskId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = task_index_.find(id);
    if (iter == task_index_.end()) {
        return false;
    }
    auto [bucket_index, bucket_iter] = iter->second;
    buckets_[bucket_index].erase(bucket_iter);
    task_index_.erase(iter);
    return true;
}

TimeWheel::TaskId TimeWheel::AddTask(std::chrono::milliseconds delay,
                                     Task task,
                                     bool repeat) {
    if (!task) {
        return 0;
    }
    if (!running_.load(std::memory_order_acquire)) {
        Start();
    }
    auto ticks = std::max<std::int64_t>(1, delay.count() / tick_interval_.count());
    std::size_t rounds = static_cast<std::size_t>(ticks / wheel_size_);
    std::size_t offset = static_cast<std::size_t>(ticks % wheel_size_);
    std::lock_guard<std::mutex> lock(mutex_);
    TaskId id = next_id_.fetch_add(1, std::memory_order_relaxed);
    TaskEntry entry{.id = id,
                    .remaining_rounds = rounds,
                    .task = std::move(task),
                    .repeat = repeat,
                    .interval = delay};
    std::size_t slot = (current_index_ + offset) % wheel_size_;
    auto& bucket = buckets_[slot];
    auto it = bucket.insert(bucket.end(), entry);
    task_index_[id] = {slot, it};
    cv_.notify_all();
    return id;
}

void TimeWheel::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load(std::memory_order_acquire)) {
        auto wake_time = std::chrono::steady_clock::now() + tick_interval_;
        cv_.wait_until(lock, wake_time, [this]() { return !running_.load(); });
        if (!running_.load()) {
            break;
        }
        auto& bucket = buckets_[current_index_];
        std::vector<TaskEntry> due;
        auto it = bucket.begin();
        while (it != bucket.end()) {
            if (it->remaining_rounds > 0) {
                --it->remaining_rounds;
                ++it;
                continue;
            }
            due.push_back(*it);
            task_index_.erase(it->id);
            it = bucket.erase(it);
        }
        current_index_ = (current_index_ + 1) % wheel_size_;
        lock.unlock();
        for (auto& entry : due) {
            try {
                entry.task();
            } catch (...) {
            }
            if (entry.repeat && running_.load(std::memory_order_acquire)) {
                Schedule(entry.interval, entry.task);
            }
        }
        lock.lock();
    }
}

}  // namespace slg::timer

