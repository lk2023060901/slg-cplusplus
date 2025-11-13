#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include "network/tcp/tcp_export.h"

namespace slg::network::tcp {

class TcpIoContext {
public:
    SLG_TCP_API explicit TcpIoContext(std::size_t thread_count = 0);
    SLG_TCP_API ~TcpIoContext();

    TcpIoContext(const TcpIoContext&) = delete;
    TcpIoContext& operator=(const TcpIoContext&) = delete;

    SLG_TCP_API boost::asio::io_context& GetContext() noexcept;
    SLG_TCP_API std::size_t ThreadCount() const noexcept;

    SLG_TCP_API void Start();
    SLG_TCP_API void Stop();
    SLG_TCP_API void Join();

private:
    void EnsureStarted();

    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    boost::asio::io_context io_context_;
    std::unique_ptr<WorkGuard> work_;
    std::vector<std::thread> threads_;
    std::size_t thread_count_{0};
    std::atomic<bool> running_{false};
};

}  // namespace slg::network::tcp
