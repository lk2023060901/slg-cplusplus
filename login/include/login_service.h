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
    const Options& GetOptions() const noexcept;
    slg::network::http::HttpClient& HttpClient() noexcept;
    slg::algorithms::SnowflakeIdGenerator& Snowflake() noexcept;
    const std::unordered_map<std::string, ServerInfo>& ServerLookup() const noexcept;

private:
    void InitializeServerLookup();

    boost::asio::io_context& io_context_;
    Options options_;
    std::unique_ptr<slg::network::http::HttpClient> http_client_;
    slg::algorithms::SnowflakeIdGenerator snowflake_;
    std::unordered_map<std::string, ServerInfo> server_lookup_;
};

}  // namespace login
