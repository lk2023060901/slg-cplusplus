#include "network/tcp/tcp_client.h"

#include <utility>

#include <boost/asio/connect.hpp>

namespace slg::network::tcp {

TcpClient::TcpClient(boost::asio::io_context& io_context)
    : io_context_(io_context), resolver_(io_context), socket_(io_context) {}

void TcpClient::AsyncConnect(const std::string& host,
                             std::uint16_t port,
                             ConnectHandler on_connect,
                             TcpConnection::ReceiveHandler on_receive,
                             TcpConnection::ErrorHandler on_error,
                             std::size_t read_buffer_size) {
    auto self = shared_from_this();
    resolver_.async_resolve(host, std::to_string(port),
                            [this, self, on_connect = std::move(on_connect),
                             on_receive = std::move(on_receive), on_error = std::move(on_error),
                             read_buffer_size](const boost::system::error_code& ec,
                                              boost::asio::ip::tcp::resolver::results_type results) {
                                if (ec) {
                                    if (on_error) {
                                        on_error(nullptr, ec);
                                    }
                                    return;
                                }

                                boost::asio::async_connect(
                                    socket_, results,
                                    [this, self, on_connect, on_receive, on_error, read_buffer_size](
                                        const boost::system::error_code& connect_ec,
                                        const boost::asio::ip::tcp::endpoint&) {
                                        if (connect_ec) {
                                            if (on_error) {
                                                on_error(nullptr, connect_ec);
                                            }
                                            return;
                                        }

                                        auto connection =
                                            std::make_shared<TcpConnection>(std::move(socket_));
                                        connection->Start(read_buffer_size, on_receive, on_error);
                                        if (on_connect) {
                                            on_connect(connection);
                                        }
                                    });
                            });
}

void TcpClient::Cancel() {
    boost::system::error_code ec;
    resolver_.cancel();
    socket_.cancel(ec);
    socket_.close(ec);
}

}  // namespace slg::network::tcp
