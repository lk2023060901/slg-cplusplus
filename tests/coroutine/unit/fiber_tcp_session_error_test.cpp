#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include <boost/asio.hpp>

#include "coroutine/fiber_tcp_session.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_connection.h"

namespace {

class IoRunner {
public:
    IoRunner() : guard_(boost::asio::make_work_guard(io_)), thread_([this]() { io_.run(); }) {}
    ~IoRunner() {
        guard_.reset();
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    boost::asio::io_context& io() { return io_; }

private:
    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_;
    std::thread thread_;
};

std::shared_ptr<slg::network::tcp::TcpConnection> MakeConnection(IoRunner& runner,
                                                                 boost::asio::ip::tcp::socket& peer) {
    boost::asio::ip::tcp::acceptor acceptor(runner.io(), {boost::asio::ip::tcp::v4(), 0});
    auto endpoint = acceptor.local_endpoint();
    std::thread accept_thread([&]() {
        boost::asio::ip::tcp::socket server_socket(runner.io());
        acceptor.accept(server_socket);
        peer = std::move(server_socket);
    });
    boost::asio::ip::tcp::socket client_socket(runner.io());
    client_socket.connect(endpoint);
    accept_thread.join();
    acceptor.close();
    auto connection = std::make_shared<slg::network::tcp::TcpConnection>(std::move(client_socket));
    return connection;
}

TEST(FiberTcpSessionErrorsTest, SendFailsIfPeerClosed) {
    IoRunner runner;
    boost::asio::ip::tcp::socket peer_socket(runner.io());
    auto connection = MakeConnection(runner, peer_socket);

    slg::coroutine::CoroutineScheduler scheduler(1);
    auto session = std::make_shared<slg::coroutine::FiberTcpSession>(scheduler, connection, 1024);
    session->Start(
        [](const slg::network::tcp::TcpConnectionPtr&, const std::uint8_t*, std::size_t) {},
        [](const slg::network::tcp::TcpConnectionPtr&, const boost::system::error_code&) {});

    peer_socket.close();
    auto result = scheduler.Schedule([session]() {
        auto future = session->Send(reinterpret_cast<const std::uint8_t*>("test"), 4);
        auto res = future.get();
        return res.ec;
    });

    auto ec = result.get();
    EXPECT_NE(ec, boost::system::error_code());

    session->Stop();
    scheduler.Stop();
}

}  // namespace
