#include "network/http/http_client.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

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

}  // namespace slg::network::http
