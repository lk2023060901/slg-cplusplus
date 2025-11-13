#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

#include "threading/threading_export.h"

namespace slg::threading {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename Fn, typename... Args>
    auto Submit(Fn&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>> {
        using ReturnType = std::invoke_result_t<std::decay_t<Fn>, std::decay_t<Args>...>;

        if (stopped_.load()) {
            throw std::runtime_error("ThreadPool has been stopped");
        }

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
        auto future = task->get_future();
        boost::asio::post(*pool_, [task]() {
            (*task)();
        });
        return future;
    }

    template <typename Fn>
    void Post(Fn&& fn) {
        if (stopped_.load()) {
            throw std::runtime_error("ThreadPool has been stopped");
        }
        boost::asio::post(*pool_, std::forward<Fn>(fn));
    }

    SLG_THREADING_API void Stop();
    SLG_THREADING_API void Wait();
    SLG_THREADING_API std::size_t ThreadCount() const noexcept;

private:
    std::size_t DetermineThreadCount(std::size_t requested) const;

    std::unique_ptr<boost::asio::thread_pool> pool_;
    std::size_t thread_count_{0};
    std::atomic<bool> stopped_{false};
};

}  // namespace slg::threading
