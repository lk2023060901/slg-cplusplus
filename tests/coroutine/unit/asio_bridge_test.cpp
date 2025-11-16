#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <thread>

#include <boost/asio.hpp>

#include "coroutine/asio_bridge.h"
#include "coroutine/scheduler.h"

namespace {

class IoContextRunner {
public:
    IoContextRunner() : work_guard_(boost::asio::make_work_guard(io_context_)), thread_([this]() { io_context_.run(); }) {}
    ~IoContextRunner() {
        work_guard_.reset();
        io_context_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    boost::asio::io_context& Context() { return io_context_; }

private:
    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread thread_;
};

TEST(AsioFiberBridgeTest, ReadWriteCompletes) {
    IoContextRunner runner;
    boost::asio::ip::tcp::acceptor acceptor(runner.Context(),
                                            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    const auto endpoint = acceptor.local_endpoint();

    boost::asio::ip::tcp::socket client(runner.Context());
    std::thread client_thread([&]() {
        client.connect(endpoint);
        boost::asio::write(client, boost::asio::buffer("ping", 4));
        std::array<char, 4> response{};
        boost::asio::read(client, boost::asio::buffer(response));
        EXPECT_EQ(std::string(response.data(), response.size()), "pong");
        client.close();
    });

    boost::asio::ip::tcp::socket server_socket(runner.Context());
    acceptor.accept(server_socket);

    slg::coroutine::CoroutineScheduler scheduler(1);
    slg::coroutine::AsioFiberBridge bridge(scheduler);

    auto future = scheduler.Schedule([&]() {
        std::array<char, 4> data{};
        auto read_future = bridge.ReadSome(server_socket, boost::asio::buffer(data));
        auto read_result = read_future.get();
        EXPECT_EQ(read_result.bytes_transferred, 4u);
        EXPECT_EQ(std::string(data.data(), data.size()), "ping");

        auto write_future = bridge.WriteSome(server_socket, boost::asio::buffer("pong", 4));
        auto write_result = write_future.get();
        EXPECT_EQ(write_result.bytes_transferred, 4u);
        return true;
    });

    EXPECT_TRUE(future.get());
    scheduler.Stop();
    server_socket.close();
    client_thread.join();
}

}  // namespace
