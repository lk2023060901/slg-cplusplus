#pragma once

#include <chrono>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/fiber/future.hpp>

#include "network/http/http_export.h"

namespace slg::network::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

struct HttpEndpoint {
    std::string host;
    unsigned short port{0};
    bool use_tls{false};
};

struct HttpClientOptions {
    bool verify_peer{true};
    std::string ca_file;
    std::string client_cert_file;
    std::string client_key_file;
    std::string sni_hostname;
};

class SLG_HTTP_API HttpClient {
public:
    using AsyncHandler =
        std::function<void(const boost::system::error_code&, HttpResponse&& response)>;

    explicit HttpClient(asio::io_context& io_context, HttpClientOptions options = {});

    HttpResponse Request(HttpRequest request,
                         const HttpEndpoint& endpoint,
                         std::chrono::milliseconds timeout = std::chrono::seconds(5));

    boost::fibers::future<HttpResponse> RequestFuture(HttpRequest request,
                                                      const HttpEndpoint& endpoint,
                                                      std::chrono::milliseconds timeout =
                                                          std::chrono::seconds(5));

    void RequestAsync(HttpRequest request,
                      const HttpEndpoint& endpoint,
                      std::chrono::milliseconds timeout,
                      AsyncHandler handler);

private:
    asio::io_context& io_context_;
    HttpClientOptions options_;
};

}  // namespace slg::network::http
