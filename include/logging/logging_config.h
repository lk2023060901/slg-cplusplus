#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#include "logging/logging_export.h"

namespace slg::logging {

enum class RotationType {
    kNone,
    kDaily,
    kHourly,
    kSize
};

struct RotationPolicy {
    RotationType type{RotationType::kNone};
    bool truncate{false};
    int hour{0};
    int minute{0};
    std::size_t max_files{0};
    std::size_t retain_days{0};
    std::size_t max_size_bytes{0};
};

struct AsyncPolicy {
    bool enabled{false};
    std::size_t queue_size{8192};
    std::size_t thread_count{1};
    spdlog::async_overflow_policy overflow_policy{spdlog::async_overflow_policy::block};
};

struct LoggerConfig {
    std::string name;
    std::string file_path;
    std::string pattern{"%+"};
    spdlog::level::level_enum level{spdlog::level::info};
    RotationPolicy rotation;
    AsyncPolicy async;
};

struct LoggingConfig {
    std::vector<LoggerConfig> loggers;
};

SLG_LOGGING_API LoggingConfig LoadLoggingConfigFromFile(const std::string& file_path);

}  // namespace slg::logging
