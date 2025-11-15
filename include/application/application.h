#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cxxopts.hpp>

#include "application/application_export.h"
#include "application/dependency_container.h"
#include "json/json_value.h"
#include "network/tcp/tcp_client.h"
#include "network/tcp/tcp_io_context.h"
#include "network/tcp/tcp_server.h"

namespace tcp = slg::network::tcp;
namespace json = slg::json;

namespace slg::application {

class Application {
public:
    struct Options {
        std::string name{"slg-service"};
        std::string version{"0.1.0"};
        std::string default_config{"config/app.json"};
        std::size_t io_threads{0};
        std::string description{"SLG service"};
    };

    struct SnowflakeConfig {
        std::uint16_t datacenter_id{0};
        std::uint16_t worker_id{0};
    };

    using InitHook = std::function<void(Application&)>;
    using ShutdownHook = std::function<void(Application&)>;
    using StopHook = std::function<void(Application&)>;
    using SignalHandler = std::function<void(int)>;
    using CliHook = std::function<void(cxxopts::Options&)>;
    using ConfigHook = std::function<void(json::JsonValue&)>;

    SLG_APPLICATION_API Application();
    SLG_APPLICATION_API explicit Application(Options options);
    SLG_APPLICATION_API ~Application();

    SLG_APPLICATION_API int Run(int argc, const char* argv[]);
    SLG_APPLICATION_API void Stop();

    SLG_APPLICATION_API void SetInitializeHook(InitHook hook);
    SLG_APPLICATION_API void SetShutdownHook(ShutdownHook hook);
    SLG_APPLICATION_API void SetStopHook(StopHook hook);
    SLG_APPLICATION_API void AddSignalHandler(int signal_number, SignalHandler handler);
    SLG_APPLICATION_API void AddCliHook(CliHook hook);
    SLG_APPLICATION_API void AddConfigHook(ConfigHook hook);

    template <typename T>
    void RegisterDependency(std::shared_ptr<T> instance, std::string_view key = {}) {
        dependencies_.Register<T>(std::move(instance), key);
    }

    template <typename T>
    std::shared_ptr<T> ResolveDependency(std::string_view key = {}) const {
        return dependencies_.Resolve<T>(key);
    }

    SLG_APPLICATION_API const json::JsonValue& Config() const noexcept;

    template <typename T>
    std::optional<T> GetConfigSection(std::string_view key) const {
        if (!config_.IsObject()) {
            return std::nullopt;
        }
        auto section = config_.Get(key);
        if (!section.has_value()) {
            return std::nullopt;
        }
        return section->template As<T>();
    }
    SLG_APPLICATION_API void MergeConfig(const json::JsonValue& extra);

    SLG_APPLICATION_API std::optional<SnowflakeConfig> GetSnowflakeConfig() const;

    SLG_APPLICATION_API tcp::TcpIoContext& TcpContext() noexcept;

    SLG_APPLICATION_API std::shared_ptr<tcp::TcpServer> CreateTcpServer(
        const boost::asio::ip::tcp::endpoint& endpoint,
        tcp::TcpServer::AcceptHandler on_accept,
        tcp::TcpConnection::ReceiveHandler on_receive,
        tcp::TcpConnection::ErrorHandler on_error,
        std::size_t read_buffer_size = tcp::TcpServer::kDefaultReadBufferSize);

    SLG_APPLICATION_API std::shared_ptr<tcp::TcpClient> CreateTcpClient();

    struct ListenerConfig {
        std::string name;
        std::string host{"0.0.0.0"};
        std::uint16_t port{0};
        std::size_t max_connections{0};
        std::size_t io_threads{0};
        std::size_t read_buffer_size{tcp::TcpServer::kDefaultReadBufferSize};
        std::string type{"tcp"};
        std::string handler;
    };

    struct ConnectorConfig {
        struct ReconnectPolicy {
            std::uint32_t interval_ms{1000};
            std::uint32_t max_interval_ms{10000};
            double backoff_multiplier{1.0};
        };

        std::string name;
        std::string host{"127.0.0.1"};
        std::uint16_t port{0};
        std::string type{"tcp"};
        std::string handler;
        ReconnectPolicy reconnect;
    };

    using ListenerStartCallback = std::function<void(const ListenerConfig&)>;
    using ListenerFailureCallback = std::function<void(const ListenerConfig&, const std::string&)>;
    using ConnectorStartCallback = std::function<void(const ConnectorConfig&)>;
    using ConnectorFailureCallback = std::function<void(const ConnectorConfig&, const std::string&)>;

