#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "algorithms/snowflake/snowflake_id.h"
#include "common/common.pb.h"
#include "common/error_code.pb.h"
#include "login/login.pb.h"
#include "login_service.h"
#include "network/http/http_client.h"

namespace login {

class PlayerLoginHandler {
public:
    struct Dependencies {
        slg::network::http::HttpClient& http_client;
        slg::algorithms::SnowflakeIdGenerator& snowflake;
        const LoginService::Options& options;
        const std::unordered_map<std::string, LoginService::ServerInfo>& server_lookup;
    };

    explicit PlayerLoginHandler(Dependencies deps);
    ~PlayerLoginHandler() = default;

    login::LoginAuthRes Process(const login::LoginAuthReq& request,
                                const std::string& client_ip);

private:
    struct PlatformVerifyResult {
        bool success{false};
        bool banned{false};
        std::string normalized_account_id;
    };

    const LoginService::ServerInfo* FindServer(std::string_view server_id) const;
    PlatformVerifyResult VerifyWithPlatform(const login::LoginAuthReq& request,
                                            const LoginService::ServerInfo& server,
                                            const std::string& client_ip);

    Dependencies deps_;
};

}  // namespace login
