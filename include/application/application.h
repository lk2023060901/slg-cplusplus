#pragma once

#include <atomic>
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
#include <nlohmann/json.hpp>

#include <cxxopts.hpp>

#include "application/application_export.h"
#include "application/dependency_container.h"
#include "network/tcp/tcp_client.h"
#include "network/tcp/tcp_io_context.h"
#include "network/tcp/tcp_server.h"

namespace tcp = slg::network::tcp;

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

    using InitHook = std::function<void(Application&)>;
    using ShutdownHook = std::function<void(Application&)>;
    using SignalHandler = std::function<void(int)>;
    using CliHook = std::function<void(cxxopts::Options&)>;
    using ConfigHook = std::function<void(nlohmann::json&)>;

    SLG_APPLICATION_API Application();
    SLG_APPLICATION_API explicit Application(Options options);
    SLG_APPLICATION_API ~Application();

    SLG_APPLICATION_API int Run(int argc, const char* argv[]);
    SLG_APPLICATION_API void Stop();

    SLG_APPLICATION_API void SetInitializeHook(InitHook hook);
    SLG_APPLICATION_API void SetShutdownHook(ShutdownHook hook);
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

    SLG_APPLICATION_API const nlohmann::json& Config() const noexcept;
    SLG_APPLICATION_API void MergeConfig(const nlohmann::json& extra);

    SLG_APPLICATION_API tcp::TcpIoContext& TcpContext() noexcept;

    SLG_APPLICATION_API std::shared_ptr<tcp::TcpServer> CreateTcpServer(
        const boost::asio::ip::tcp::endpoint& endpoint,
        tcp::TcpServer::AcceptHandler on_accept,
        tcp::TcpConnection::ReceiveHandler on_receive,
        tcp::TcpConnection::ErrorHandler on_error,
        std::size_t read_buffer_size = tcp::TcpServer::kDefaultReadBufferSize);

    SLG_APPLICATION_API std::shared_ptr<tcp::TcpClient> CreateTcpClient();

private:
    void ParseCommandLine(int argc, const char* argv[]);
    void LoadConfig();
    void SetupSignalHandling();
    void HandleSignal(int signal_number);
    void WaitForShutdown();

    static void SignalHandlerThunk(int signal_number);

    Options options_;
    DependencyContainer dependencies_;
    std::unique_ptr<tcp::TcpIoContext> tcp_context_;
    std::vector<std::shared_ptr<tcp::TcpServer>> tcp_servers_;
    std::vector<std::shared_ptr<tcp::TcpClient>> tcp_clients_;

    std::vector<CliHook> cli_hooks_;
    std::vector<ConfigHook> config_hooks_;
    std::unordered_map<int, std::vector<SignalHandler>> signal_handlers_;

    nlohmann::json config_;
    std::string config_path_;
    bool config_loaded_{false};

    InitHook init_hook_;
    ShutdownHook shutdown_hook_;

    std::promise<void> shutdown_promise_;
    std::future<void> shutdown_future_;
    std::atomic<bool> stopping_{false};
    bool shutdown_signal_sent_{false};

    static Application* active_instance_;
};

}  // namespace slg::application
