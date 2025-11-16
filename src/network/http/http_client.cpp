#include "network/http/http_client.h"

#include <stdexcept>
#include <utility>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <openssl/err.h>

namespace slg::network::http {

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

    void Run(HttpRequest request, HttpEndpoint endpoint) {
        request_ = std::move(request);
        endpoint_ = std::move(endpoint);
        request_.set(http::field::host, endpoint_.host);
        request_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        resolver_.async_resolve(endpoint_.host,
                                std::to_string(endpoint_.port),
                                beast::bind_front_handler(&AsyncPlainSession::OnResolve,
                                                          shared_from_this()));
    }

private:
    void ApplyTimeout() {
        if (timeout_.count() > 0) {
            stream_.expires_after(timeout_);
        } else {
            stream_.expires_never();
        }
    }

    void OnResolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
        stream_.async_connect(results,
                              beast::bind_front_handler(&AsyncPlainSession::OnConnect,
                                                        shared_from_this()));
    }

    void OnConnect(beast::error_code ec,
                   const asio::ip::tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
        http::async_write(stream_, request_,
                          beast::bind_front_handler(&AsyncPlainSession::OnWrite,
                                                    shared_from_this()));
    }

    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
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
        Complete(ec, HttpResponse{});
    }

    void Complete(beast::error_code ec, HttpResponse&& response) {
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
    std::chrono::milliseconds timeout_;
    HttpEndpoint endpoint_;
};

class AsyncTlsSession : public std::enable_shared_from_this<AsyncTlsSession> {
public:
    AsyncTlsSession(asio::io_context& io_context,
                    std::shared_ptr<asio::ssl::context> ssl_context,
                    HttpClient::AsyncHandler handler,
                    std::chrono::milliseconds timeout,
                    HttpClientOptions options)
        : resolver_(asio::make_strand(io_context)),
          stream_(asio::make_strand(io_context), *ssl_context),
          ssl_context_(std::move(ssl_context)),
          handler_(std::move(handler)),
          timeout_(timeout),
          options_(std::move(options)) {}

    void Run(HttpRequest request, HttpEndpoint endpoint) {
        request_ = std::move(request);
        endpoint_ = std::move(endpoint);
        request_.set(http::field::host, endpoint_.host);
        request_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        resolver_.async_resolve(endpoint_.host,
                                std::to_string(endpoint_.port),
                                beast::bind_front_handler(&AsyncTlsSession::OnResolve,
                                                          shared_from_this()));
    }

private:
    void ApplyTimeout() {
        auto& lowest = beast::get_lowest_layer(stream_);
        if (timeout_.count() > 0) {
            lowest.expires_after(timeout_);
        } else {
            lowest.expires_never();
        }
    }

