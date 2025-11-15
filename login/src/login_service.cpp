#include "login_service.h"

#include <utility>

namespace login {

LoginService::LoginService(boost::asio::io_context& io_context, Options options)
    : io_context_(io_context),
      options_(std::move(options)),
      http_client_(std::make_unique<slg::network::http::HttpClient>(io_context_)),
      snowflake_(options_.snowflake.datacenter_id, options_.snowflake.worker_id) {
    InitializeServerLookup();
}

const LoginService::Options& LoginService::GetOptions() const noexcept {
    return options_;
}

slg::network::http::HttpClient& LoginService::HttpClient() noexcept {
    return *http_client_;
}

slg::algorithms::SnowflakeIdGenerator& LoginService::Snowflake() noexcept {
    return snowflake_;
}

const std::unordered_map<std::string, LoginService::ServerInfo>& LoginService::ServerLookup()
    const noexcept {
    return server_lookup_;
}

void LoginService::InitializeServerLookup() {
    server_lookup_.clear();
    for (const auto& server : options_.servers) {
        server_lookup_[server.id] = server;
    }
}

}  // namespace login

