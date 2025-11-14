#include <atomic>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "application/application.h"
#include "json/json_value.h"
#include "logging/logging_config.h"
#include "logging/logging_manager.h"
#include "login/logging_macros.h"
#include "login_build_info.h"

namespace app = slg::application;
namespace json = slg::json;
namespace logging = slg::logging;

namespace {

std::atomic<int> g_received_signal{0};

struct LoginConfig {
    std::string host{"0.0.0.0"};
    std::uint16_t port{57001};
    std::size_t max_connections{8192};
    std::size_t io_threads{0};
};

struct ServiceConfig {
    std::string name{"login-service"};
    int shard_id{0};
};

LoginConfig LoadLoginConfig(const json::JsonValue& root) {
    auto login_section = root.Get("login");
    if (!login_section.has_value() || !login_section->IsObject()) {
        throw std::runtime_error("Missing 'login' configuration section");
    }

    LoginConfig cfg;
    if (auto host = login_section->GetAs<std::string>("host")) {
        cfg.host = *host;
    }
    if (auto port = login_section->GetAs<std::uint16_t>("port")) {
        cfg.port = *port;
    }
    if (auto max_conn = login_section->GetAs<std::size_t>("max_connections")) {
        cfg.max_connections = *max_conn;
    }
    if (auto io_threads = login_section->GetAs<std::size_t>("io_threads")) {
        cfg.io_threads = *io_threads;
    }

    return cfg;
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

void LogStartupInfo(const LoginConfig& config, const ServiceConfig& service) {
    LOGIN_LOG_INFO("login configuration host={} port={} max_connections={} io_threads={}",
                   config.host, config.port, config.max_connections, config.io_threads);
    LOGIN_LOG_INFO("service context name={} shard_id={}", service.name, service.shard_id);
    LOGIN_LOG_INFO("build version={} timestamp={} git_hash={}", login::build_info::kVersion,
                   login::build_info::kTimestamp, login::build_info::kGitHash);
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
        auto config = LoadLoginConfig(app_instance.Config());
        LogStartupInfo(config, service_config);
        LOGIN_LOG_INFO("login service started");
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
