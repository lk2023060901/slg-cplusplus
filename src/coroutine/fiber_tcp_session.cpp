#include "coroutine/fiber_tcp_session.h"

#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/system/system_error.hpp>

namespace slg::coroutine {

FiberTcpSession::FiberTcpSession(CoroutineScheduler& scheduler,
                                 tcp::TcpConnectionPtr connection,
                                 std::size_t read_buffer_size)
    : scheduler_(scheduler),
      bridge_(scheduler),
      connection_(std::move(connection)),
      read_buffer_(read_buffer_size) {}

void FiberTcpSession::Start(ReceiveHandler on_receive, ErrorHandler on_error) {
    if (running_.exchange(true)) {
        return;
    }
    on_receive_ = std::move(on_receive);
    on_error_ = std::move(on_error);
    scheduler_.Schedule([self = shared_from_this()]() { self->Run(); });
}

void FiberTcpSession::Stop() {
    stopping_.store(true, std::memory_order_release);
    if (connection_) {
        connection_->Close();
    }
}

bool FiberTcpSession::Running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

boost::fibers::future<IoResult> FiberTcpSession::Send(const std::uint8_t* data, std::size_t size) {
    return bridge_.WriteSome(connection_->GetSocket(), boost::asio::buffer(data, size));
}

boost::fibers::future<IoResult> FiberTcpSession::Send(std::string_view data) {
    return Send(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

void FiberTcpSession::Run() {
    boost::system::error_code last_error;
    while (!stopping_.load(std::memory_order_acquire)) {
        auto future = bridge_.ReadSome(connection_->GetSocket(), boost::asio::buffer(read_buffer_));
        IoResult result{};
        try {
            result = future.get();
        } catch (const std::exception& ex) {
            last_error = boost::system::error_code{boost::system::errc::operation_canceled,
                                                   boost::system::system_category()};
            if (on_error_) {
                on_error_(connection_, last_error);
            }
            break;
        }

        if (!result || result.bytes_transferred == 0) {
            last_error = result.ec ? result.ec
                                   : boost::system::error_code{boost::system::errc::connection_reset,
                                                               boost::system::system_category()};
            if (on_error_) {
                on_error_(connection_, last_error);
            }
            break;
        }

        if (on_receive_) {
            on_receive_(connection_, read_buffer_.data(), result.bytes_transferred);
        }
    }

    running_.store(false, std::memory_order_release);
}

}  // namespace slg::coroutine
