#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "coroutine/fiber_tcp_session.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_io_context.h"
#include "tests/coroutine/performance/performance_config.h"

namespace {

using namespace std::chrono_literals;
namespace perf = slg::tests::coroutine::performance;

class SessionRegistry {
public:
    void Add(std::uint64_t id, std::shared_ptr<slg::coroutine::FiberTcpSession> session) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[id] = std::move(session);
    }

    void Remove(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(id);
    }

    void StopAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, session] : sessions_) {
            session->Stop();
        }
        sessions_.clear();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<slg::coroutine::FiberTcpSession>> sessions_;
};

TEST(FiberTcpSessionPerformanceTest, EchoesManyClients) {
    const std::size_t client_count = perf::g_fiber_session_client_count;
    const std::size_t messages_per_client = perf::g_fiber_session_messages_per_client;

    slg::network::tcp::TcpIoContext io_context(4);
    io_context.Start();
    auto scheduler = std::make_shared<slg::coroutine::CoroutineScheduler>(4);
    SessionRegistry registry;
    std::atomic<std::uint64_t> next_id{1};

    boost::asio::ip::tcp::acceptor acceptor(io_context.GetContext(),
                                            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    const auto endpoint = acceptor.local_endpoint();

    std::atomic<bool> accepting{true};
    std::thread accept_thread([&]() {
        while (accepting.load(std::memory_order_acquire)) {
            boost::asio::ip::tcp::socket socket(io_context.GetContext());
            boost::system::error_code ec;
            acceptor.accept(socket, ec);
            if (ec) {
                if (accepting.load(std::memory_order_acquire)) {
                    continue;
                }
                break;
            }
            auto connection = std::make_shared<slg::network::tcp::TcpConnection>(std::move(socket));
            const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
            auto session = std::make_shared<slg::coroutine::FiberTcpSession>(*scheduler, connection);
            registry.Add(id, session);
            session->Start(
                [](const slg::network::tcp::TcpConnectionPtr& conn, const std::uint8_t* data, std::size_t size) {
                    conn->AsyncSend(data, size);
                },
                [&, id](const slg::network::tcp::TcpConnectionPtr& conn, const boost::system::error_code&) {
                    if (conn) {
                        conn->Close();
                    }
                    registry.Remove(id);
                });
        }
    });

    std::atomic<std::size_t> success_count{0};
    std::vector<std::thread> clients;
    clients.reserve(client_count);
    for (std::size_t i = 0; i < client_count; ++i) {
        clients.emplace_back([&, i]() {
            boost::asio::io_context client_context;
            boost::asio::ip::tcp::socket socket(client_context);
            socket.connect(endpoint);
            for (std::size_t m = 0; m < messages_per_client; ++m) {
                const std::string payload = "msg-" + std::to_string(i) + "-" + std::to_string(m);
                boost::asio::write(socket, boost::asio::buffer(payload));
                std::vector<char> buffer(payload.size());
                boost::asio::read(socket, boost::asio::buffer(buffer));
                if (std::string(buffer.begin(), buffer.end()) == payload) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            socket.close();
        });
    }

    for (auto& thread : clients) {
        thread.join();
    }

    accepting.store(false, std::memory_order_release);
    acceptor.close();
    accept_thread.join();

    registry.StopAll();
    scheduler->Stop();
    io_context.Stop();
    io_context.Join();

    EXPECT_EQ(success_count.load(), client_count * messages_per_client);
}

TEST(FiberTcpSessionPerformanceTest, HandlesBurstDisconnects) {
    const std::size_t client_count = perf::g_fiber_session_client_count;

    slg::network::tcp::TcpIoContext io_context(4);
    io_context.Start();
    auto scheduler = std::make_shared<slg::coroutine::CoroutineScheduler>(4);
    SessionRegistry registry;
    std::atomic<std::uint64_t> next_id{1};

    boost::asio::ip::tcp::acceptor acceptor(io_context.GetContext(),
                                            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    const auto endpoint = acceptor.local_endpoint();

    std::atomic<bool> accepting{true};
    std::thread accept_thread([&]() {
        while (accepting.load(std::memory_order_acquire)) {
            boost::asio::ip::tcp::socket socket(io_context.GetContext());
            boost::system::error_code ec;
            acceptor.accept(socket, ec);
            if (ec) {
                if (!accepting.load()) {
                    break;
                }
                continue;
            }
            auto connection = std::make_shared<slg::network::tcp::TcpConnection>(std::move(socket));
            const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
            auto session = std::make_shared<slg::coroutine::FiberTcpSession>(*scheduler, connection);
            registry.Add(id, session);
            session->Start(
                [](const slg::network::tcp::TcpConnectionPtr&, const std::uint8_t*, std::size_t) {},
                [&, id](const slg::network::tcp::TcpConnectionPtr& conn, const boost::system::error_code&) {
                    if (conn) {
                        conn->Close();
                    }
                    registry.Remove(id);
                });
        }
    });

    std::vector<std::thread> clients;
    clients.reserve(client_count);
    for (std::size_t i = 0; i < client_count; ++i) {
        clients.emplace_back([endpoint]() {
            boost::asio::io_context client_ctx;
            boost::asio::ip::tcp::socket socket(client_ctx);
            socket.connect(endpoint);
            socket.close();
        });
    }

    for (auto& thread : clients) {
        thread.join();
    }

    accepting.store(false, std::memory_order_release);
    acceptor.close();
    accept_thread.join();

    registry.StopAll();
    scheduler->Stop();
    io_context.Stop();
    io_context.Join();

    SUCCEED();
}

}  // namespace
