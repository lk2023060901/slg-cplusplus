#pragma once

#include <functional>
#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_export.h"

namespace slg::network::tcp {

class TcpServer {
public:
    using AcceptHandler = std::function<void(const TcpConnectionPtr&)>;
    using ErrorHandler = std::function<void(const boost::system::error_code&)>;

    static constexpr std::size_t kDefaultReadBufferSize = 4096;

    SLG_TCP_API TcpServer(boost::asio::io_context& io_context,
                          const boost::asio::ip::tcp::endpoint& endpoint);

    SLG_TCP_API void Start(AcceptHandler on_accept,
                           TcpConnection::ReceiveHandler on_receive,
                           TcpConnection::ErrorHandler on_error,
                           std::size_t read_buffer_size = kDefaultReadBufferSize,
                           bool auto_start = true);

    SLG_TCP_API void Stop();
    SLG_TCP_API bool IsRunning() const noexcept;

private:
    void DoAccept();

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    AcceptHandler on_accept_;
    TcpConnection::ReceiveHandler on_receive_;
    TcpConnection::ErrorHandler on_error_;
    std::size_t read_buffer_size_{kDefaultReadBufferSize};
    bool auto_start_{true};
    bool running_{false};
};

}  // namespace slg::network::tcp
