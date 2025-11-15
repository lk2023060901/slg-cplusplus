#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "algorithms/snowflake/snowflake_id.h"
#include "common/common.pb.h"
#include "common/error_code.pb.h"
#include "login/login.pb.h"
#include "login_service.h"
#include "network/http/http_client.h"

namespace slg::application::protocol {
class SecurityContext;
class ProtocolRegistry;
class TcpProtocolRouter;
}  // namespace slg::application::protocol

namespace login {

class PlayerLoginHandler {
public:
    struct Dependencies {
        slg::network::http::HttpClient& http_client;
        slg::algorithms::SnowflakeIdGenerator& snowflake;
        const LoginService::Options& options;
        const std::unordered_map<std::string, LoginService::ServerInfo>& server_lookup;
    };

    using LoginAuthCallback = std::function<void(LoginAuthRes)>;

    explicit PlayerLoginHandler(Dependencies deps);
    ~PlayerLoginHandler() = default;

    void ProcessAsync(const login::LoginAuthReq& request,
                      const std::string& client_ip,
                      LoginAuthCallback callback);

private:
    struct PlatformVerifyResult {
        bool success{false};
        bool banned{false};
        std::string normalized_account_id;
    };

    const LoginService::ServerInfo* FindServer(std::string_view server_id) const;
    void VerifyWithPlatformAsync(const login::LoginAuthReq& request,
                                 const LoginService::ServerInfo& server,
                                 const std::string& client_ip,
                                 std::function<void(PlatformVerifyResult)> callback);

    Dependencies deps_;
};

void RegisterPlayerProtocols(
    const std::shared_ptr<PlayerLoginHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context,
    slg::application::protocol::ProtocolRegistry& registry);

}  // namespace login
