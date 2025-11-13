#include "network/tcp/tcp_connection.h"

#include <algorithm>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

namespace slg::network::tcp {

TcpConnection::TcpConnection(Socket socket)
    : socket_(std::move(socket)), strand_(socket_.get_executor()) {}

void TcpConnection::Start(std::size_t read_buffer_size,
                          ReceiveHandler on_receive,
                          ErrorHandler on_error) {
    read_buffer_.resize(read_buffer_size);
    on_receive_ = std::move(on_receive);
    on_error_ = std::move(on_error);
    DoRead();
}

void TcpConnection::AsyncSend(const std::uint8_t* data, std::size_t size) {
    if (!socket_.is_open()) {
        return;
    }

    auto buffer = std::vector<std::uint8_t>(data, data + size);
    boost::asio::post(strand_, [self = shared_from_this(), buffer = std::move(buffer)]() mutable {
        self->write_queue_.emplace_back(std::move(buffer));
        if (!self->writing_) {
            self->writing_ = true;
            self->DoWrite();
        }
    });
}

void TcpConnection::AsyncSend(std::string_view data) {
    AsyncSend(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

void TcpConnection::AsyncSend(std::vector<std::uint8_t> data) {
    if (!socket_.is_open()) {
        return;
    }

    boost::asio::post(strand_, [self = shared_from_this(), buffer = std::move(data)]() mutable {
        self->write_queue_.emplace_back(std::move(buffer));
        if (!self->writing_) {
            self->writing_ = true;
            self->DoWrite();
        }
    });
}

void TcpConnection::Close() {
    boost::asio::post(strand_, [self = shared_from_this()]() {
        boost::system::error_code ec;
        self->socket_.cancel(ec);
        self->socket_.close(ec);
    });
}

TcpConnection::Socket& TcpConnection::GetSocket() noexcept {
    return socket_;
}

const TcpConnection::Socket& TcpConnection::GetSocket() const noexcept {
    return socket_;
}

std::string TcpConnection::RemoteAddress() const {
    boost::system::error_code ec;
    const auto endpoint = socket_.remote_endpoint(ec);
    return ec ? std::string{} : endpoint.address().to_string();
}

std::uint16_t TcpConnection::RemotePort() const {
    boost::system::error_code ec;
    const auto endpoint = socket_.remote_endpoint(ec);
    return ec ? 0 : endpoint.port();
}

void TcpConnection::DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        boost::asio::bind_executor(
            strand_,
            [self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                self->HandleRead(ec, bytes_transferred);
            }));
}

void TcpConnection::HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred) {
    if (ec) {
        if (on_error_) {
            on_error_(shared_from_this(), ec);
        }
        return;
    }

    if (on_receive_) {
        on_receive_(shared_from_this(), read_buffer_.data(), bytes_transferred);
    }

    DoRead();
}

void TcpConnection::DoWrite() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    auto self = shared_from_this();
    auto& buffer = write_queue_.front();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(buffer),
        boost::asio::bind_executor(
            strand_,
            [self](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    if (self->on_error_) {
                        self->on_error_(self, ec);
                    }
                    return;
                }

                self->write_queue_.pop_front();
                if (!self->write_queue_.empty()) {
                    self->DoWrite();
                } else {
                    self->writing_ = false;
                }
            }));
}

}  // namespace slg::network::tcp
