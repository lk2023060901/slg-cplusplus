#pragma once

#include <functional>
#include <memory>
#include <string>

#include <boost/asio/ip/tcp.hpp>

#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_export.h"

namespace slg::network::tcp {

class TcpClient : public std::enable_shared_from_this<TcpClient> {
public:
    using ConnectHandler = std::function<void(const TcpConnectionPtr&)>;
    using ErrorHandler = std::function<void(const boost::system::error_code&)>;

    static constexpr std::size_t kDefaultReadBufferSize = 4096;

    SLG_TCP_API explicit TcpClient(boost::asio::io_context& io_context);

    SLG_TCP_API void AsyncConnect(const std::string& host,
                                  std::uint16_t port,
                                  ConnectHandler on_connect,
                                  TcpConnection::ReceiveHandler on_receive,
                                  TcpConnection::ErrorHandler on_error,
                                  std::size_t read_buffer_size = kDefaultReadBufferSize);

    SLG_TCP_API void Cancel();

private:
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
};

using TcpClientPtr = std::shared_ptr<TcpClient>;

}  // namespace slg::network::tcp