    void OnResolve(beast::error_code ec, asio::ip::tcp::resolver::results_type results) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
        beast::get_lowest_layer(stream_)
            .async_connect(results,
                           beast::bind_front_handler(&AsyncTlsSession::OnConnect,
                                                     shared_from_this()));
    }

    void OnConnect(beast::error_code ec,
                   const asio::ip::tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            return Fail(ec);
        }

        const std::string sni = options_.sni_hostname.empty() ? endpoint_.host : options_.sni_hostname;
        if (!SSL_set_tlsext_host_name(stream_.native_handle(), sni.c_str())) {
            beast::error_code sni_ec(static_cast<int>(::ERR_get_error()),
                                     asio::error::get_ssl_category());
            return Fail(sni_ec);
        }

        ApplyTimeout();
        stream_.async_handshake(asio::ssl::stream_base::client,
                                beast::bind_front_handler(&AsyncTlsSession::OnHandshake,
                                                          shared_from_this()));
    }

    void OnHandshake(beast::error_code ec) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
        http::async_write(stream_, request_,
                          beast::bind_front_handler(&AsyncTlsSession::OnWrite,
                                                    shared_from_this()));
    }

    void OnWrite(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        ApplyTimeout();
        http::async_read(stream_, buffer_, response_,
                         beast::bind_front_handler(&AsyncTlsSession::OnRead,
                                                   shared_from_this()));
    }

    void OnRead(beast::error_code ec, std::size_t) {
        if (ec) {
            return Fail(ec);
        }
        beast::error_code shutdown_ec;
        stream_.async_shutdown(
            beast::bind_front_handler(&AsyncTlsSession::OnShutdown, shared_from_this()));
        (void)shutdown_ec;
    }

    void OnShutdown(beast::error_code ec) {
        (void)ec;
        Complete({}, std::move(response_));
    }

    void Fail(beast::error_code ec) {
        Complete(ec, HttpResponse{});
    }

    void Complete(beast::error_code ec, HttpResponse&& response) {
        if (!handler_) {
            return;
        }
        handler_(ec, std::move(response));
        handler_ = nullptr;
    }

    asio::ip::tcp::resolver resolver_;
    beast::ssl_stream<beast::tcp_stream> stream_;
    std::shared_ptr<asio::ssl::context> ssl_context_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
    HttpResponse response_;
    HttpClient::AsyncHandler handler_;
    std::chrono::milliseconds timeout_;
    HttpEndpoint endpoint_;
    HttpClientOptions options_;
};

std::shared_ptr<asio::ssl::context> CreateSslContext(const HttpClientOptions& options) {
    auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
    context->set_options(asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
                         asio::ssl::context::no_sslv3 | asio::ssl::context::single_dh_use);
    if (!options.ca_file.empty()) {
        context->load_verify_file(options.ca_file);
    } else {
        context->set_default_verify_paths();
    }

    if (!options.client_cert_file.empty()) {
        context->use_certificate_chain_file(options.client_cert_file);
    }
    if (!options.client_key_file.empty()) {
        context->use_private_key_file(options.client_key_file, asio::ssl::context::pem);
    }

    context->set_verify_mode(options.verify_peer ? asio::ssl::verify_peer
                                                 : asio::ssl::verify_none);
    return context;
}

}  // namespace

HttpClient::HttpClient(asio::io_context& io_context, HttpClientOptions options)
    : io_context_(io_context), options_(std::move(options)) {}

HttpResponse HttpClient::Request(HttpRequest request,
                                 const HttpEndpoint& endpoint,
                                 std::chrono::milliseconds timeout) {
    auto future = RequestFuture(std::move(request), endpoint, timeout);
    return future.get();
}

boost::fibers::future<HttpResponse> HttpClient::RequestFuture(HttpRequest request,
                                                              const HttpEndpoint& endpoint,
                                                              std::chrono::milliseconds timeout) {
    boost::fibers::promise<HttpResponse> promise;
    auto future = promise.get_future();
    auto shared_promise = std::make_shared<boost::fibers::promise<HttpResponse>>(std::move(promise));
    RequestAsync(std::move(request), endpoint, timeout,
                 [shared_promise](const boost::system::error_code& ec, HttpResponse&& response) mutable {
                     if (ec) {
                         shared_promise->set_exception(
                             std::make_exception_ptr(beast::system_error(ec)));
                         return;
                     }
                     shared_promise->set_value(std::move(response));
                 });
    return future;
}

void HttpClient::RequestAsync(HttpRequest request,
                              const HttpEndpoint& endpoint,
                              std::chrono::milliseconds timeout,
                              AsyncHandler handler) {
    if (!handler) {
        throw std::invalid_argument("AsyncRequest handler must not be empty");
    }

    if (endpoint.use_tls) {
        auto session = std::make_shared<AsyncTlsSession>(
            io_context_, CreateSslContext(options_), std::move(handler), timeout, options_);
        session->Run(std::move(request), endpoint);
        return;
    }

    auto session =
        std::make_shared<AsyncPlainSession>(io_context_, std::move(handler), timeout);
    session->Run(std::move(request), endpoint);
}

}  // namespace slg::network::http
