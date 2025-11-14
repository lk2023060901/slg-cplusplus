#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "logging/logging_config.h"
#include "singleton/singleton.h"

namespace spdlog {
namespace details {
class thread_pool;
}
}  // namespace spdlog

namespace slg::logging {

class LoggingManager {
public:
    SLG_LOGGING_API LoggingManager();
    SLG_LOGGING_API ~LoggingManager();

    SLG_LOGGING_API void LoadConfig(const LoggingConfig& config);
    SLG_LOGGING_API void LoadConfigFromFile(const std::string& file_path);
    SLG_LOGGING_API void Reload();
    SLG_LOGGING_API void ReloadFromFile(const std::string& file_path);

    SLG_LOGGING_API std::shared_ptr<spdlog::logger> GetLogger(std::string_view name) const;
    SLG_LOGGING_API std::shared_ptr<spdlog::logger> GetLogger(const std::string& name) const;
    SLG_LOGGING_API bool HasLogger(const std::string& name) const;
    SLG_LOGGING_API void Shutdown();

private:
    void ApplyConfig(const LoggingConfig& config);
    std::shared_ptr<spdlog::logger> CreateLogger(const LoggerConfig& config);
    std::vector<spdlog::sink_ptr> BuildSinks(const LoggerConfig& config);
    void DropAllLoggers();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
    std::vector<std::shared_ptr<spdlog::details::thread_pool>> async_pools_;
    LoggingConfig current_config_;
    bool has_config_path_{false};
    std::string config_path_;
};

using LoggingManagerSingleton = singleton::Singleton<LoggingManager>;

SLG_LOGGING_API LoggingManager& LoggingManagerInstance();

#define SLG_LOGGING_LOG(level, logger_name, ...)                                             \
    do {                                                                                     \
        auto& _slg_logging_manager = ::slg::logging::LoggingManagerInstance();               \
        auto _slg_logger_ptr = _slg_logging_manager.GetLogger(logger_name);                  \
        if (_slg_logger_ptr) {                                                               \
            _slg_logger_ptr->log(level, __VA_ARGS__);                                        \
        }                                                                                    \
    } while (0)

#define LOG_TRACE(logger_name, ...) SLG_LOGGING_LOG(spdlog::level::trace, logger_name, __VA_ARGS__)
#define LOG_DEBUG(logger_name, ...) SLG_LOGGING_LOG(spdlog::level::debug, logger_name, __VA_ARGS__)
#define LOG_INFO(logger_name, ...) SLG_LOGGING_LOG(spdlog::level::info, logger_name, __VA_ARGS__)
#define LOG_WARN(logger_name, ...) SLG_LOGGING_LOG(spdlog::level::warn, logger_name, __VA_ARGS__)
#define LOG_ERROR(logger_name, ...) SLG_LOGGING_LOG(spdlog::level::err, logger_name, __VA_ARGS__)
#define LOG_CRITICAL(logger_name, ...)                                                        \
    SLG_LOGGING_LOG(spdlog::level::critical, logger_name, __VA_ARGS__)

}  // namespace slg::logging
