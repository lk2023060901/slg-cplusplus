#include "application/application.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <boost/asio/ip/address.hpp>

#include "json/json_reader.h"
#include "network/tcp/tcp_client.h"
#include "network/tcp/tcp_server.h"
#include "compressor/compression_processor.h"
#include "crypto/aes_crypto_processor.h"
#include "crypto/crypto_processor.h"
#include "application/protocol/security_context.h"

namespace tcp = slg::network::tcp;
namespace json = slg::json;

namespace slg::application {

Application* Application::active_instance_ = nullptr;

Application::Application() : Application(Options{}) {}

Application::Application(Options options) : options_(std::move(options)) {
    tcp_context_ = std::make_unique<tcp::TcpIoContext>(options_.io_threads);
    shutdown_future_ = shutdown_promise_.get_future();
    RegisterCryptoFactory("none", [](const std::string&, const std::string&) {
        return std::make_shared<protocol::NullCryptoProcessor>();
    });
    RegisterCryptoFactory("aes128", [](const std::string& key, const std::string& iv) {
        return std::make_shared<protocol::Aes128CtrCryptoProcessor>(key, iv);
    });
    RegisterCompressionFactory("none", []() {
        return std::make_shared<protocol::NullCompressionProcessor>();
    });
    RegisterCompressionFactory("lz4", []() {
        return std::make_shared<protocol::Lz4CompressionProcessor>();
    });
    RegisterCompressionFactory("zstd", []() {
        return std::make_shared<protocol::ZstdCompressionProcessor>();
    });
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

    StopConnectors();
    StopListeners();
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

std::optional<Application::SnowflakeConfig> Application::GetSnowflakeConfig() const {
    auto section = config_.Get("snowflake");
    if (!section.has_value() || !section->IsObject()) {
        return std::nullopt;
    }

    SnowflakeConfig cfg;
    if (auto datacenter = section->GetAs<std::uint16_t>("datacenter_id")) {
        cfg.datacenter_id = *datacenter;
    }
    if (auto worker = section->GetAs<std::uint16_t>("worker_id")) {
        cfg.worker_id = *worker;
    }
    return cfg;
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

void Application::RegisterListenerHandler(std::string handler_name,
                                          tcp::TcpServer::AcceptHandler on_accept,
                                          tcp::TcpConnection::ReceiveHandler on_receive,
                                          tcp::TcpConnection::ErrorHandler on_error,
                                          ListenerStartCallback on_started,
                                          ListenerFailureCallback on_failed) {
    listener_handlers_[std::move(handler_name)] =
        ListenerHandler{std::move(on_accept), std::move(on_receive), std::move(on_error),
                        std::move(on_started), std::move(on_failed)};
}

void Application::RegisterConnectorHandler(std::string handler_name,
                                           tcp::TcpClient::ConnectHandler on_connect,
                                           tcp::TcpConnection::ReceiveHandler on_receive,
                                           tcp::TcpConnection::ErrorHandler on_error,
                                           ConnectorStartCallback on_started,
                                           ConnectorFailureCallback on_failed) {
    connector_handlers_[std::move(handler_name)] =
        ConnectorHandler{std::move(on_connect), std::move(on_receive), std::move(on_error),
                         std::move(on_started), std::move(on_failed)};
}

bool Application::StartListeners() {
    const auto configs = ParseListenerConfigs();
    bool ok = true;
    for (const auto& config : configs) {
        auto handler_it = listener_handlers_.find(config.handler);
        if (handler_it == listener_handlers_.end()) {
            NotifyListenerFailed(config,
                                 "handler '" + config.handler + "' is not registered",
                                 nullptr);
            ok = false;
            continue;
        }
        std::string error_message;
        if (StartSingleListener(config, handler_it->second, error_message)) {
            NotifyListenerStarted(config, &handler_it->second);
        } else {
            NotifyListenerFailed(config, error_message, &handler_it->second);
            ok = false;
        }
    }
    return ok;
}

bool Application::StartSingleListener(const ListenerConfig& config,
                                      const ListenerHandler& handler,
                                      std::string& error_message) {
    if (config.type != "tcp") {
        error_message = "unsupported listener type '" + config.type + "'";
        return false;
    }

    boost::system::error_code ec;
    auto address =
        boost::asio::ip::make_address(config.host.empty() ? std::string("0.0.0.0") : config.host, ec);
    if (ec) {
        error_message = "invalid listener host '" + config.host + "': " + ec.message();
        return false;
    }

    try {
        const std::string listener_identifier = config.name.empty() ? config.handler : config.name;
        auto server = std::make_shared<tcp::TcpServer>(
            tcp_context_->GetContext(), boost::asio::ip::tcp::endpoint(address, config.port));
        auto on_accept = handler.on_accept;
        auto on_receive = handler.on_receive;
        auto on_error = handler.on_error;
        auto listener_name = listener_identifier;
        server->Start(
            [this, on_accept, listener_name](const tcp::TcpConnectionPtr& conn) {
                const auto id = next_connection_id_.fetch_add(1, std::memory_order_relaxed);
                conn->SetConnectionId(id);
                conn->SetListenerName(listener_name);
                TrackListenerConnection(listener_name, conn);
                if (on_accept) {
                    on_accept(conn);
                }
            },
            [on_receive](const tcp::TcpConnectionPtr& conn, const std::uint8_t* data, std::size_t size) {
                if (on_receive) {
                    on_receive(conn, data, size);
                }
            },
            [this, on_error, listener_name](const tcp::TcpConnectionPtr& conn,
                                            const boost::system::error_code& ec) {
                RemoveListenerConnection(listener_name, conn);
                if (on_error) {
                    on_error(conn, ec);
                }
            },
            config.read_buffer_size);
        managed_listeners_.push_back(server);
        return true;
    } catch (const std::exception& ex) {
        error_message = ex.what();
        return false;
    }
}

void Application::StopListeners() {
    for (auto& listener : managed_listeners_) {
        if (listener) {
            listener->Stop();
        }
    }
    managed_listeners_.clear();
}

std::vector<Application::ListenerConfig> Application::ParseListenerConfigs() const {
    std::vector<ListenerConfig> listeners;
    auto listeners_section = config_.Get("listeners");
    if (!listeners_section.has_value() || !listeners_section->IsArray()) {
        return listeners;
    }

    const auto size = listeners_section->Raw().size();
    listeners.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        auto entry = listeners_section->Get(i);
        if (!entry.has_value() || !entry->IsObject()) {
            continue;
        }
        ListenerConfig cfg;
        cfg.name = entry->GetAs<std::string>("name").value_or("");
        cfg.host = entry->GetAs<std::string>("host").value_or("0.0.0.0");
        cfg.port = entry->GetAs<std::uint16_t>("port").value_or(0);
        cfg.max_connections = entry->GetAs<std::size_t>("max_connections").value_or(0);
        cfg.io_threads = entry->GetAs<std::size_t>("io_threads").value_or(0);
        cfg.read_buffer_size = entry->GetAs<std::size_t>("read_buffer_size")
                                  .value_or(tcp::TcpServer::kDefaultReadBufferSize);
        cfg.type = entry->GetAs<std::string>("type").value_or("tcp");
        cfg.handler = entry->GetAs<std::string>("handler").value_or("");
        cfg.crypto_handler = global_crypto_handler_;
        cfg.crypto_key = global_crypto_key_;
        cfg.crypto_iv = global_crypto_iv_;
        cfg.compression_handler = global_compression_handler_;
        cfg.compression_min_bytes = global_compression_min_bytes_;
        if (auto crypto = entry->Get("crypto")) {
            if (crypto->IsObject()) {
                if (auto handler = crypto->GetAs<std::string>("handler")) {
                    cfg.crypto_handler = *handler;
                }
                if (auto key = crypto->GetAs<std::string>("key")) {
                    cfg.crypto_key = *key;
                }
                if (auto iv = crypto->GetAs<std::string>("iv")) {
                    cfg.crypto_iv = *iv;
                }
            }
        }
        if (auto compression = entry->Get("compression")) {
            if (compression->IsObject()) {
                if (auto handler = compression->GetAs<std::string>("handler")) {
                    cfg.compression_handler = *handler;
                }
                if (auto min_bytes = compression->GetAs<std::size_t>("min_bytes")) {
                    cfg.compression_min_bytes = *min_bytes;
                }
            }
        }
        if (cfg.port == 0 || cfg.handler.empty()) {
            continue;
        }
        listeners.push_back(std::move(cfg));
    }
    return listeners;
}

bool Application::StartConnectors() {
    const auto configs = ParseConnectorConfigs();
    bool ok = true;
    for (const auto& config : configs) {
        auto handler_it = connector_handlers_.find(config.handler);
        if (handler_it == connector_handlers_.end()) {
            NotifyConnectorFailed(config,
                                   "handler '" + config.handler + "' is not registered",
                                   nullptr);
            ok = false;
            continue;
        }
        if (config.type != "tcp") {
            NotifyConnectorFailed(config, "unsupported connector type '" + config.type + "'",
                                   &handler_it->second);
            ok = false;
            continue;
        }
        auto runtime = std::make_shared<ConnectorRuntime>();
        runtime->config = config;
        runtime->handler = handler_it->second;
        connector_runtimes_.push_back(runtime);
        NotifyConnectorStarted(config, &handler_it->second);
        StartConnectorAttempt(runtime);
    }
    return ok;
}

void Application::StartConnectorAttempt(const std::shared_ptr<ConnectorRuntime>& runtime) {
    if (!runtime || runtime->stopped) {
        return;
    }
    if (runtime->config.type != "tcp") {
        NotifyConnectorFailed(runtime->config,
                               "unsupported connector type '" + runtime->config.type + "'",
                               &runtime->handler);
        return;
    }

    runtime->active_client = std::make_shared<tcp::TcpClient>(tcp_context_->GetContext());
    auto weak_runtime = std::weak_ptr<ConnectorRuntime>(runtime);

    runtime->active_client->AsyncConnect(
        runtime->config.host, runtime->config.port,
        [weak_runtime](const tcp::TcpConnectionPtr& connection) {
            if (auto locked = weak_runtime.lock()) {
                locked->attempt = 0;
                if (locked->handler.on_connect) {
                    locked->handler.on_connect(connection);
                }
            }
        },
        runtime->handler.on_receive,
        [this, weak_runtime](const tcp::TcpConnectionPtr& connection,
                             const boost::system::error_code& ec) {
            auto runtime = weak_runtime.lock();
            if (!runtime || runtime->stopped) {
                return;
            }
            if (runtime->handler.on_error) {
                runtime->handler.on_error(connection, ec);
            }
            runtime->attempt += 1;
            const auto delay = ComputeConnectorDelay(runtime->config.reconnect, runtime->attempt);
            ScheduleConnectorRetry(runtime, delay);
        });
}

void Application::ScheduleConnectorRetry(const std::shared_ptr<ConnectorRuntime>& runtime,
                                         std::chrono::milliseconds delay) {
    if (!runtime || runtime->stopped) {
        return;
    }

    if (delay.count() == 0) {
        StartConnectorAttempt(runtime);
        return;
    }

    if (!runtime->retry_timer) {
        runtime->retry_timer =
            std::make_shared<boost::asio::steady_timer>(tcp_context_->GetContext());
    }
    runtime->retry_timer->cancel();
    runtime->retry_timer->expires_after(delay);
    auto weak_runtime = std::weak_ptr<ConnectorRuntime>(runtime);
    runtime->retry_timer->async_wait([this, weak_runtime](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        if (auto runtime = weak_runtime.lock()) {
            StartConnectorAttempt(runtime);
        }
    });
}

std::chrono::milliseconds Application::ComputeConnectorDelay(
    const ConnectorConfig::ReconnectPolicy& policy,
    std::size_t attempt) const {
    if (attempt == 0) {
        return std::chrono::milliseconds(0);
    }
    double interval = static_cast<double>(policy.interval_ms);
    if (policy.backoff_multiplier > 1.0) {
        interval *= std::pow(policy.backoff_multiplier, static_cast<double>(attempt - 1));
    }
    if (policy.backoff_multiplier <= 1.0) {
        interval = policy.interval_ms;
    }
    if (policy.max_interval_ms > 0) {
        interval = std::min(interval, static_cast<double>(policy.max_interval_ms));
    }
    if (interval < 0.0) {
        interval = static_cast<double>(policy.interval_ms);
    }
    return std::chrono::milliseconds(static_cast<long long>(interval));
}

std::vector<Application::ConnectorConfig> Application::ParseConnectorConfigs() const {
    std::vector<ConnectorConfig> connectors;
    auto section = config_.Get("connectors");
    if (!section.has_value() || !section->IsArray()) {
        return connectors;
    }

    const auto size = section->Raw().size();
    connectors.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        auto entry = section->Get(i);
        if (!entry.has_value() || !entry->IsObject()) {
            continue;
        }
        ConnectorConfig cfg;
        cfg.name = entry->GetAs<std::string>("name").value_or("");
        cfg.host = entry->GetAs<std::string>("host").value_or("127.0.0.1");
        cfg.port = entry->GetAs<std::uint16_t>("port").value_or(0);
        cfg.type = entry->GetAs<std::string>("type").value_or("tcp");
        cfg.handler = entry->GetAs<std::string>("handler").value_or("");
        cfg.crypto_handler = global_crypto_handler_;
        cfg.crypto_key = global_crypto_key_;
        cfg.crypto_iv = global_crypto_iv_;
        cfg.compression_handler = global_compression_handler_;
        cfg.compression_min_bytes = global_compression_min_bytes_;
        if (auto crypto = entry->Get("crypto")) {
            if (crypto->IsObject()) {
                if (auto handler = crypto->GetAs<std::string>("handler")) {
                    cfg.crypto_handler = *handler;
                }
                if (auto key = crypto->GetAs<std::string>("key")) {
                    cfg.crypto_key = *key;
                }
                if (auto iv = crypto->GetAs<std::string>("iv")) {
                    cfg.crypto_iv = *iv;
                }
            }
        }
        if (auto compression = entry->Get("compression")) {
            if (compression->IsObject()) {
                if (auto handler = compression->GetAs<std::string>("handler")) {
                    cfg.compression_handler = *handler;
                }
                if (auto min_bytes = compression->GetAs<std::size_t>("min_bytes")) {
                    cfg.compression_min_bytes = *min_bytes;
                }
            }
        }
        if (auto reconnect_section = entry->Get("reconnect")) {
            cfg.reconnect.interval_ms = reconnect_section->GetAs<std::uint32_t>("interval_ms").value_or(1000);
            cfg.reconnect.max_interval_ms =
                reconnect_section->GetAs<std::uint32_t>("max_interval_ms").value_or(10000);
            cfg.reconnect.backoff_multiplier =
                reconnect_section->GetAs<double>("backoff_multiplier").value_or(1.0);
            if (cfg.reconnect.backoff_multiplier < 1.0) {
                cfg.reconnect.backoff_multiplier = 1.0;
            }
        }
        if (cfg.port == 0 || cfg.handler.empty()) {
            continue;
        }
        connectors.push_back(std::move(cfg));
    }
    return connectors;
}

void Application::StopConnectors() {
    for (auto& runtime : connector_runtimes_) {
        if (!runtime) {
            continue;
        }
        runtime->stopped = true;
        if (runtime->retry_timer) {
            runtime->retry_timer->cancel();
        }
        if (runtime->active_client) {
            runtime->active_client->Cancel();
        }
    }
    connector_runtimes_.clear();
}

void Application::NotifyListenerStarted(const ListenerConfig& config,
                                        const ListenerHandler* handler) {
    if (handler && handler->on_started) {
        handler->on_started(config);
        return;
    }
    std::cout << "[application] listener '" << config.name << "' started on " << config.host << ':'
              << config.port << std::endl;
}

void Application::NotifyListenerFailed(const ListenerConfig& config,
                                       const std::string& reason,
                                       const ListenerHandler* handler) {
    if (handler && handler->on_failed) {
        handler->on_failed(config, reason);
        return;
    }
    std::cerr << "[application] listener '" << config.name << "' failed to start: " << reason
              << std::endl;
    Stop();
}

void Application::NotifyConnectorStarted(const ConnectorConfig& config,
                                         const ConnectorHandler* handler) {
    if (handler && handler->on_started) {
        handler->on_started(config);
        return;
    }
    std::cout << "[application] connector '" << config.name << "' scheduled for host " << config.host
              << ':' << config.port << std::endl;
}

void Application::NotifyConnectorFailed(const ConnectorConfig& config,
                                        const std::string& reason,
                                        const ConnectorHandler* handler) {
    if (handler && handler->on_failed) {
        handler->on_failed(config, reason);
        return;
    }
    std::cerr << "[application] connector '" << config.name << "' failed to start: " << reason
              << std::endl;
}

std::shared_ptr<protocol::SecurityContext> Application::CreateSecurityContext(
    const std::string& crypto,
    const std::string& crypto_key,
    const std::string& crypto_iv,
    const std::string& compression,
    std::size_t compression_min_bytes) const {
    auto crypto_processor = CreateCryptoProcessor(crypto, crypto_key, crypto_iv);
    auto compression_processor = CreateCompressionProcessor(compression);
    return std::make_shared<protocol::SecurityContext>(std::move(crypto_processor),
                                                       std::move(compression_processor),
                                                       compression_min_bytes);
}

std::shared_ptr<protocol::CryptoProcessor> Application::CreateCryptoProcessor(
    const std::string& handler,
    const std::string& key,
    const std::string& iv) const {
    auto iter = crypto_factories_.find(handler);
    if (iter == crypto_factories_.end()) {
        auto fallback = crypto_factories_.find("none");
        if (fallback == crypto_factories_.end()) {
            return std::make_shared<protocol::NullCryptoProcessor>();
        }
        return fallback->second("", "");
    }
    return iter->second(key, iv);
}

std::shared_ptr<protocol::CompressionProcessor> Application::CreateCompressionProcessor(
    const std::string& handler) const {
    auto iter = compression_factories_.find(handler);
    if (iter == compression_factories_.end()) {
        auto fallback = compression_factories_.find("none");
        if (fallback == compression_factories_.end()) {
            return std::make_shared<protocol::NullCompressionProcessor>();
        }
        return fallback->second();
    }
    return iter->second();
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

    if (auto crypto = config_.Get("crypto")) {
        if (crypto->IsObject()) {
            if (auto handler = crypto->GetAs<std::string>("handler")) {
                global_crypto_handler_ = *handler;
            }
            if (auto key = crypto->GetAs<std::string>("key")) {
                global_crypto_key_ = *key;
            }
            if (auto iv = crypto->GetAs<std::string>("iv")) {
                global_crypto_iv_ = *iv;
            }
        }
    }
    if (auto compression = config_.Get("compression")) {
        if (compression->IsObject()) {
            if (auto handler = compression->GetAs<std::string>("handler")) {
                global_compression_handler_ = *handler;
            }
            if (auto min_bytes = compression->GetAs<std::size_t>("min_bytes")) {
                global_compression_min_bytes_ = *min_bytes;
            }
        }
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

void Application::RegisterCryptoFactory(std::string name, CryptoFactory factory) {
    crypto_factories_[std::move(name)] = std::move(factory);
}

void Application::RegisterCompressionFactory(std::string name, CompressionFactory factory) {
    compression_factories_[std::move(name)] = std::move(factory);
}

std::shared_ptr<protocol::SecurityContext> Application::CreateListenerSecurityContext(
    std::string_view handler_name) const {
    auto configs = ParseListenerConfigs();
    std::string crypto = global_crypto_handler_;
    std::string crypto_key = global_crypto_key_;
    std::string crypto_iv = global_crypto_iv_;
    std::string compression = global_compression_handler_;
    std::size_t compression_min_bytes = global_compression_min_bytes_;
    for (const auto& cfg : configs) {
        if (cfg.handler == handler_name) {
            if (!cfg.crypto_handler.empty()) {
                crypto = cfg.crypto_handler;
            }
            if (!cfg.crypto_key.empty()) {
                crypto_key = cfg.crypto_key;
            }
            if (!cfg.crypto_iv.empty()) {
                crypto_iv = cfg.crypto_iv;
            }
            if (!cfg.compression_handler.empty()) {
                compression = cfg.compression_handler;
            }
            compression_min_bytes = cfg.compression_min_bytes;
            break;
        }
    }
    return CreateSecurityContext(crypto, crypto_key, crypto_iv, compression, compression_min_bytes);
}

std::shared_ptr<protocol::SecurityContext> Application::CreateConnectorSecurityContext(
    std::string_view handler_name) const {
    auto configs = ParseConnectorConfigs();
    std::string crypto = global_crypto_handler_;
    std::string crypto_key = global_crypto_key_;
    std::string crypto_iv = global_crypto_iv_;
    std::string compression = global_compression_handler_;
    std::size_t compression_min_bytes = global_compression_min_bytes_;
    for (const auto& cfg : configs) {
        if (cfg.handler == handler_name) {
            if (!cfg.crypto_handler.empty()) {
                crypto = cfg.crypto_handler;
            }
            if (!cfg.crypto_key.empty()) {
                crypto_key = cfg.crypto_key;
            }
            if (!cfg.crypto_iv.empty()) {
                crypto_iv = cfg.crypto_iv;
            }
            if (!cfg.compression_handler.empty()) {
                compression = cfg.compression_handler;
            }
            compression_min_bytes = cfg.compression_min_bytes;
            break;
        }
    }
    return CreateSecurityContext(crypto, crypto_key, crypto_iv, compression, compression_min_bytes);
}

std::vector<tcp::TcpConnectionPtr> Application::GetListenerConnections(
    std::string_view listener_name) const {
    std::vector<tcp::TcpConnectionPtr> result;
    std::lock_guard<std::mutex> lock(listener_connections_mutex_);
    auto iter = listener_connections_.find(std::string(listener_name));
    if (iter == listener_connections_.end()) {
        return result;
    }
    result.reserve(iter->second.connections.size());
    for (const auto& [id, conn] : iter->second.connections) {
        result.push_back(conn);
    }
    return result;
}

tcp::TcpConnectionPtr Application::GetListenerConnection(std::string_view listener_name,
                                                          std::uint64_t connection_id) const {
    std::lock_guard<std::mutex> lock(listener_connections_mutex_);
    auto iter = listener_connections_.find(std::string(listener_name));
    if (iter == listener_connections_.end()) {
        return nullptr;
    }
    auto conn_iter = iter->second.connections.find(connection_id);
    if (conn_iter == iter->second.connections.end()) {
        return nullptr;
    }
    return conn_iter->second;
}

bool Application::CloseListenerConnection(std::string_view listener_name,
                                          std::uint64_t connection_id) {
    auto connection = GetListenerConnection(listener_name, connection_id);
    if (!connection) {
        return false;
    }
    connection->Close();
    return true;
}

void Application::TrackListenerConnection(const std::string& listener_name,
                                          const tcp::TcpConnectionPtr& connection) {
    if (!connection) {
        return;
    }
    std::lock_guard<std::mutex> lock(listener_connections_mutex_);
    listener_connections_[listener_name].connections[connection->ConnectionId()] = connection;
}

void Application::RemoveListenerConnection(const std::string& listener_name,
                                           const tcp::TcpConnectionPtr& connection) {
    if (!connection) {
        return;
    }
    std::lock_guard<std::mutex> lock(listener_connections_mutex_);
    auto iter = listener_connections_.find(listener_name);
    if (iter == listener_connections_.end()) {
        return;
    }
    iter->second.connections.erase(connection->ConnectionId());
}

void Application::SignalHandlerThunk(int signal_number) {
    if (active_instance_) {
        active_instance_->HandleSignal(signal_number);
    }
}

}  // namespace slg::application
