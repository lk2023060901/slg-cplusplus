#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "application/application.h"
#include "application/protocol/protocol_registry.h"
#include "application/protocol/security_context.h"
#include "application/protocol/tcp_protocol_router.h"
#include "coroutine/fiber_tcp_session.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_connection.h"

namespace {

struct DummySecurityContext : public slg::application::protocol::SecurityContext {
    DummySecurityContext() : slg::application::protocol::SecurityContext(nullptr, nullptr, nullptr) {}
    std::vector<std::uint8_t> Encode(std::uint32_t, const std::uint8_t* data, std::size_t size, std::uint32_t) const override {
        return std::vector<std::uint8_t>(data, data + size);
    }
};

TEST(TcpListenerFiberIntegrationTest, RouterReceivesData) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    auto registry = std::make_shared<slg::application::protocol::ProtocolRegistry>();
    auto security = std::make_shared<DummySecurityContext>();
    auto router = std::make_shared<slg::application::protocol::TcpProtocolRouter>(registry, security);

    bool invoked = false;
    registry->Register(123, [&](const slg::application::protocol::PacketHeader&, const slg::network::tcp::TcpConnectionPtr&, const std::uint8_t*, std::size_t) {
        invoked = true;
    });

    // Create in-memory tcp pair
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::tcp::v4(), 0});
    auto endpoint = acceptor.local_endpoint();
    boost::asio::ip::tcp::socket server_socket(io);
    std::thread accept_thread([&]() { acceptor.accept(server_socket); });
    boost::asio::ip::tcp::socket client_socket(io);
    client_socket.connect(endpoint);
    accept_thread.join();

    auto connection = std::make_shared<slg::network::tcp::TcpConnection>(std::move(server_socket));
    auto session = std::make_shared<slg::coroutine::FiberTcpSession>(scheduler, connection);
    session->Start([
                       &router
                   ](const slg::network::tcp::TcpConnectionPtr& conn, const std::uint8_t* data, std::size_t size) {
        slg::application::protocol::PacketHeader header{};
        header.command = 123;
        header.length = static_cast<std::uint32_t>(size);
        router->OnReceive(conn, data, size);
    },
                   [](const slg::network::tcp::TcpConnectionPtr&, const boost::system::error_code&) {});

    std::string packet = "hello";
    boost::asio::write(client_socket, boost::asio::buffer(packet));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(invoked);
    client_socket.close();
    session->Stop();
    scheduler.Stop();
}

}  // namespace
