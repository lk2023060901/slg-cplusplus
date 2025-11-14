#include "logging/logging_manager.h"

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/hourly_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace slg::logging {
namespace {

void EnsureParentDirectory(const std::string& file_path) {
    const std::filesystem::path path(file_path);
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log directory: " + parent.string());
    }
}

uint16_t ToUint16(std::size_t value) {
    return static_cast<uint16_t>(std::min<std::size_t>(value, std::numeric_limits<uint16_t>::max()));
}

std::size_t EffectiveTimeRetention(const RotationPolicy& policy) {
    if (policy.max_files > 0) {
        return policy.max_files;
    }
    if (policy.retain_days > 0) {
        return policy.retain_days;
    }
    return 0;
}

}  // namespace

LoggingManager::LoggingManager() = default;
LoggingManager::~LoggingManager() { Shutdown(); }

void LoggingManager::LoadConfig(const LoggingConfig& config) {
    has_config_path_ = false;
    config_path_.clear();
    ApplyConfig(config);
}

void LoggingManager::LoadConfigFromFile(const std::string& file_path) {
    auto config = LoadLoggingConfigFromFile(file_path);
    config_path_ = file_path;
    has_config_path_ = true;
    ApplyConfig(config);
}

void LoggingManager::Reload() {
    if (!has_config_path_) {
        throw std::runtime_error("Reload called before LoadConfigFromFile");
    }

    ReloadFromFile(config_path_);
}

void LoggingManager::ReloadFromFile(const std::string& file_path) {
    auto config = LoadLoggingConfigFromFile(file_path);
    config_path_ = file_path;
    has_config_path_ = true;
    ApplyConfig(config);
}

std::shared_ptr<spdlog::logger> LoggingManager::GetLogger(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = loggers_.find(std::string(name));
    if (iter == loggers_.end()) {
        return nullptr;
    }
    return iter->second;
}

std::shared_ptr<spdlog::logger> LoggingManager::GetLogger(const std::string& name) const {
    return GetLogger(std::string_view{name});
}

bool LoggingManager::HasLogger(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loggers_.find(name) != loggers_.end();
}

void LoggingManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    DropAllLoggers();
    spdlog::shutdown();
}

void LoggingManager::ApplyConfig(const LoggingConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    DropAllLoggers();

    for (const auto& logger_config : config.loggers) {
        auto logger = CreateLogger(logger_config);
        spdlog::register_logger(logger);
        loggers_.emplace(logger_config.name, std::move(logger));
    }

    current_config_ = config;
}

std::shared_ptr<spdlog::logger> LoggingManager::CreateLogger(const LoggerConfig& config) {
    auto sinks = BuildSinks(config);

    std::shared_ptr<spdlog::logger> logger;
    if (config.async.enabled) {
        auto pool = std::make_shared<spdlog::details::thread_pool>(config.async.queue_size,
                                                                   config.async.thread_count);
        async_pools_.push_back(pool);
        logger = std::make_shared<spdlog::async_logger>(config.name, sinks.begin(), sinks.end(),
                                                        pool, config.async.overflow_policy);
    } else {
        logger = std::make_shared<spdlog::logger>(config.name, sinks.begin(), sinks.end());
    }

    logger->set_level(config.level);
    logger->set_pattern(config.pattern.empty() ? "%+" : config.pattern);
    logger->flush_on(config.level);
    return logger;
}

std::vector<spdlog::sink_ptr> LoggingManager::BuildSinks(const LoggerConfig& config) {
    std::vector<spdlog::sink_ptr> sinks;
    if (config.enable_console) {
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (!config.file_path.empty()) {
        EnsureParentDirectory(config.file_path);
        const auto& rotation = config.rotation;

        switch (rotation.type) {
            case RotationType::kDaily: {
                const auto retention = EffectiveTimeRetention(rotation);
                sinks.emplace_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                    config.file_path, rotation.hour, rotation.minute, rotation.truncate,
                    ToUint16(retention)));
                break;
            }
            case RotationType::kHourly: {
                const auto retention = EffectiveTimeRetention(rotation);
                sinks.emplace_back(std::make_shared<spdlog::sinks::hourly_file_sink_mt>(
                    config.file_path, rotation.truncate, ToUint16(retention)));
                break;
            }
            case RotationType::kSize: {
                const auto max_files = rotation.max_files > 0 ? rotation.max_files : 1;
                if (rotation.max_size_bytes == 0) {
                    throw std::runtime_error(
                        "Size-based rotation requires a non-zero max_size or max_size_mb");
                }
                sinks.emplace_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    config.file_path, rotation.max_size_bytes, max_files, rotation.truncate));
                break;
            }
            case RotationType::kNone:
            default:
                sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    config.file_path, rotation.truncate));
                break;
        }
    }

    if (sinks.empty()) {
        throw std::runtime_error("Logger " + config.name + " has no sinks configured");
    }

    return sinks;
}

void LoggingManager::DropAllLoggers() {
    for (auto& [name, logger] : loggers_) {
        if (logger) {
            logger->flush();
            spdlog::drop(name);
        }
    }
    loggers_.clear();
    async_pools_.clear();
}

LoggingManager& LoggingManagerInstance() {
    return LoggingManagerSingleton::Instance();
}

}  // namespace slg::logging
