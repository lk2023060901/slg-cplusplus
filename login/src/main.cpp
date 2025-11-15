#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "application/application.h"
#include "application/protocol/protocol_registry.h"
#include "application/protocol/tcp_protocol_router.h"
#include "json/json_value.h"
#include "logging/logging_config.h"
#include "logging/logging_manager.h"
#include "internal_service_handler.h"
#include "logging_macros.h"
#include "login/login.pb.h"
#include "login_build_info.h"
#include "login_service.h"
#include "network/tcp/tcp_connection.h"
#include "player_login_handler.h"

namespace app = slg::application;
namespace json = slg::json;
namespace logging = slg::logging;

namespace {

std::atomic<int> g_received_signal{0};
std::shared_ptr<login::LoginService> g_login_service;

struct ServiceConfig {
    std::string name{"login-service"};
    int shard_id{0};
};

login::LoginService::PlatformAuthConfig LoadPlatformAuthConfig(const json::JsonValue& root) {
    auto platform_section = root.Get("platform_auth");
    if (!platform_section.has_value() || !platform_section->IsObject()) {
        throw std::runtime_error("Missing 'platform_auth' configuration section");
    }

    login::LoginService::PlatformAuthConfig cfg;
    if (auto host = platform_section->GetAs<std::string>("host")) {
        cfg.host = *host;
    }
    if (auto port = platform_section->GetAs<std::uint16_t>("port")) {
        cfg.port = *port;
    }
    if (auto path = platform_section->GetAs<std::string>("path")) {
        cfg.path = *path;
    }
    if (auto use_tls = platform_section->GetAs<bool>("use_tls")) {
        cfg.use_tls = *use_tls;
    }
    if (auto timeout = platform_section->GetAs<std::uint32_t>("timeout_ms")) {
        cfg.timeout_ms = *timeout;
    }
    if (auto app_id = platform_section->GetAs<std::string>("app_id")) {
        cfg.app_id = *app_id;
    }
    if (auto secret = platform_section->GetAs<std::string>("app_secret")) {
        cfg.app_secret = *secret;
    }

    if (cfg.host.empty() || cfg.port == 0 || cfg.app_id.empty()) {
        throw std::runtime_error("Invalid platform_auth configuration");
    }

    if (cfg.path.empty()) {
        cfg.path = "/platform/auth";
    }
    return cfg;
}

std::vector<login::LoginService::ServerInfo> LoadServerInfo(const json::JsonValue& root) {
    auto servers_section = root.Get("servers");
    if (!servers_section.has_value() || !servers_section->IsArray()) {
        throw std::runtime_error("Missing 'servers' configuration array");
    }

    std::vector<login::LoginService::ServerInfo> servers;
    for (std::size_t i = 0; i < servers_section->Raw().size(); ++i) {
        auto item = servers_section->Get(i);
        if (!item.has_value() || !item->IsObject()) {
            continue;
        }
        auto id = item->GetAs<std::string>("id");
        if (!id || id->empty()) {
            continue;
        }
        login::LoginService::ServerInfo server;
        server.id = *id;
        server.name = item->GetAs<std::string>("name").value_or(server.id);
        server.region_code = item->GetAs<std::string>("region_code").value_or("global");
        server.online = item->GetAs<bool>("online").value_or(true);
        servers.push_back(std::move(server));
    }

    if (servers.empty()) {
        throw std::runtime_error("At least one server must be configured");
    }

    return servers;
}

login::LoginService::Options LoadLoginServiceOptions(const json::JsonValue& root) {
    login::LoginService::Options options;
    options.platform = LoadPlatformAuthConfig(root);
    options.servers = LoadServerInfo(root);
    return options;
}

ServiceConfig LoadServiceConfig(const json::JsonValue& root) {
    ServiceConfig cfg;
    auto service_section = root.Get("service");
    if (service_section.has_value() && service_section->IsObject()) {
        if (auto name = service_section->GetAs<std::string>("name")) {
            cfg.name = *name;
        }
        if (auto shard = service_section->GetAs<int>("shard_id")) {
            cfg.shard_id = *shard;
        }
    }
    return cfg;
}

void ApplyServiceContext(const ServiceConfig& cfg) {
    login::logging::SetServiceContext(cfg.name, cfg.shard_id);
}

void InitializeLogging(const json::JsonValue& root) {
    auto logging_section = root.Get("logging");
    if (!logging_section.has_value() || !logging_section->IsObject()) {
        return;
    }

    try {
        const auto config = logging::LoadLoggingConfigFromJson(logging_section->Raw());
        logging::LoggingManagerInstance().LoadConfig(config);
    } catch (const std::exception& ex) {
        std::cerr << "[login] failed to initialize logging: " << ex.what() << std::endl;
        throw;
    }
}

void LogStartupInfo(const ServiceConfig& service) {
    LOGIN_LOG_INFO("service context name={} shard_id={}", service.name, service.shard_id);
    LOGIN_LOG_INFO("build version={} timestamp={} git_hash={}", login::build_info::kVersion,
                   login::build_info::kTimestamp, login::build_info::kGitHash);
}

void RegisterTcpHandlers(app::Application& app_instance,
                         const std::shared_ptr<login::LoginService>& service) {
    namespace protocol = slg::application::protocol;
    auto player_handler = std::make_shared<login::PlayerLoginHandler>(
        login::PlayerLoginHandler::Dependencies{service->HttpClient(), service->Snowflake(),
                                                service->GetOptions(), service->ServerLookup()});
    auto internal_handler = std::make_shared<login::InternalServiceHandler>();

    auto player_security_context = app_instance.CreateListenerSecurityContext("player_handler");
    auto internal_security_context =
        app_instance.CreateListenerSecurityContext("internal_handler");

    auto player_registry = std::make_shared<protocol::ProtocolRegistry>();
    login::RegisterPlayerProtocols(player_handler, player_security_context, *player_registry);
    auto player_router =
        std::make_shared<protocol::TcpProtocolRouter>(player_registry, player_security_context);

    auto internal_registry = std::make_shared<protocol::ProtocolRegistry>();
    login::RegisterInternalProtocols(internal_handler, internal_security_context,
                                     *internal_registry);
    auto internal_router = std::make_shared<protocol::TcpProtocolRouter>(
        internal_registry, internal_security_context);

    app_instance.RegisterListenerHandler(
        "player_handler",
        [player_router](const slg::network::tcp::TcpConnectionPtr& conn) {
            player_router->OnAccept(conn);
        },
        [player_router](const slg::network::tcp::TcpConnectionPtr& conn,
                        const std::uint8_t* data,
                        std::size_t size) {
            player_router->OnReceive(conn, data, size);
        },
        [player_router](const slg::network::tcp::TcpConnectionPtr& conn,
                        const boost::system::error_code& ec) {
            player_router->OnError(conn, ec);
        });

    app_instance.RegisterListenerHandler(
        "internal_handler",
        [internal_router](const slg::network::tcp::TcpConnectionPtr& conn) {
            internal_router->OnAccept(conn);
        },
        [internal_router](const slg::network::tcp::TcpConnectionPtr& conn,
                          const std::uint8_t* data,
                          std::size_t size) {
            internal_router->OnReceive(conn, data, size);
        },
        [internal_router](const slg::network::tcp::TcpConnectionPtr& conn,
                          const boost::system::error_code& ec) {
            internal_router->OnError(conn, ec);
        });
}

}  // namespace

