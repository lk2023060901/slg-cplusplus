#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <thread>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/fiber/all.hpp>
#include <boost/asio/ssl.hpp>

#include "network/http/http_client.h"

namespace {

namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;

using slg::network::http::HttpClient;
using slg::network::http::HttpRequest;
using slg::network::http::HttpResponse;

HttpRequest MakeGetRequest(const std::string& target) {
    HttpRequest request{http::verb::get, target, 11};
    request.prepare_payload();
    return request;
}

class TestHttpServer {
public:
    struct Options {
        Options()
            : body("OK"),
              status(http::status::ok),
              response_delay(std::chrono::milliseconds(0)),
              respond(true),
              use_tls(false),
              abort_on_accept(false) {}

        std::string body;
        http::status status;
        std::chrono::milliseconds response_delay;
        bool respond;
        bool use_tls;
        std::string certificate_chain_file;
        std::string private_key_file;
        bool abort_on_accept;
    };

    TestHttpServer(unsigned short port, Options options = Options())
        : io_context_(1),
          acceptor_(io_context_,
                    asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port)),
          options_(std::move(options)) {
        port_ = acceptor_.local_endpoint().port();
    }

    ~TestHttpServer() {
        Stop();
    }

    void Start() {
        if (running_.exchange(true)) {
            return;
        }
        io_context_.restart();
        StartAccept();
        thread_ = std::thread([this]() { io_context_.run(); });
    }

    void Stop() {
        if (!running_.exchange(false)) {
            return;
        }
        asio::post(io_context_, [this]() {
            boost::system::error_code ec;
            acceptor_.close(ec);
        });
        io_context_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    unsigned short port() const {
        return port_;
    }

private:
    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(asio::ip::tcp::socket socket, Options options)
            : socket_(std::move(socket)),
              options_(std::move(options)),
              timer_(socket_.get_executor()) {}

        void Start() {
            http::async_read(socket_, buffer_, request_,
                             [self = shared_from_this()](const boost::system::error_code& ec,
                                                         std::size_t) {
                                 if (ec) {
                                     return;
                                 }
                                 self->OnReadComplete();
                             });
        }

    private:
        void OnReadComplete() {
            if (!options_.respond) {
                return;
            }
            if (options_.response_delay.count() > 0) {
                timer_.expires_after(options_.response_delay);
                timer_.async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
                    if (!ec) {
                        self->WriteResponse();
                    }
                });
            } else {
                WriteResponse();
            }
        }

        void WriteResponse() {
            response_.version(request_.version());
            response_.result(options_.status);
            response_.set(http::field::server, "test-http-server");
            response_.set(http::field::content_type, "text/plain");
            response_.body() = options_.body;
            response_.prepare_payload();

            http::async_write(
                socket_, response_,
                [self = shared_from_this()](const boost::system::error_code&, std::size_t) {
                    boost::system::error_code ec;
                    self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                });
        }

        asio::ip::tcp::socket socket_;
        Options options_;
        boost::beast::flat_buffer buffer_;
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;
        asio::steady_timer timer_;
    };

    class TlsSession : public std::enable_shared_from_this<TlsSession> {
    public:
        TlsSession(asio::ip::tcp::socket socket, ssl::context& context, Options options)
            : stream_(std::move(socket), context),
              options_(std::move(options)),
              timer_(stream_.get_executor()) {}

        void Start() {
            stream_.async_handshake(ssl::stream_base::server,
                                    [self = shared_from_this()](const boost::system::error_code& ec) {
                                        if (ec) {
                                            return;
                                        }
                                        self->DoRead();
                                    });
        }

    private:
        void DoRead() {
            http::async_read(stream_, buffer_, request_,
                             [self = shared_from_this()](const boost::system::error_code& ec,
                                                         std::size_t) {
                                 if (ec) {
                                     return;
                                 }
                                 self->OnReadComplete();
                             });
        }

        void OnReadComplete() {
            if (!options_.respond) {
                return;
            }
            if (options_.response_delay.count() > 0) {
                timer_.expires_after(options_.response_delay);
                timer_.async_wait([self = shared_from_this()](const boost::system::error_code& ec) {
                    if (!ec) {
                        self->WriteResponse();
                    }
                });
            } else {
                WriteResponse();
            }
        }

        void WriteResponse() {
            response_.version(request_.version());
            response_.result(options_.status);
            response_.set(http::field::server, "test-https-server");
            response_.set(http::field::content_type, "text/plain");
            response_.body() = options_.body;
            response_.prepare_payload();
            http::async_write(
                stream_, response_,
                [self = shared_from_this()](const boost::system::error_code&, std::size_t) {
                    boost::system::error_code ec;
                    self->stream_.shutdown(ec);
                });
        }

        ssl::stream<asio::ip::tcp::socket> stream_;
        Options options_;
        boost::beast::flat_buffer buffer_;
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;
        asio::steady_timer timer_;
    };

    void StartAccept() {
        if (options_.use_tls && !ssl_context_) {
            ssl_context_ = std::make_unique<ssl::context>(ssl::context::tls_server);
            ssl_context_->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                                      ssl::context::no_sslv3 | ssl::context::single_dh_use);
            if (!options_.certificate_chain_file.empty()) {
                ssl_context_->use_certificate_chain_file(options_.certificate_chain_file);
            }
            if (!options_.private_key_file.empty()) {
                ssl_context_->use_private_key_file(options_.private_key_file, ssl::context::pem);
            }
        }

        acceptor_.async_accept(
            [this](const boost::system::error_code& ec, asio::ip::tcp::socket socket) {
                if (ec || !acceptor_.is_open()) {
                    return;
                }
                if (options_.abort_on_accept) {
                    boost::system::error_code close_ec;
                    socket.close(close_ec);
                } else if (options_.use_tls && ssl_context_) {
                    std::make_shared<TlsSession>(std::move(socket), *ssl_context_, options_)->Start();
                } else {
                    std::make_shared<Session>(std::move(socket), options_)->Start();
                }
                StartAccept();
            });
    }

    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    unsigned short port_{0};
    Options options_;
    std::unique_ptr<ssl::context> ssl_context_;
};

class HttpClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        client_io_.restart();
        work_guard_.emplace(asio::make_work_guard(client_io_));
        client_thread_ = std::thread([this]() { client_io_.run(); });
    }

    void TearDown() override {
        work_guard_.reset();
        client_io_.stop();
        if (client_thread_.joinable()) {
            client_thread_.join();
        }
    }

    asio::io_context client_io_;
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
    std::thread client_thread_;
};

std::string TestDataPath(const std::string& relative) {
#ifdef SLG_PROJECT_ROOT
    std::filesystem::path root(SLG_PROJECT_ROOT);
#else
    std::filesystem::path root = std::filesystem::current_path();
#endif
    return (root / "tests" / "network" / "http" / relative).string();
}

TEST_F(HttpClientTest, PlainRequestWithinFiberReturnsResponse) {
    TestHttpServer::Options options;
    options.body = "fiber-response";
    TestHttpServer server(0, options);
    server.Start();

    HttpClient client(client_io_);
    std::optional<HttpResponse> response;

    boost::fibers::fiber request_fiber([&]() {
        response = client.Request(MakeGetRequest("/"), "127.0.0.1", server.port(), false,
                                  std::chrono::milliseconds(500));
    });
    request_fiber.join();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->result(), http::status::ok);
    EXPECT_EQ(response->body(), "fiber-response");

    server.Stop();
}

