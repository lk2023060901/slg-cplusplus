#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/beast/http.hpp>

#include "coroutine/scheduler.h"
#include "network/http/http_client.h"
#include "network/http/http_server.h"
#include "network/http/performance/performance_config.h"

namespace {

namespace asio = boost::asio;
namespace http = boost::beast::http;
using slg::network::http::HttpClient;
using slg::network::http::HttpRequest;
using slg::network::http::HttpResponse;
using slg::network::http::HttpServer;

class LocalHttpServer {
public:
    explicit LocalHttpServer(unsigned short port)
        : endpoint_(asio::ip::make_address("127.0.0.1"), port),
          io_context_(1),
          work_guard_(asio::make_work_guard(io_context_)),
          server_(io_context_,
                  endpoint_,
                  [this](HttpRequest&& request, const std::string& remote) {
                      request_count_.fetch_add(1, std::memory_order_relaxed);
                      HttpResponse response{http::status::ok, request.version()};
                      response.set(http::field::server, "fiber-http-load");
                      response.set(http::field::content_type, "application/json");
                      response.keep_alive(false);
                      response.body() = "{\"path\":\"" + std::string(request.target()) +
                                        "\",\"from\":\"" + remote + "\"}";
                      response.prepare_payload();
                      return response;
                  }) {}

    void Start() {
        server_.Start();
        server_thread_ = std::thread([this]() { io_context_.run(); });
    }

    void Stop() {
        server_.Stop();
        io_context_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    unsigned short port() const {
        return endpoint_.port();
    }

    std::size_t RequestCount() const {
        return request_count_.load(std::memory_order_relaxed);
    }

private:
    asio::ip::tcp::endpoint endpoint_;
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    HttpServer server_;
    std::thread server_thread_;
    std::atomic<std::size_t> request_count_{0};
};

}  // namespace

namespace perf = slg::tests::network::http::performance;

TEST(HttpClientLoadTest, HandlesManyConcurrentFibers) {
    constexpr unsigned short kPort = 19090;
    LocalHttpServer server(kPort);
    server.Start();

    asio::io_context client_io;
    auto client_guard = asio::make_work_guard(client_io);
    std::vector<std::thread> client_threads;
    const auto client_workers = std::max<std::size_t>(2, std::thread::hardware_concurrency());
    client_threads.reserve(client_workers);
    for (std::size_t i = 0; i < client_workers; ++i) {
        client_threads.emplace_back([&client_io]() { client_io.run(); });
    }

    slg::coroutine::CoroutineScheduler scheduler(std::thread::hardware_concurrency());
    std::atomic<bool> all_clients_ok{true};

    auto run_client = [&]() -> bool {
        HttpClient thread_client(client_io);
        for (std::size_t request_index = 0; request_index < perf::g_http_requests_per_client;
             ++request_index) {
            HttpRequest request{http::verb::get, "/load-test", 11};
            request.set(http::field::user_agent, "http-load-test");
            request.prepare_payload();
            auto response = thread_client.Request(
                request, "127.0.0.1", kPort, false, std::chrono::seconds(3));
            if (!response.has_value() || response->result() != http::status::ok ||
                response->body().empty()) {
                return false;
            }
        }
        return true;
    };

    std::vector<boost::fibers::future<bool>> futures;
    futures.reserve(perf::g_http_concurrency);
    for (std::size_t i = 0; i < perf::g_http_concurrency; ++i) {
        futures.emplace_back(scheduler.Schedule(run_client));
    }
    for (auto& future : futures) {
        all_clients_ok.store(all_clients_ok.load(std::memory_order_relaxed) && future.get(),
                             std::memory_order_relaxed);
    }
    EXPECT_TRUE(all_clients_ok.load(std::memory_order_relaxed));
    scheduler.Stop();
    client_guard.reset();
    client_io.stop();
    for (auto& thread : client_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    server.Stop();

    const auto expected =
        perf::g_http_concurrency * perf::g_http_requests_per_client;
    EXPECT_EQ(server.RequestCount(), expected);
}
