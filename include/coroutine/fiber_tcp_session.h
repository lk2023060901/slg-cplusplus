#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <boost/fiber/future.hpp>

#include "coroutine/asio_bridge.h"
#include "coroutine/coroutine_export.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_server.h"

namespace tcp = slg::network::tcp;

namespace slg::coroutine {

class SLG_COROUTINE_API FiberTcpSession
    : public std::enable_shared_from_this<FiberTcpSession> {
public:
    using ReceiveHandler = tcp::TcpConnection::ReceiveHandler;
    using ErrorHandler = tcp::TcpConnection::ErrorHandler;

    FiberTcpSession(CoroutineScheduler& scheduler,
                    tcp::TcpConnectionPtr connection,
                    std::size_t read_buffer_size = tcp::TcpServer::kDefaultReadBufferSize);

    FiberTcpSession(const FiberTcpSession&) = delete;
    FiberTcpSession& operator=(const FiberTcpSession&) = delete;

    void Start(ReceiveHandler on_receive, ErrorHandler on_error);
    void Stop();
    bool Running() const noexcept;

    boost::fibers::future<IoResult> Send(const std::uint8_t* data, std::size_t size);
    boost::fibers::future<IoResult> Send(std::string_view data);

    const tcp::TcpConnectionPtr& Connection() const noexcept {
        return connection_;
    }

private:
    void Run();

    CoroutineScheduler& scheduler_;
    AsioFiberBridge bridge_;
    tcp::TcpConnectionPtr connection_;
    ReceiveHandler on_receive_;
    ErrorHandler on_error_;
    std::vector<std::uint8_t> read_buffer_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
};

using FiberTcpSessionPtr = std::shared_ptr<FiberTcpSession>;

}  // namespace slg::coroutine
