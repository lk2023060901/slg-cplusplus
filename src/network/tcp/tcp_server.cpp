#include "network/tcp/tcp_server.h"

#include <utility>

namespace slg::network::tcp {

TcpServer::TcpServer(boost::asio::io_context& io_context,
                     const boost::asio::ip::tcp::endpoint& endpoint)
    : io_context_(io_context), acceptor_(io_context) {
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
}

void TcpServer::Start(AcceptHandler on_accept,
                      TcpConnection::ReceiveHandler on_receive,
                      TcpConnection::ErrorHandler on_error,
                      std::size_t read_buffer_size) {
    on_accept_ = std::move(on_accept);
    on_receive_ = std::move(on_receive);
    on_error_ = std::move(on_error);
    read_buffer_size_ = read_buffer_size;
    running_ = true;
    DoAccept();
}

void TcpServer::Stop() {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.close(ec);
}

bool TcpServer::IsRunning() const noexcept {
    return running_;
}

void TcpServer::DoAccept() {
    if (!running_) {
        return;
    }

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (ec) {
            if (on_error_) {
                on_error_(TcpConnectionPtr{}, ec);
            }
        } else {
            auto connection = std::make_shared<TcpConnection>(std::move(*socket));
            connection->Start(read_buffer_size_, on_receive_, on_error_);
            if (on_accept_) {
                on_accept_(connection);
            }
        }

        DoAccept();
    });
}

}  // namespace slg::network::tcp
