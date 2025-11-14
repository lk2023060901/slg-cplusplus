#include "application/application.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "json/json_reader.h"
#include "network/tcp/tcp_client.h"
#include "network/tcp/tcp_server.h"

namespace tcp = slg::network::tcp;
namespace json = slg::json;

namespace slg::application {

Application* Application::active_instance_ = nullptr;

Application::Application() : Application(Options{}) {}

Application::Application(Options options) : options_(std::move(options)) {
    tcp_context_ = std::make_unique<tcp::TcpIoContext>(options_.io_threads);
    shutdown_future_ = shutdown_promise_.get_future();
}

Application::~Application() {
    Stop();
}

int Application::Run(int argc, const char* argv[]) {
    if (!ParseCommandLine(argc, argv)) {
        return cli_exit_code_;
    }
    if (!LoadConfig()) {
        return 1;
    }

    tcp_context_->Start();
    active_instance_ = this;
    SetupSignalHandling();

    if (init_hook_) {
        init_hook_(*this);
    }

    WaitForShutdown();

    if (shutdown_hook_) {
        shutdown_hook_(*this);
    }

    tcp_context_->Stop();
    tcp_context_->Join();

    return 0;
}

void Application::Stop() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) {
        return;
    }

    if (stop_hook_) {
        stop_hook_(*this);
    }

    tcp_context_->Stop();

    if (!shutdown_signal_sent_) {
        shutdown_signal_sent_ = true;
        shutdown_promise_.set_value();
    }
}

void Application::SetInitializeHook(InitHook hook) {
    init_hook_ = std::move(hook);
}

void Application::SetShutdownHook(ShutdownHook hook) {
    shutdown_hook_ = std::move(hook);
}

void Application::SetStopHook(StopHook hook) {
    stop_hook_ = std::move(hook);
}

void Application::AddSignalHandler(int signal_number, SignalHandler handler) {
    signal_handlers_[signal_number].push_back(std::move(handler));
}

void Application::AddCliHook(CliHook hook) {
    cli_hooks_.push_back(std::move(hook));
}

void Application::AddConfigHook(ConfigHook hook) {
    config_hooks_.push_back(std::move(hook));
}

const json::JsonValue& Application::Config() const noexcept {
    return config_;
}

void Application::MergeConfig(const json::JsonValue& extra) {
    config_.Raw().merge_patch(extra.Raw());
}

tcp::TcpIoContext& Application::TcpContext() noexcept {
    return *tcp_context_;
}

std::shared_ptr<tcp::TcpServer> Application::CreateTcpServer(
    const boost::asio::ip::tcp::endpoint& endpoint,
    tcp::TcpServer::AcceptHandler on_accept,
    tcp::TcpConnection::ReceiveHandler on_receive,
    tcp::TcpConnection::ErrorHandler on_error,
    std::size_t read_buffer_size) {
    auto server = std::make_shared<tcp::TcpServer>(tcp_context_->GetContext(), endpoint);
    server->Start(std::move(on_accept), std::move(on_receive), std::move(on_error),
                  read_buffer_size);
    tcp_servers_.push_back(server);
    return server;
}

std::shared_ptr<tcp::TcpClient> Application::CreateTcpClient() {
    auto client = std::make_shared<tcp::TcpClient>(tcp_context_->GetContext());
    tcp_clients_.push_back(client);
    return client;
}

bool Application::ParseCommandLine(int argc, const char* argv[]) {
    cxxopts::Options cli(options_.name, options_.description);
    cli.add_options()
        ("c,config", "Path to configuration file",
         cxxopts::value<std::string>()->default_value(options_.default_config))
        ("io-threads", "Number of IO threads",
         cxxopts::value<std::size_t>()->default_value(std::to_string(options_.io_threads)))
        ("h,help", "Print help");

    for (auto& hook : cli_hooks_) {
        hook(cli);
    }

    cxxopts::ParseResult result;
    try {
        result = cli.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cout << cli.help() << std::endl;
        cli_requested_exit_ = true;
        cli_exit_code_ = 1;
        return false;
    }
    if (result.count("help") && result["help"].as<bool>()) {
        std::cout << cli.help() << std::endl;
        cli_requested_exit_ = true;
        cli_exit_code_ = 0;
        return false;
    }

    config_path_ = result["config"].as<std::string>();
    options_.io_threads = result["io-threads"].as<std::size_t>();
    if (options_.io_threads != tcp_context_->ThreadCount()) {
        tcp_context_ = std::make_unique<tcp::TcpIoContext>(options_.io_threads);
    }
    return true;
}

bool Application::LoadConfig() {
    config_ = json::JsonValue::Object();
    if (!config_path_.empty() && std::filesystem::exists(config_path_)) {
        json::JsonReader reader;
        auto parsed = reader.ParseFile(config_path_);
        if (!parsed.has_value()) {
            std::cerr << "Failed to parse config file: " << config_path_ << std::endl;
            return false;
        }
        if (parsed->IsObject()) {
            config_ = std::move(*parsed);
        } else {
            std::cerr << "Config file " << config_path_
                      << " does not contain a JSON object; using empty object\n";
        }
    }

    for (auto& hook : config_hooks_) {
        hook(config_);
    }

    config_loaded_ = true;
    return true;
}

void Application::SetupSignalHandling() {
    std::signal(SIGINT, SignalHandlerThunk);
    std::signal(SIGTERM, SignalHandlerThunk);
}

void Application::HandleSignal(int signal_number) {
    auto iter = signal_handlers_.find(signal_number);
    if (iter != signal_handlers_.end()) {
        for (auto& handler : iter->second) {
            handler(signal_number);
        }
    }

    Stop();
}

void Application::WaitForShutdown() {
    shutdown_future_.wait();
}

void Application::SignalHandlerThunk(int signal_number) {
    if (active_instance_) {
        active_instance_->HandleSignal(signal_number);
    }
}

}  // namespace slg::application
