#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "coroutine/fiber_tcp_session.h"
#include "coroutine/scheduler.h"
#include "network/tcp/tcp_client.h"
#include "network/tcp/tcp_connection.h"
#include "network/tcp/tcp_io_context.h"

namespace {

TEST(TcpConnectorFiberTest, ReconnectsAfterFailure) {
    slg::network::tcp::TcpIoContext io_context(1);
    io_context.Start();

    auto client = std::make_shared<slg::network::tcp::TcpClient>(io_context.GetContext());
    std::atomic<int> error_count{0};
    std::promise<void> reconnected;

    client->AsyncConnect("127.0.0.1", 65530,
        [&](const slg::network::tcp::TcpConnectionPtr& conn) {
            reconnected.set_value();
        },
        [](const slg::network::tcp::TcpConnectionPtr&, const std::uint8_t*, std::size_t) {},
        [&](const slg::network::tcp::TcpConnectionPtr&, const boost::system::error_code&) {
            if (++error_count == 1) {
                client->AsyncConnect("127.0.0.1", 65530,
                    [&](const slg::network::tcp::TcpConnectionPtr&) {
                        reconnected.set_value();
                    },
                    [](const slg::network::tcp::TcpConnectionPtr&, const std::uint8_t*, std::size_t) {},
                    [&](const slg::network::tcp::TcpConnectionPtr&, const boost::system::error_code&) {
                        error_count.fetch_add(1, std::memory_order_relaxed);
                    });
            }
        });

    EXPECT_EQ(reconnected.get_future().wait_for(std::chrono::milliseconds(200)), std::future_status::timeout);
    io_context.Stop();
    io_context.Join();
}

}  // namespace
