#include "network/http/http_client.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <openssl/err.h>

namespace slg::network::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;

HttpClient::HttpClient(asio::io_context& io_context, HttpClientOptions options)
    : io_context_(io_context), options_(std::move(options)) {}

std::optional<HttpResponse> HttpClient::Request(HttpRequest request,
                                                const std::string& host,
                                                unsigned short port,
                                                bool use_tls,
                                                std::chrono::milliseconds timeout) {
    request.set(http::field::host, host);
    request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    asio::ip::tcp::resolver resolver(io_context_);

    try {
        auto results = resolver.resolve(host, std::to_string(port));

        if (use_tls) {
            ssl::context ctx(ssl::context::tls_client);
            ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                            ssl::context::no_sslv3 | ssl::context::single_dh_use);

            if (!options_.ca_file.empty()) {
                ctx.load_verify_file(options_.ca_file);
            } else {
                ctx.set_default_verify_paths();
            }

            if (!options_.client_cert_file.empty()) {
                ctx.use_certificate_chain_file(options_.client_cert_file);
            }
            if (!options_.client_key_file.empty()) {
                ctx.use_private_key_file(options_.client_key_file, ssl::context::pem);
            }

            ssl::stream<beast::tcp_stream> stream(io_context_, ctx);
            stream.set_verify_mode(options_.verify_peer ? ssl::verify_peer : ssl::verify_none);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                return std::nullopt;
            }

            beast::get_lowest_layer(stream).expires_after(timeout);
            beast::get_lowest_layer(stream).connect(results);

            beast::error_code ec;
            stream.handshake(ssl::stream_base::client, ec);
            if (ec) {
                return std::nullopt;
            }

            http::write(stream, request, ec);
            if (ec) {
                return std::nullopt;
            }

            beast::flat_buffer buffer;
            HttpResponse response;
            http::read(stream, buffer, response, ec);
            if (ec) {
                return std::nullopt;
            }

            stream.shutdown(ec);
            return response;
        } else {
            beast::tcp_stream stream(io_context_);
            stream.expires_after(timeout);
            stream.connect(results);

            http::write(stream, request);
            beast::flat_buffer buffer;
            HttpResponse response;
            http::read(stream, buffer, response);
            beast::error_code ec;
            stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            return response;
        }
    } catch (...) {
        return std::nullopt;
    }
}

namespace {

class AsyncPlainSession : public std::enable_shared_from_this<AsyncPlainSession> {
public:
    AsyncPlainSession(asio::io_context& io_context,
                      HttpClient::AsyncHandler handler,
                      std::chrono::milliseconds timeout)
        : resolver_(asio::make_strand(io_context)),
          stream_(asio::make_strand(io_context)),
          handler_(std::move(handler)),
          timeout_(timeout) {}

    void Run(HttpRequest request, std::string host, unsigned short port) {
        request_ = std::move(request);
        host_ = std::move(host);
        port_ = port;
        request_.set(http::field::host, host_);
        request_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        resolver_.async_resolve(host_, std::to_string(port_),
                                beast::bind_front_handler(&AsyncPlainSession::OnResolve,
                                                         shared_from_this()));
    }

private:
    void OnResolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results) {
        if (ec) {
            return Fail(ec);
        }
        stream_.expires_after(timeout_);
        stream_.async_connect(results,
                              beast::bind_front_handler(&AsyncPlainSession::OnConnect,
                                                        shared_from_this()));
    }

    void OnConnect(beast::error_code ec, const asio::ip::tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            return Fail(ec);
        }
        http::async_write(stream_, request_,
                          beast::bind_front_handler(&AsyncPlainSession::OnWrite,
                                                    shared_from_this()));
    }

    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        http::async_read(stream_, buffer_, response_,
                         beast::bind_front_handler(&AsyncPlainSession::OnRead,
                                                   shared_from_this()));
    }

    void OnRead(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        beast::error_code shutdown_ec;
        stream_.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
        Complete({}, std::move(response_));
    }

    void Fail(beast::error_code ec) {
        Complete(ec, std::nullopt);
    }

    void Complete(beast::error_code ec, std::optional<HttpResponse> response) {
        if (!handler_) {
            return;
        }
        handler_(ec, std::move(response));
        handler_ = nullptr;
    }

    asio::ip::tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
    HttpResponse response_;
    HttpClient::AsyncHandler handler_;
    std::string host_;
    unsigned short port_{0};
    std::chrono::milliseconds timeout_;
};

