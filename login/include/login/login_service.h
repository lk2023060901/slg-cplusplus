#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>

#include "algorithms/snowflake/snowflake_id.h"
#include "network/http/http_client.h"
#include "network/http/http_server.h"

namespace login {

class LoginAuthReq;
class LoginAuthRes;

class LoginService {
public:
    struct ServerInfo {
        std::string id;
        std::string region_code;
        std::string name;
        bool online{true};
    };

    struct PlatformAuthConfig {
        std::string host;
        std::uint16_t port{0};
        std::string path;
        bool use_tls{false};
        std::uint32_t timeout_ms{2000};
        std::string app_id;
        std::string app_secret;
    };

    struct SnowflakeConfig {
        std::uint16_t datacenter_id{0};
        std::uint16_t worker_id{0};
    };

    struct Options {
        PlatformAuthConfig platform;
        SnowflakeConfig snowflake;
        std::vector<ServerInfo> servers;
    };

    LoginService(boost::asio::io_context& io_context, Options options);
    ~LoginService();

    bool Start(const std::string& host, std::uint16_t port);
    void Stop();

private:
    struct PlatformVerifyResult {
        bool success{false};
        bool banned{false};
        std::string normalized_account_id;
    };

    slg::network::http::HttpResponse HandleRequest(slg::network::http::HttpRequest&& request,
                                                   const std::string& remote_address);
    LoginAuthRes ProcessLogin(const LoginAuthReq& request, const std::string& client_ip);
    void InitializeServerLookup();
    const ServerInfo* FindServer(std::string_view server_id) const;
    PlatformVerifyResult VerifyWithPlatform(const LoginAuthReq& request,
                                            const ServerInfo& server,
                                            const std::string& client_ip);
    boost::asio::io_context& io_context_;
    Options options_;
    std::unique_ptr<slg::network::http::HttpServer> http_server_;
    std::unique_ptr<slg::network::http::HttpClient> http_client_;
    slg::algorithms::SnowflakeIdGenerator snowflake_;
    std::unordered_map<std::string, ServerInfo> server_lookup_;
};

}  // namespace login