TEST_F(HttpClientTest, AsyncRequestDeliversResponse) {
    TestHttpServer::Options options;
    options.body = "async-response";
    TestHttpServer server(0, options);
    server.Start();

    HttpClient client(client_io_);
    std::promise<std::optional<HttpResponse>> promise;
    auto future = promise.get_future();

    client.AsyncRequest(MakeGetRequest("/async"), "127.0.0.1", server.port(), false,
                        std::chrono::milliseconds(500),
                        [&promise](const boost::system::error_code& ec,
                                   std::optional<HttpResponse> response) mutable {
                            if (ec) {
                                promise.set_value(std::nullopt);
                                return;
                            }
                            promise.set_value(std::move(response));
                        });

    auto status = future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready);
    auto response = future.get();
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->body(), "async-response");

    server.Stop();
}

TEST_F(HttpClientTest, RequestTimesOutWhenServerIsSlow) {
    TestHttpServer::Options options;
    options.response_delay = std::chrono::seconds(2);
    TestHttpServer server(0, options);
    server.Start();

    HttpClient client(client_io_);
    auto response = client.Request(MakeGetRequest("/slow"), "127.0.0.1", server.port(), false,
                                   std::chrono::milliseconds(100));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestWithSelfSignedCertificateSucceeds) {
    TestHttpServer::Options options;
    options.body = "tls-response";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/test_server.crt");
    options.private_key_file = TestDataPath("certs/test_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = true;
    client_options.ca_file = options.certificate_chain_file;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->result(), http::status::ok);
    EXPECT_EQ(response->body(), "tls-response");

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestFailsWithBadCertificate) {
    TestHttpServer::Options options;
    options.body = "tls-response";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/test_server.crt");
    options.private_key_file = TestDataPath("certs/test_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = true;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-fail"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestFailsForExpiredCertificate) {
    TestHttpServer::Options options;
    options.body = "tls-expired";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/expired_server.crt");
    options.private_key_file = TestDataPath("certs/expired_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = true;
    client_options.ca_file = TestDataPath("certs/test_ca.crt");
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-expired"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestSucceedsWithoutVerificationEvenIfExpired) {
    TestHttpServer::Options options;
    options.body = "tls-expired";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/expired_server.crt");
    options.private_key_file = TestDataPath("certs/expired_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = false;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-expired"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->body(), "tls-expired");

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestTimesOutWhenServerDelaysResponse) {
    TestHttpServer::Options options;
    options.body = "tls-timeout";
    options.use_tls = true;
    options.response_delay = std::chrono::seconds(2);
    options.certificate_chain_file = TestDataPath("certs/test_server.crt");
    options.private_key_file = TestDataPath("certs/test_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = true;
    client_options.ca_file = options.certificate_chain_file;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-timeout"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(100));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestFailsForNotYetValidCertificate) {
    TestHttpServer::Options options;
    options.body = "tls-notyet";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/notyet_server.crt");
    options.private_key_file = TestDataPath("certs/notyet_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = true;
    client_options.ca_file = TestDataPath("certs/test_ca.crt");
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-notyet"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsHandshakeFailsWhenServerIsPlain) {
    TestHttpServer::Options options;
    options.body = "plain-server";
    options.use_tls = false;

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = false;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-handshake"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

TEST_F(HttpClientTest, HttpsRequestSucceedsWithoutVerificationEvenIfNotYetValid) {
    TestHttpServer::Options options;
    options.body = "tls-notyet";
    options.use_tls = true;
    options.certificate_chain_file = TestDataPath("certs/notyet_server.crt");
    options.private_key_file = TestDataPath("certs/notyet_server.key");

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = false;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-notyet"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->body(), "tls-notyet");

    server.Stop();
}

TEST_F(HttpClientTest, HttpsHandshakeFailsWhenServerDropsConnectionImmediately) {
    TestHttpServer::Options options;
    options.use_tls = true;
    options.abort_on_accept = true;

    TestHttpServer server(0, options);
    server.Start();

    slg::network::http::HttpClientOptions client_options;
    client_options.verify_peer = false;
    HttpClient client(client_io_, client_options);

    auto response = client.Request(MakeGetRequest("/tls-drop"), "127.0.0.1", server.port(), true,
                                   std::chrono::milliseconds(500));

    EXPECT_FALSE(response.has_value());

    server.Stop();
}

}  // namespace