class AsyncTlsSession : public std::enable_shared_from_this<AsyncTlsSession> {
public:
    AsyncTlsSession(asio::io_context& io_context,
                    HttpClientOptions options,
                    HttpClient::AsyncHandler handler,
                    std::chrono::milliseconds timeout)
        : resolver_(asio::make_strand(io_context)),
          ssl_ctx_(ssl::context::tls_client),
          stream_(asio::make_strand(io_context), ssl_ctx_),
          handler_(std::move(handler)),
          timeout_(timeout) {
        ssl_ctx_.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                             ssl::context::no_sslv3 | ssl::context::single_dh_use);
        if (!options.ca_file.empty()) {
            ssl_ctx_.load_verify_file(options.ca_file);
        } else {
            ssl_ctx_.set_default_verify_paths();
        }
        if (!options.client_cert_file.empty()) {
            ssl_ctx_.use_certificate_chain_file(options.client_cert_file);
        }
        if (!options.client_key_file.empty()) {
            ssl_ctx_.use_private_key_file(options.client_key_file, ssl::context::pem);
        }
        stream_.set_verify_mode(options.verify_peer ? ssl::verify_peer : ssl::verify_none);
    }

    void Run(HttpRequest request, std::string host, unsigned short port) {
        request_ = std::move(request);
        host_ = std::move(host);
        port_ = port;
        request_.set(http::field::host, host_);
        request_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        if (!SSL_set_tlsext_host_name(stream_.native_handle(), host_.c_str())) {
            Fail(beast::error_code(static_cast<int>(::ERR_get_error()),
                                   asio::error::get_ssl_category()));
            return;
        }

        resolver_.async_resolve(host_, std::to_string(port_),
                                beast::bind_front_handler(&AsyncTlsSession::OnResolve,
                                                         shared_from_this()));
    }

private:
    void OnResolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results) {
        if (ec) {
            return Fail(ec);
        }
        beast::get_lowest_layer(stream_).expires_after(timeout_);
        beast::get_lowest_layer(stream_)
            .async_connect(results, beast::bind_front_handler(&AsyncTlsSession::OnConnect,
                                                              shared_from_this()));
    }

    void OnConnect(beast::error_code ec, const asio::ip::tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            return Fail(ec);
        }
        stream_.async_handshake(ssl::stream_base::client,
                                beast::bind_front_handler(&AsyncTlsSession::OnHandshake,
                                                         shared_from_this()));
    }

    void OnHandshake(beast::error_code ec) {
        if (ec) {
            return Fail(ec);
        }
        http::async_write(stream_, request_,
                          beast::bind_front_handler(&AsyncTlsSession::OnWrite,
                                                    shared_from_this()));
    }

    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        http::async_read(stream_, buffer_, response_,
                         beast::bind_front_handler(&AsyncTlsSession::OnRead,
                                                   shared_from_this()));
    }

    void OnRead(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        stream_.async_shutdown(beast::bind_front_handler(&AsyncTlsSession::OnShutdown,
                                                         shared_from_this()));
        Complete({}, std::move(response_));
    }

    void OnShutdown(beast::error_code ec) {
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            ec = {};
        }
        if (ec) {
            Fail(ec);
        }
    }

    void Fail(beast::error_code ec) {
        Complete(ec, std::nullopt);
    }

    void Complete(beast::error_code ec, std::optional<HttpResponse> response) {
        if (!handler_) {
            return;
        }
        handler_(ec, std::move(response));
        handler_ = nullptr;
    }

    asio::ip::tcp::resolver resolver_;
    ssl::context ssl_ctx_;
    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
    HttpResponse response_;
    HttpClient::AsyncHandler handler_;
    std::string host_;
    unsigned short port_{0};
    std::chrono::milliseconds timeout_;
};

}  // namespace

void HttpClient::AsyncRequest(HttpRequest request,
                              const std::string& host,
                              unsigned short port,
                              bool use_tls,
                              std::chrono::milliseconds timeout,
                              AsyncHandler handler) {
    if (use_tls) {
        auto session =
            std::make_shared<AsyncTlsSession>(io_context_, options_, std::move(handler), timeout);
        session->Run(std::move(request), host, port);
    } else {
        auto session = std::make_shared<AsyncPlainSession>(io_context_, std::move(handler), timeout);
        session->Run(std::move(request), host, port);
    }
}

}  // namespace slg::network::http
