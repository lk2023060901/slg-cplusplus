#include "network/http/http_server.h"

#include <type_traits>
#include <utility>

namespace slg::network::http {

namespace {

HttpResponse InvokeHandler(HttpRequest request,
                           const std::shared_ptr<HttpServer::RequestHandler>& handler,
                           const asio::ip::tcp::endpoint& remote_endpoint) {
    HttpResponse response{http::status::internal_server_error, request.version()};
    response.set(http::field::server, "slg-login");
    response.keep_alive(false);

    try {
        if (handler) {
            response = (*handler)(std::move(request), remote_endpoint.address().to_string());
        }
    } catch (...) {
        response.result(http::status::internal_server_error);
        response.body() = "Unhandled server error";
        response.prepare_payload();
    }

    return response;
}

template <typename StreamType>
class BasicSession : public std::enable_shared_from_this<BasicSession<StreamType>> {
public:
    BasicSession(StreamType stream,
                 std::shared_ptr<HttpServer::RequestHandler> handler,
                 bool requires_handshake)
        : stream_(std::move(stream)), handler_(std::move(handler)), handshake_(requires_handshake) {}

    void Start() {
        if constexpr (std::is_same_v<StreamType, beast::ssl_stream<asio::ip::tcp::socket>>) {
            if (handshake_) {
                auto self = this->shared_from_this();
                stream_.async_handshake(ssl::stream_base::server,
                                        [self](beast::error_code ec) {
                                            if (!ec) {
                                                self->DoRead();
                                            }
                                        });
                return;
            }
        }
        DoRead();
    }

private:
    void DoRead() {
        auto self = this->shared_from_this();
        http::async_read(stream_, buffer_, request_,
                         [self](beast::error_code ec, std::size_t) {
                             if (!ec) {
                                 self->HandleRequest();
                             }
                         });
    }

    void HandleRequest() {
        asio::ip::tcp::endpoint endpoint;
        if constexpr (std::is_same_v<StreamType, asio::ip::tcp::socket>) {
            endpoint = stream_.remote_endpoint();
        } else {
            endpoint = beast::get_lowest_layer(stream_).remote_endpoint();
        }
        auto response = InvokeHandler(std::move(request_), handler_, endpoint);
        auto self = this->shared_from_this();
        http::async_write(stream_, response,
                          [self](beast::error_code, std::size_t) {
                              beast::error_code shutdown_ec;
                              if constexpr (std::is_same_v<StreamType,
                                                            beast::ssl_stream<asio::ip::tcp::socket>>) {
                                  self->stream_.async_shutdown(
                                      [self](beast::error_code) {});
                              } else {
                                  self->stream_.shutdown(asio::ip::tcp::socket::shutdown_send,
                                                         shutdown_ec);
                              }
                          });
    }

    StreamType stream_;
    beast::flat_buffer buffer_;
    HttpRequest request_;
    std::shared_ptr<HttpServer::RequestHandler> handler_;
    bool handshake_{false};
};

}  // namespace

HttpServer::HttpServer(asio::io_context& io_context,
                       const asio::ip::tcp::endpoint& endpoint,
                       RequestHandler handler,
                       std::optional<TlsConfig> tls_config)
    : io_context_(io_context),
      acceptor_(io_context),
      handler_(std::make_shared<RequestHandler>(std::move(handler))) {
    if (tls_config && tls_config->enabled) {
        InitializeTlsContext(*tls_config);
        use_tls_ = true;
    }

    beast::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (!ec) {
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    }
    if (!ec) {
        acceptor_.bind(endpoint, ec);
    }
    if (!ec) {
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    }
    if (ec) {
        throw beast::system_error(ec);
    }
}

void HttpServer::Start() {
    if (running_.exchange(true)) {
        return;
    }
    DoAccept();
}

void HttpServer::Stop() {
    running_.store(false);
    beast::error_code ec;
    acceptor_.close(ec);
}

void HttpServer::DoAccept() {
    acceptor_.async_accept([this](beast::error_code ec, asio::ip::tcp::socket socket) {
        if (!running_) {
            return;
        }

        if (!ec) {
            if (use_tls_ && ssl_context_) {
                beast::ssl_stream<asio::ip::tcp::socket> stream(std::move(socket), *ssl_context_);
                std::make_shared<BasicSession<beast::ssl_stream<asio::ip::tcp::socket>>>(
                    std::move(stream), handler_, true)
                    ->Start();
            } else {
                asio::ip::tcp::socket plain_stream(std::move(socket));
                std::make_shared<BasicSession<asio::ip::tcp::socket>>(std::move(plain_stream),
                                                                      handler_, false)
                    ->Start();
            }
        }

        if (running_) {
            DoAccept();
        }
    });
}

void HttpServer::InitializeTlsContext(const TlsConfig& config) {
    ssl_context_ = std::make_unique<ssl::context>(ssl::context::tls_server);
    auto& ctx = *ssl_context_;
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 |
                    ssl::context::single_dh_use);

    if (!config.password.empty()) {
        ctx.set_password_callback([password = config.password](std::size_t,
                                                               ssl::context::password_purpose) {
            return password;
        });
    }

    if (!config.certificate_chain_file.empty()) {
        ctx.use_certificate_chain_file(config.certificate_chain_file);
    }
    if (!config.private_key_file.empty()) {
        ctx.use_private_key_file(config.private_key_file, ssl::context::pem);
    }
    if (!config.dh_file.empty()) {
        ctx.use_tmp_dh_file(config.dh_file);
    }

    if (config.require_client_cert && !config.ca_file.empty()) {
        ctx.load_verify_file(config.ca_file);
        ctx.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
    }
}

}  // namespace slg::network::http
