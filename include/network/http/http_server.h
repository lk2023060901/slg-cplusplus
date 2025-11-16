#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>

#include "network/http/http_client.h"
#include "network/http/http_export.h"

namespace slg::network::http {

using HttpRequestHandler =
    std::function<HttpResponse(HttpRequest&& request, const asio::ip::tcp::endpoint& remote)>;

struct HttpServerOptions {
    asio::ip::tcp::endpoint endpoint;
    bool use_tls{false};
    HttpClientOptions tls_options;
};

class SLG_HTTP_API HttpServer {
public:
    HttpServer(asio::io_context& io_context,
               HttpServerOptions options,
               HttpRequestHandler handler);

    void Start();
    void Stop();

private:
    asio::io_context& io_context_;
    HttpServerOptions options_;
    HttpRequestHandler handler_;
    std::unique_ptr<asio::ssl::context> ssl_context_;
};

}  // namespace slg::network::http
