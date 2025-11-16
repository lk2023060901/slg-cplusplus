#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include <boost/fiber/future.hpp>

#include "coroutine/coroutine_export.h"

namespace slg::coroutine {

class SLG_COROUTINE_API CoroutineScheduler {
public:
    using Task = std::function<void()>;

    explicit CoroutineScheduler(std::size_t worker_count = 0);
    ~CoroutineScheduler();

    CoroutineScheduler(const CoroutineScheduler&) = delete;
    CoroutineScheduler& operator=(const CoroutineScheduler&) = delete;

    void Stop();

    template <typename Fn>
    auto Schedule(Fn&& fn) -> boost::fibers::future<std::invoke_result_t<Fn>>;

private:
    friend class Worker;

    bool Enqueue(Task task);
    void PushGlobal(Task task);
    bool TryDequeueGlobal(Task& task);
    bool TryStealTask(std::size_t thief_index, Task& task);
    bool HasPendingGlobalTasks() const;
    void NotifyOneWorker();
    void OnFiberStarted();
    void OnFiberFinished();

    class Worker;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<std::size_t> next_worker_{0};
    std::atomic<bool> shutting_down_{false};
    std::atomic<std::size_t> active_fibers_{0};

    std::deque<Task> global_queue_;
    mutable std::mutex global_mutex_;
    std::atomic<std::size_t> global_task_count_{0};
};

template <typename Fn>
auto CoroutineScheduler::Schedule(Fn&& fn) -> boost::fibers::future<std::invoke_result_t<Fn>> {
    using Result = std::invoke_result_t<Fn>;
    auto promise = std::make_shared<boost::fibers::promise<Result>>();
    auto future = promise->get_future();

    Task task = [func = std::forward<Fn>(fn), promise]() mutable {
        try {
            if constexpr (std::is_void_v<Result>) {
                func();
                promise->set_value();
            } else {
                promise->set_value(func());
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };

    if (!Enqueue(std::move(task))) {
        throw std::runtime_error("CoroutineScheduler is stopped");
    }
    return future;
}

}  // namespace slg::coroutine
