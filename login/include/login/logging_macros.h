#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

#include "logging/logging_manager.h"

namespace login::logging {

inline constexpr std::string_view kLoginLoggerName{"login"};

struct ServiceContext {
    std::string name{"login-service"};
    int shard_id{0};
};

inline ServiceContext& MutableServiceContext() {
    static ServiceContext context{};
    return context;
}

inline void SetServiceContext(std::string name, int shard_id) {
    auto& context = MutableServiceContext();
    context.name = std::move(name);
    context.shard_id = shard_id;
}

inline const ServiceContext& GetServiceContext() {
    return MutableServiceContext();
}

}  // namespace login::logging

#define LOGIN_LOG_WITH_LEVEL(level, fmt, ...)                                              \
    SLG_LOGGING_LOG(level, ::login::logging::kLoginLoggerName,                           \
                    "[{}][shard:{}] " fmt,                                              \
                    ::login::logging::GetServiceContext().name,                           \
                    ::login::logging::GetServiceContext().shard_id, ##__VA_ARGS__)

#define LOGIN_LOG_TRACE(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::trace, fmt, ##__VA_ARGS__)
#define LOGIN_LOG_DEBUG(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::debug, fmt, ##__VA_ARGS__)
#define LOGIN_LOG_INFO(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::info, fmt, ##__VA_ARGS__)
#define LOGIN_LOG_WARN(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::warn, fmt, ##__VA_ARGS__)
#define LOGIN_LOG_ERROR(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::err, fmt, ##__VA_ARGS__)
#define LOGIN_LOG_CRITICAL(fmt, ...) LOGIN_LOG_WITH_LEVEL(spdlog::level::critical, fmt, ##__VA_ARGS__)
