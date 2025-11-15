#include "player_login_handler.h"

#include <chrono>

#include <boost/beast/http.hpp>

#include "application/protocol/protocol_registry.h"
#include "application/protocol/security_context.h"
#include "application/protocol/tcp_protocol_router.h"
#include "client/enums.pb.h"
#include "common/common.pb.h"
#include "json/json_reader.h"
#include "json/json_value.h"
#include "logging_macros.h"
#include "network/tcp/tcp_connection.h"

namespace json = slg::json;

namespace login {

namespace {

std::uint64_t CurrentTimestampMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

slg::application::protocol::CommandHandler OnLoginAuth(
    const std::shared_ptr<PlayerLoginHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context) {
    return [handler, security_context](const slg::application::protocol::PacketHeader& header,
                                      const slg::network::tcp::TcpConnectionPtr& conn,
                                      const std::uint8_t* data,
                                      std::size_t size) {
        login::LoginAuthReq request;
        if (!request.ParseFromArray(data, static_cast<int>(size))) {
            LOGIN_LOG_WARN("failed to parse LoginAuthReq from {}", conn->RemoteAddress());
            return;
        }
        auto sequence = header.sequence;
        auto connection = conn;
        handler->ProcessAsync(
            request, conn->RemoteAddress(),
            [security_context, connection, sequence](LoginAuthRes response) {
                connection->AsyncSend(security_context->EncodeMessage(
                    static_cast<std::uint32_t>(client::CMD_LOGIN_AUTH_RES), response, sequence));
            });
    };
}

slg::application::protocol::CommandHandler OnPlayerHeartbeat(
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context) {
    return [security_context](const slg::application::protocol::PacketHeader& header,
                             const slg::network::tcp::TcpConnectionPtr& conn,
                             const std::uint8_t* data,
                             std::size_t size) {
        common::HeartbeatReq heartbeat_req;
        if (!heartbeat_req.ParseFromArray(data, static_cast<int>(size))) {
            LOGIN_LOG_WARN("invalid heartbeat from {}", conn->RemoteAddress());
            return;
        }
        common::HeartbeatRes heartbeat_res;
        heartbeat_res.set_client_timestamp(heartbeat_req.client_timestamp());
        heartbeat_res.set_server_timestamp(CurrentTimestampMs());
        conn->AsyncSend(security_context->EncodeMessage(
            static_cast<std::uint32_t>(client::CMD_HEARTBEAT_RES), heartbeat_res,
            header.sequence));
    };
}

}  // namespace

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

void PlayerLoginHandler::ProcessAsync(const LoginAuthReq& request,
                                      const std::string& client_ip,
                                      LoginAuthCallback callback) {
    LoginAuthRes response;
    response.set_selected_server_id(request.selected_server_id());

    const auto* server = FindServer(request.selected_server_id());
    if (!server) {
        response.set_err_code(common::ERROR_LOGIN_SERVER_NOT_FOUND);
        callback(std::move(response));
        return;
    }
    if (!server->online) {
        response.set_err_code(common::ERROR_LOGIN_SERVER_UNAVAILABLE);
        callback(std::move(response));
        return;
    }

    if (request.account_id().empty() || request.access_token().empty()) {
        response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
        callback(std::move(response));
        return;
    }

    VerifyWithPlatformAsync(
        request, *server, client_ip,
        [this, response = std::move(response), callback = std::move(callback), server](
            PlatformVerifyResult verify_result) mutable {
            if (!verify_result.success) {
                response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
                callback(std::move(response));
                return;
            }
            if (verify_result.banned) {
                response.set_err_code(common::ERROR_LOGIN_ACCOUNT_BANNED);
                callback(std::move(response));
                return;
            }
            if (verify_result.normalized_account_id.empty()) {
                response.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
                callback(std::move(response));
                return;
            }
            response.set_err_code(common::ERROR_SUCCESS);
            response.set_uid(verify_result.normalized_account_id.empty()
                                 ? std::to_string(deps_.snowflake.NextId())
                                 : verify_result.normalized_account_id);
            response.set_selected_server_id(server->id);
            callback(std::move(response));
        });
}

void PlayerLoginHandler::VerifyWithPlatformAsync(
    const LoginAuthReq& request,
    const LoginService::ServerInfo& server,
    const std::string& client_ip,
    std::function<void(PlatformVerifyResult)> callback) {
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
    deps_.http_client.AsyncRequest(
        std::move(http_request), deps_.options.platform.host, deps_.options.platform.port,
        deps_.options.platform.use_tls, timeout,
        [request, callback = std::move(callback)](
            const boost::system::error_code& ec,
            std::optional<slg::network::http::HttpResponse> response) mutable {
            PlatformVerifyResult result;
            if (ec || !response) {
                LOGIN_LOG_WARN("platform verification HTTP request failed for account {}",
                               request.account_id());
                callback(result);
                return;
            }
            if (response->result() != boost::beast::http::status::ok) {
                LOGIN_LOG_WARN("platform verification returned status {} for account {}",
                               static_cast<unsigned>(response->result()), request.account_id());
                callback(result);
                return;
            }
            json::JsonReader reader;
            auto parsed = reader.ParseString(response->body());
            if (!parsed.has_value() || !parsed->IsObject()) {
                LOGIN_LOG_WARN("platform verification body is not valid JSON");
                callback(result);
                return;
            }
            const bool success = parsed->GetAs<bool>("success").value_or(false);
            const bool banned = parsed->GetAs<bool>("banned").value_or(false);
            auto normalized = parsed->GetAs<std::string>("account_id")
                                   .value_or(request.account_id());
            if (!success) {
                callback(result);
                return;
            }
            result.success = true;
            result.banned = banned;
            result.normalized_account_id =
                normalized.empty() ? request.account_id() : normalized;
            callback(result);
        });
}

void RegisterPlayerProtocols(
    const std::shared_ptr<PlayerLoginHandler>& handler,
    const std::shared_ptr<slg::application::protocol::SecurityContext>& security_context,
    slg::application::protocol::ProtocolRegistry& registry) {
    registry.Register(static_cast<std::uint32_t>(client::CMD_LOGIN_AUTH_REQ),
                      OnLoginAuth(handler, security_context));

    registry.Register(static_cast<std::uint32_t>(client::CMD_HEARTBEAT_REQ),
                      OnPlayerHeartbeat(security_context));
}

}  // namespace login