int main(int argc, const char* argv[]) {
    app::Application::Options options;
    options.name = "login-service";
    options.version = "0.1.0";
    options.description = "SLG Login Service";
    options.default_config = "config/login/login.json";

    app::Application application(options);

    application.SetInitializeHook([](app::Application& app_instance) {
        InitializeLogging(app_instance.Config());
        auto service_config = LoadServiceConfig(app_instance.Config());
        ApplyServiceContext(service_config);
        auto service_options = LoadLoginServiceOptions(app_instance.Config());
        if (auto snowflake = app_instance.GetSnowflakeConfig()) {
            service_options.snowflake.datacenter_id = snowflake->datacenter_id;
            service_options.snowflake.worker_id = snowflake->worker_id;
        }

        g_login_service = std::make_shared<login::LoginService>(
            app_instance.TcpContext().GetContext(), std::move(service_options));
        LogStartupInfo(service_config);

        RegisterTcpHandlers(app_instance, g_login_service);
        if (!app_instance.StartListeners()) {
            throw std::runtime_error("failed to start login listeners");
        }
        LOGIN_LOG_INFO("login tcp listeners started");
    });

    application.SetStopHook([](app::Application&) {
        const int signal = g_received_signal.load(std::memory_order_relaxed);
        if (signal != 0) {
            LOGIN_LOG_WARN("received signal {}, preparing to stop", signal);
        } else {
            LOGIN_LOG_INFO("stop requested");
        }
    });

    application.SetShutdownHook([](app::Application&) {
        g_login_service.reset();
        g_received_signal.store(0, std::memory_order_relaxed);
        LOGIN_LOG_INFO("login service shutdown complete");
    });

    auto* app_ptr = &application;
    auto stop_handler = [app_ptr](int signal_number) {
        g_received_signal.store(signal_number, std::memory_order_relaxed);
        app_ptr->Stop();
    };
    application.AddSignalHandler(SIGINT, stop_handler);
    application.AddSignalHandler(SIGTERM, stop_handler);

    int exit_code = application.Run(argc, argv);

    logging::LoggingManagerInstance().Shutdown();
    logging::LoggingManagerSingleton::Destroy();
    return exit_code;
}
