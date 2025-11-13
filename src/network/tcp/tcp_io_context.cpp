#include "network/tcp/tcp_io_context.h"

#include <algorithm>
#include <thread>

namespace slg::network::tcp {

TcpIoContext::TcpIoContext(std::size_t thread_count) : thread_count_(thread_count) {
    if (thread_count_ == 0) {
        const auto hw = std::thread::hardware_concurrency();
        thread_count_ = hw == 0 ? 1 : hw;
    }
}

TcpIoContext::~TcpIoContext() {
    Stop();
    Join();
}

boost::asio::io_context& TcpIoContext::GetContext() noexcept {
    return io_context_;
}

std::size_t TcpIoContext::ThreadCount() const noexcept {
    return thread_count_;
}

void TcpIoContext::Start() {
    EnsureStarted();
}

void TcpIoContext::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (work_) {
        work_.reset();
    }

    io_context_.stop();
}

void TcpIoContext::Join() {
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void TcpIoContext::EnsureStarted() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    io_context_.restart();
    work_ = std::make_unique<WorkGuard>(boost::asio::make_work_guard(io_context_));
    threads_.reserve(thread_count_);
    for (std::size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back([this]() {
            io_context_.run();
        });
    }
}

}  // namespace slg::network::tcp
