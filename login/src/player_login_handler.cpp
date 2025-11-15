#include "player_login_handler.h"

#include <chrono>

#include <boost/beast/http.hpp>

#include "json/json_reader.h"
#include "json/json_value.h"
#include "logging_macros.h"

namespace json = slg::json;

namespace login {

PlayerLoginHandler::PlayerLoginHandler(Dependencies deps) : deps_(deps) {}

const LoginService::ServerInfo* PlayerLoginHandler::FindServer(std::string_view server_id) const {
    if (server_id.empty()) {
        return nullptr;
    }
    auto iter = deps_.server_lookup.find(std::string(server_id));
    if (iter == deps_.server_lookup.end()) {
        return nullptr;
    }
    return &iter->second;
}

LoginAuthRes PlayerLoginHandler::Process(const LoginAuthReq& request,
                                         const std::string& client_ip) {
    LoginAuthRes response;
    response.set_selected_server_id(request.selected_server_id());

    const auto* server = FindServer(request.selected_server_id());
    if (!server) {
        response.set_err_code(common::ERROR_LOGIN_SERVER_NOT_FOUND);
        return response;
    }
    if (!server->online) {
        response.set_err_code(common::ERROR_LOGIN_SERVER_UNAVAILABLE);
        return response;
    }

    if (request.account_id().empty() || request.access_token().empty()) {
        response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
        return response;
    }

    const auto verify_result = VerifyWithPlatform(request, *server, client_ip);
    if (!verify_result.success) {
        response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
        return response;
    }
    if (verify_result.banned) {
        response.set_err_code(common::ERROR_LOGIN_ACCOUNT_BANNED);
        return response;
    }

    if (verify_result.normalized_account_id.empty()) {
        response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
        return response;
    }

    response.set_err_code(common::ERROR_SUCCESS);
    response.set_uid(verify_result.normalized_account_id.empty()
                         ? std::to_string(deps_.snowflake.NextId())
                         : verify_result.normalized_account_id);
    response.set_selected_server_id(server->id);
    return response;
}

PlayerLoginHandler::PlatformVerifyResult PlayerLoginHandler::VerifyWithPlatform(
    const LoginAuthReq& request,
    const LoginService::ServerInfo& server,
    const std::string& client_ip) {
    PlatformVerifyResult result;

    slg::network::http::HttpRequest http_request{boost::beast::http::verb::post,
                                                 deps_.options.platform.path.empty()
                                                     ? "/platform/auth"
                                                     : deps_.options.platform.path,
                                                 11};
    http_request.set(boost::beast::http::field::content_type, "application/json");

    json::JsonValue payload = json::JsonValue::Object();
    payload.Set("app_id", deps_.options.platform.app_id);
    payload.Set("app_secret", deps_.options.platform.app_secret);
    payload.Set("account_id", request.account_id());
    payload.Set("access_token", request.access_token());
    payload.Set("channel", request.channel());
    payload.Set("client_ip", client_ip);
    payload.Set("server_id", server.id);
    http_request.body() = payload.Serialize();
    http_request.prepare_payload();

    const auto timeout = std::chrono::milliseconds(deps_.options.platform.timeout_ms);
    auto http_response = deps_.http_client.Request(std::move(http_request),
                                                   deps_.options.platform.host,
                                                   deps_.options.platform.port,
                                                   deps_.options.platform.use_tls, timeout);
    if (!http_response) {
        LOGIN_LOG_WARN("platform verification HTTP request failed for account {}",
                       request.account_id());
        return result;
    }
    if (http_response->result() != boost::beast::http::status::ok) {
        LOGIN_LOG_WARN("platform verification returned status {} for account {}",
                       static_cast<unsigned>(http_response->result()), request.account_id());
        return result;
    }

    json::JsonReader reader;
    auto parsed = reader.ParseString(http_response->body());
    if (!parsed.has_value() || !parsed->IsObject()) {
        LOGIN_LOG_WARN("platform verification body is not valid JSON");
        return result;
    }

    const bool success = parsed->GetAs<bool>("success").value_or(false);
    const bool banned = parsed->GetAs<bool>("banned").value_or(false);
    auto normalized = parsed->GetAs<std::string>("account_id").value_or(request.account_id());

    if (!success) {
        return result;
    }

    result.success = true;
    result.banned = banned;
    result.normalized_account_id = normalized.empty() ? request.account_id() : normalized;
    return result;
}

}  // namespace login

