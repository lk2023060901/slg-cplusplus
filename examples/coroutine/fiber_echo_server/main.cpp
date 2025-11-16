#include <atomic>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <boost/asio/ip/tcp.hpp>

#include "coroutine/fiber_tcp_session.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_server.h"
#include "network/tcp/tcp_io_context.h"

namespace {

std::atomic<bool> g_running{true};

void HandleSignal(int signal) {
    std::cout << "[fiber_echo_server] received signal " << signal << ", shutting down" << std::endl;
    g_running.store(false, std::memory_order_release);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t port = 9100;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    slg::network::tcp::TcpIoContext io_context(std::thread::hardware_concurrency());
    io_context.Start();
    auto scheduler = std::make_shared<slg::coroutine::CoroutineScheduler>();

    std::mutex sessions_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<slg::coroutine::FiberTcpSession>> sessions;
    std::atomic<std::uint64_t> next_connection_id{1};

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
    auto server = std::make_shared<slg::network::tcp::TcpServer>(io_context.GetContext(), endpoint);
    std::cout << "[fiber_echo_server] listening on 0.0.0.0:" << port << std::endl;

    server->Start(
        [&](const slg::network::tcp::TcpConnectionPtr& conn) {
            const auto connection_id = next_connection_id.fetch_add(1, std::memory_order_relaxed);
            conn->SetConnectionId(connection_id);
            auto session = std::make_shared<slg::coroutine::FiberTcpSession>(*scheduler, conn);
            {
                std::lock_guard<std::mutex> lock(sessions_mutex);
                sessions[connection_id] = session;
            }
            session->Start(
                [](const slg::network::tcp::TcpConnectionPtr& connection,
                   const std::uint8_t* data,
                   std::size_t size) {
                    connection->AsyncSend(data, size);
                },
                [&, connection_id](const slg::network::tcp::TcpConnectionPtr& connection,
                                   const boost::system::error_code& ec) {
                    if (connection) {
                        std::cout << "[fiber_echo_server] connection " << connection_id
                                  << " closed: " << ec.message() << std::endl;
                        connection->Close();
                    }
                    std::lock_guard<std::mutex> lock(sessions_mutex);
                    sessions.erase(connection_id);
                });
        },
        {},
        {},
        slg::network::tcp::TcpServer::kDefaultReadBufferSize,
        false);

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->Stop();

    {
        std::lock_guard<std::mutex> lock(sessions_mutex);
        for (auto& [id, session] : sessions) {
            session->Stop();
        }
        sessions.clear();
    }

    scheduler->Stop();
    io_context.Stop();
    io_context.Join();
    std::cout << "[fiber_echo_server] shutdown complete" << std::endl;
    return 0;
}
