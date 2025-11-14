#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include "network/http/http_export.h"

namespace slg::network::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

struct TlsConfig {
    bool enabled{false};
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string dh_file;
    std::string password;
    bool require_client_cert{false};
    std::string ca_file;
};

class SLG_HTTP_API HttpServer {
public:
    using RequestHandler = std::function<HttpResponse(HttpRequest&&, const std::string&)>;

    HttpServer(asio::io_context& io_context,
               const asio::ip::tcp::endpoint& endpoint,
               RequestHandler handler,
               std::optional<TlsConfig> tls_config = std::nullopt);

    void Start();
    void Stop();

private:
    class PlainSession;
    class TlsSession;

    void DoAccept();
    void InitializeTlsContext(const TlsConfig& config);

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<RequestHandler> handler_;
    std::atomic<bool> running_{false};
    bool use_tls_{false};
    std::unique_ptr<ssl::context> ssl_context_;
};

}  // namespace slg::network::http
