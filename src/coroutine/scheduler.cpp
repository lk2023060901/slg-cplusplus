#include "coroutine/scheduler.h"

#include <utility>

#include <boost/fiber/algo/round_robin.hpp>
#include <boost/fiber/fiber.hpp>
#include <boost/fiber/operations.hpp>

namespace slg::coroutine {
namespace {
std::size_t ResolveWorkerCount(std::size_t requested) {
    if (requested == 0) {
        requested = std::thread::hardware_concurrency();
        if (requested == 0) {
            requested = 1;
        }
    }
    return requested;
}
}  // namespace

class CoroutineScheduler::Worker {
public:
    Worker(CoroutineScheduler& scheduler, std::size_t index)
        : scheduler_(scheduler), index_(index) {}

    void Start() {
        thread_ = std::thread([this]() { Run(); });
    }

    void RequestStop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
    }

    void Join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool Enqueue(Task task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        local_queue_.push_back(std::move(task));
        cv_.notify_one();
        return true;
    }

    bool Steal(Task& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (local_queue_.empty()) {
            return false;
        }
        task = std::move(local_queue_.front());
        local_queue_.pop_front();
        return true;
    }

    void Notify() {
        cv_.notify_one();
    }

    static Worker* Current() {
        return current_;
    }

private:
    void Run() {
        current_ = this;
        boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
        while (true) {
            Task task;
            if (TryPopLocal(task) || scheduler_.TryDequeueGlobal(task) ||
                scheduler_.TryStealTask(index_, task)) {
                Execute(std::move(task));
                continue;
            }

            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stopping_ || !local_queue_.empty() || scheduler_.shutting_down_.load(std::memory_order_acquire) ||
                       scheduler_.HasPendingGlobalTasks();
            });

            if (stopping_ && local_queue_.empty() && !scheduler_.HasPendingGlobalTasks()) {
                break;
            }
        }
        current_ = nullptr;
    }

    bool TryPopLocal(Task& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (local_queue_.empty()) {
            return false;
        }
        task = std::move(local_queue_.back());
        local_queue_.pop_back();
        return true;
    }

    void Execute(Task&& task) {
        boost::fibers::fiber([this, task = std::move(task)]() mutable {
            scheduler_.OnFiberStarted();
            struct FiberCompletion {
                CoroutineScheduler& scheduler;
                ~FiberCompletion() {
                    scheduler.OnFiberFinished();
                }
            };
            FiberCompletion completion{scheduler_};
            task();
        }).detach();
        boost::this_fiber::yield();
    }

    CoroutineScheduler& scheduler_;
    const std::size_t index_;
    std::thread thread_;
    std::deque<Task> local_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{false};

    static thread_local Worker* current_;
};

thread_local CoroutineScheduler::Worker* CoroutineScheduler::Worker::current_ = nullptr;

CoroutineScheduler::CoroutineScheduler(std::size_t worker_count) {
    const std::size_t count = ResolveWorkerCount(worker_count);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto worker = std::make_unique<Worker>(*this, i);
        worker->Start();
        workers_.emplace_back(std::move(worker));
    }
}

CoroutineScheduler::~CoroutineScheduler() {
    Stop();
}

void CoroutineScheduler::Stop() {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    for (auto& worker : workers_) {
        worker->RequestStop();
        worker->Notify();
    }

    for (auto& worker : workers_) {
        worker->Join();
    }

    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.clear();
        global_task_count_.store(0, std::memory_order_release);
    }
}

bool CoroutineScheduler::Enqueue(Task task) {
    if (workers_.empty() || shutting_down_.load(std::memory_order_acquire)) {
        return false;
    }

    if (auto* current = Worker::Current(); current != nullptr) {
        if (current->Enqueue(std::move(task))) {
            return true;
        }
    }

    PushGlobal(std::move(task));
    return true;
}

void CoroutineScheduler::PushGlobal(Task task) {
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_queue_.push_back(std::move(task));
        global_task_count_.fetch_add(1, std::memory_order_release);
    }
    NotifyOneWorker();
}

bool CoroutineScheduler::TryDequeueGlobal(Task& task) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (global_queue_.empty()) {
        return false;
    }
    task = std::move(global_queue_.front());
    global_queue_.pop_front();
    global_task_count_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool CoroutineScheduler::TryStealTask(std::size_t thief_index, Task& task) {
    const auto size = workers_.size();
    if (size <= 1) {
        return false;
    }
    for (std::size_t i = 1; i < size; ++i) {
        const std::size_t victim = (thief_index + i) % size;
        if (workers_[victim]->Steal(task)) {
            return true;
        }
    }
    return false;
}

bool CoroutineScheduler::HasPendingGlobalTasks() const {
    return global_task_count_.load(std::memory_order_acquire) > 0;
}

void CoroutineScheduler::NotifyOneWorker() {
    if (workers_.empty()) {
        return;
    }
    const auto index = next_worker_.fetch_add(1, std::memory_order_relaxed);
    workers_[index % workers_.size()]->Notify();
}

void CoroutineScheduler::OnFiberStarted() {
    active_fibers_.fetch_add(1, std::memory_order_acq_rel);
}

void CoroutineScheduler::OnFiberFinished() {
    active_fibers_.fetch_sub(1, std::memory_order_acq_rel);
}

}  // namespace slg::coroutine
