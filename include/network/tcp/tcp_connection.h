#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include "network/tcp/tcp_export.h"

namespace slg::network::tcp {

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using Socket = boost::asio::ip::tcp::socket;
    using ReceiveHandler = std::function<void(const std::shared_ptr<TcpConnection>&,
                                              const std::uint8_t*,
                                              std::size_t)>;
    using ErrorHandler = std::function<void(const std::shared_ptr<TcpConnection>&,
                                            const boost::system::error_code&)>;

    SLG_TCP_API explicit TcpConnection(Socket socket);

    SLG_TCP_API void Start(std::size_t read_buffer_size,
                           ReceiveHandler on_receive,
                           ErrorHandler on_error);

    SLG_TCP_API void AsyncSend(const std::uint8_t* data, std::size_t size);
    SLG_TCP_API void AsyncSend(std::string_view data);
    SLG_TCP_API void AsyncSend(std::vector<std::uint8_t> data);

    SLG_TCP_API void Close();

    SLG_TCP_API Socket& GetSocket() noexcept;
    SLG_TCP_API const Socket& GetSocket() const noexcept;

    SLG_TCP_API std::string RemoteAddress() const;
    SLG_TCP_API std::uint16_t RemotePort() const;

private:
    void DoRead();
    void HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void DoWrite();

    using Strand = boost::asio::strand<typename Socket::executor_type>;

    Socket socket_;
    Strand strand_;
    std::vector<std::uint8_t> read_buffer_;
    std::deque<std::vector<std::uint8_t>> write_queue_;
    ReceiveHandler on_receive_;
    ErrorHandler on_error_;
    bool writing_{false};
};

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

}  // namespace slg::network::tcp
