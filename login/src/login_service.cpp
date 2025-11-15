#include "login/login_service.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

#include "common/error_code.pb.h"
#include "json/json_reader.h"
#include "json/json_value.h"
#include "login/logging_macros.h"
#include "login/login.pb.h"

namespace http = slg::network::http;
namespace beast_http = boost::beast::http;
namespace json = slg::json;

namespace login {

namespace {

constexpr std::string_view kLoginEndpoint{"/login/auth"};

}  // namespace

LoginService::LoginService(boost::asio::io_context& io_context, Options options)
    : io_context_(io_context),
      options_(std::move(options)),
      http_client_(std::make_unique<http::HttpClient>(io_context_)),
      snowflake_(options_.snowflake.datacenter_id, options_.snowflake.worker_id) {
    InitializeServerLookup();
}

LoginService::~LoginService() {
    Stop();
}

bool LoginService::Start(const std::string& host, std::uint16_t port) {
    boost::system::error_code ec;
    auto address = boost::asio::ip::make_address(host, ec);
    if (ec) {
        LOGIN_LOG_ERROR("invalid bind address {}: {}", host, ec.message());
        return false;
    }

    http::HttpServer::RequestHandler handler = [this](http::HttpRequest&& request,
                                                      const std::string& remote) {
        return HandleRequest(std::move(request), remote);
    };

    try {
        http_server_ = std::make_unique<http::HttpServer>(
            io_context_, boost::asio::ip::tcp::endpoint(address, port), handler);
        http_server_->Start();
        LOGIN_LOG_INFO("login HTTP server listening on {}:{}", host, port);
    } catch (const std::exception& ex) {
        LOGIN_LOG_ERROR("failed to start HTTP server: {}", ex.what());
        return false;
    }

    return true;
}

void LoginService::Stop() {
    if (http_server_) {
        http_server_->Stop();
        http_server_.reset();
    }
}

void LoginService::InitializeServerLookup() {
    server_lookup_.clear();
    for (const auto& server : options_.servers) {
        server_lookup_[server.id] = server;
    }
}

const LoginService::ServerInfo* LoginService::FindServer(std::string_view server_id) const {
    if (server_id.empty()) {
        return nullptr;
    }
    auto iter = server_lookup_.find(std::string(server_id));
    if (iter == server_lookup_.end()) {
        return nullptr;
    }
    return &iter->second;
}

http::HttpResponse LoginService::HandleRequest(http::HttpRequest&& request,
                                               const std::string& remote_address) {
    http::HttpResponse response{beast_http::status::ok, request.version()};
    response.set(beast_http::field::content_type, "application/x-protobuf");
    response.keep_alive(false);

    const std::string target_path{request.target().data(), request.target().size()};
    if (request.method() != beast_http::verb::post || target_path != kLoginEndpoint) {
        response.result(beast_http::status::not_found);
        response.body() = "unknown endpoint";
        response.prepare_payload();
        return response;
    }

    LoginAuthReq auth_request;
    if (!auth_request.ParseFromString(request.body())) {
        LOGIN_LOG_WARN("failed to parse LoginAuthReq from {}", remote_address);
        LoginAuthRes error_res;
        error_res.set_err_code(common::ERROR_LOGIN_INVALID_TOKEN);
        response.body() = error_res.SerializeAsString();
        response.prepare_payload();
        return response;
    }

    auto result = ProcessLogin(auth_request, remote_address);
    response.body() = result.SerializeAsString();
    response.prepare_payload();
    return response;
}

LoginAuthRes LoginService::ProcessLogin(const LoginAuthReq& request,
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
    if (!verify_result.normalized_account_id.empty()) {
        response.set_uid(verify_result.normalized_account_id);
    } else {
        response.set_uid(std::to_string(snowflake_.NextId()));
    }
    response.set_selected_server_id(server->id);
    return response;
}

LoginService::PlatformVerifyResult LoginService::VerifyWithPlatform(
    const LoginAuthReq& request,
    const ServerInfo& server,
    const std::string& client_ip) {
    PlatformVerifyResult result;

    http::HttpRequest http_request{beast_http::verb::post,
                                   options_.platform.path.empty() ? "/platform/auth" :
                                                                   options_.platform.path,
                                   11};
    http_request.set(beast_http::field::content_type, "application/json");

    json::JsonValue payload = json::JsonValue::Object();
    payload.Set("app_id", options_.platform.app_id);
    payload.Set("app_secret", options_.platform.app_secret);
    payload.Set("account_id", request.account_id());
    payload.Set("access_token", request.access_token());
    payload.Set("channel", request.channel());
    payload.Set("client_ip", client_ip);
    payload.Set("server_id", server.id);
    http_request.body() = payload.Serialize();
    http_request.prepare_payload();

    const auto timeout = std::chrono::milliseconds(options_.platform.timeout_ms);
    auto http_response =
        http_client_->Request(std::move(http_request), options_.platform.host,
                               options_.platform.port, options_.platform.use_tls, timeout);
    if (!http_response) {
        LOGIN_LOG_WARN("platform verification HTTP request failed for account {}",
                       request.account_id());
        return result;
    }
    if (http_response->result() != beast_http::status::ok) {
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
    result.normalized_account_id = normalized;
    if (result.normalized_account_id.empty()) {
        result.normalized_account_id = request.account_id();
    }
    return result;
}

}  // namespace login