    SLG_APPLICATION_API void RegisterListenerHandler(
        std::string handler_name,
        tcp::TcpServer::AcceptHandler on_accept,
        tcp::TcpConnection::ReceiveHandler on_receive,
        tcp::TcpConnection::ErrorHandler on_error,
        ListenerStartCallback on_started = {},
        ListenerFailureCallback on_failed = {});

    SLG_APPLICATION_API void RegisterConnectorHandler(
        std::string handler_name,
        tcp::TcpClient::ConnectHandler on_connect,
        tcp::TcpConnection::ReceiveHandler on_receive,
        tcp::TcpConnection::ErrorHandler on_error,
        ConnectorStartCallback on_started = {},
        ConnectorFailureCallback on_failed = {});

    SLG_APPLICATION_API bool StartListeners();
    SLG_APPLICATION_API bool StartConnectors();
    SLG_APPLICATION_API void StopListeners();
    SLG_APPLICATION_API void StopConnectors();

private:
    bool ParseCommandLine(int argc, const char* argv[]);
    bool LoadConfig();
    void SetupSignalHandling();
    void HandleSignal(int signal_number);
    void WaitForShutdown();

    static void SignalHandlerThunk(int signal_number);

    struct ListenerHandler {
        tcp::TcpServer::AcceptHandler on_accept;
        tcp::TcpConnection::ReceiveHandler on_receive;
        tcp::TcpConnection::ErrorHandler on_error;
        ListenerStartCallback on_started;
        ListenerFailureCallback on_failed;
    };

    struct ConnectorHandler {
        tcp::TcpClient::ConnectHandler on_connect;
        tcp::TcpConnection::ReceiveHandler on_receive;
        tcp::TcpConnection::ErrorHandler on_error;
        ConnectorStartCallback on_started;
        ConnectorFailureCallback on_failed;
    };

    struct ConnectorRuntime {
        ConnectorConfig config;
        ConnectorHandler handler;
        std::size_t attempt{0};
        bool stopped{false};
        std::shared_ptr<boost::asio::steady_timer> retry_timer;
        std::shared_ptr<tcp::TcpClient> active_client;
    };

    std::vector<ListenerConfig> ParseListenerConfigs() const;
    std::vector<ConnectorConfig> ParseConnectorConfigs() const;
    bool StartSingleListener(const ListenerConfig& config,
                             const ListenerHandler& handler,
                             std::string& error_message);
    void StartConnectorAttempt(const std::shared_ptr<ConnectorRuntime>& runtime);
    void ScheduleConnectorRetry(const std::shared_ptr<ConnectorRuntime>& runtime,
                                std::chrono::milliseconds delay);
    std::chrono::milliseconds ComputeConnectorDelay(const ConnectorConfig::ReconnectPolicy& policy,
                                                    std::size_t attempt) const;
    void NotifyListenerStarted(const ListenerConfig& config, const ListenerHandler* handler);
    void NotifyListenerFailed(const ListenerConfig& config,
                              const std::string& reason,
                              const ListenerHandler* handler);
    void NotifyConnectorStarted(const ConnectorConfig& config, const ConnectorHandler* handler);
    void NotifyConnectorFailed(const ConnectorConfig& config,
                               const std::string& reason,
                               const ConnectorHandler* handler);

    Options options_;
    DependencyContainer dependencies_;
    std::unique_ptr<tcp::TcpIoContext> tcp_context_;
    std::vector<std::shared_ptr<tcp::TcpServer>> tcp_servers_;
    std::vector<std::shared_ptr<tcp::TcpClient>> tcp_clients_;
    std::vector<std::shared_ptr<tcp::TcpServer>> managed_listeners_;
    std::vector<std::shared_ptr<ConnectorRuntime>> connector_runtimes_;

    std::vector<CliHook> cli_hooks_;
    std::vector<ConfigHook> config_hooks_;
    std::unordered_map<int, std::vector<SignalHandler>> signal_handlers_;
    std::unordered_map<std::string, ListenerHandler> listener_handlers_;
    std::unordered_map<std::string, ConnectorHandler> connector_handlers_;

    json::JsonValue config_;
    std::string config_path_;
    bool config_loaded_{false};

    InitHook init_hook_;
    ShutdownHook shutdown_hook_;
    StopHook stop_hook_;

    std::promise<void> shutdown_promise_;
    std::future<void> shutdown_future_;
    std::atomic<bool> stopping_{false};
    bool shutdown_signal_sent_{false};
    bool cli_requested_exit_{false};
    int cli_exit_code_{0};

    static Application* active_instance_;
};

}  // namespace slg::application
