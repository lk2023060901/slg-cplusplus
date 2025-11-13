#include "threading/thread_pool.h"

#include <algorithm>
#include <thread>

namespace slg::threading {

ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(DetermineThreadCount(thread_count)),
      pool_(std::make_unique<boost::asio::thread_pool>(thread_count_)) {}

ThreadPool::~ThreadPool() {
    try {
        if (!stopped_.load()) {
            pool_->join();
        }
    } catch (...) {
        // swallow
    }
}

void ThreadPool::Stop() {
    if (!stopped_.exchange(true)) {
        pool_->stop();
    }
}

void ThreadPool::Wait() {
    if (pool_) {
        pool_->join();
        stopped_.store(true);
    }
}

std::size_t ThreadPool::ThreadCount() const noexcept {
    return thread_count_;
}

std::size_t ThreadPool::DetermineThreadCount(std::size_t requested) const {
    if (requested > 0) {
        return requested;
    }
    const auto hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

}  // namespace slg::threading
