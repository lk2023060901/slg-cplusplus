#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>
#include <boost/fiber/future.hpp>

#include "coroutine/coroutine_export.h"
#include "coroutine/scheduler.h"

namespace slg::coroutine {

struct IoResult {
    boost::system::error_code ec;
    std::size_t bytes_transferred{0};

    explicit operator bool() const noexcept {
        return !ec;
    }
};

class SLG_COROUTINE_API AsioFiberBridge {
public:
    explicit AsioFiberBridge(CoroutineScheduler& scheduler)
        : scheduler_(scheduler) {}

    template <typename MutableBufferSequence>
    boost::fibers::future<IoResult> ReadSome(boost::asio::ip::tcp::socket& socket,
                                             const MutableBufferSequence& buffers) {
        return StartIo([&socket, buffers](auto&& handler) mutable {
            socket.async_read_some(buffers, std::forward<decltype(handler)>(handler));
        });
    }

    template <typename ConstBufferSequence>
    boost::fibers::future<IoResult> WriteSome(boost::asio::ip::tcp::socket& socket,
                                              const ConstBufferSequence& buffers) {
        return StartIo([&socket, buffers](auto&& handler) mutable {
            socket.async_write_some(buffers, std::forward<decltype(handler)>(handler));
        });
    }

    boost::fibers::future<boost::system::error_code> Connect(boost::asio::ip::tcp::socket& socket,
                                                             const boost::asio::ip::tcp::endpoint& endpoint) {
        return StartConnect([&socket, endpoint](auto&& handler) mutable {
            socket.async_connect(endpoint, std::forward<decltype(handler)>(handler));
        });
    }

    template <typename Endpoints>
    boost::fibers::future<boost::system::error_code> Connect(boost::asio::ip::tcp::socket& socket,
                                                             const Endpoints& endpoints) {
        return StartConnect([&socket, endpoints](auto&& handler) mutable {
            boost::asio::async_connect(socket, endpoints, std::forward<decltype(handler)>(handler));
        });
    }

private:
    template <typename Initiation>
    boost::fibers::future<IoResult> StartIo(Initiation&& initiation) {
        auto promise = std::make_shared<boost::fibers::promise<IoResult>>();
        auto future = promise->get_future();
        auto handler = [this, promise](const boost::system::error_code& ec, std::size_t bytes) mutable {
            scheduler_.Schedule([promise, ec, bytes]() mutable {
                promise->set_value(IoResult{ec, bytes});
            });
        };
        initiation(std::move(handler));
        return future;
    }

    template <typename Initiation>
    boost::fibers::future<boost::system::error_code> StartConnect(Initiation&& initiation) {
        auto promise = std::make_shared<boost::fibers::promise<boost::system::error_code>>();
        auto future = promise->get_future();
        auto handler = [this, promise](const boost::system::error_code& ec) mutable {
            scheduler_.Schedule([promise, ec]() mutable {
                promise->set_value(ec);
            });
        };
        initiation(std::move(handler));
        return future;
    }

    CoroutineScheduler& scheduler_;
};

}  // namespace slg::coroutine
