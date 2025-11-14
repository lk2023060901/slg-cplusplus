#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include "network/http/http_export.h"

namespace slg::network::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

struct HttpClientOptions {
    bool verify_peer{true};
    std::string ca_file;
    std::string client_cert_file;
    std::string client_key_file;
};

class SLG_HTTP_API HttpClient {
public:
    explicit HttpClient(asio::io_context& io_context, HttpClientOptions options = {});

    std::optional<HttpResponse> Request(HttpRequest request,
                                        const std::string& host,
                                        unsigned short port,
                                        bool use_tls = false,
                                        std::chrono::milliseconds timeout =
                                            std::chrono::seconds(5));

private:
    asio::io_context& io_context_;
    HttpClientOptions options_;
};

}  // namespace slg::network::http
