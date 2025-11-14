#include "logging/logging_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace slg::logging {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

spdlog::level::level_enum ParseLevel(const std::string& value) {
    const auto lowered = ToLower(value);
    auto level = spdlog::level::from_str(lowered);
    if (level == spdlog::level::n_levels) {
        throw std::runtime_error("Unknown log level: " + value);
    }
    return level;
}

spdlog::async_overflow_policy ParseOverflowPolicy(const std::string& value) {
    const auto lowered = ToLower(value);
    if (lowered == "block") {
        return spdlog::async_overflow_policy::block;
    }
    if (lowered == "overrun_oldest") {
        return spdlog::async_overflow_policy::overrun_oldest;
    }

    throw std::runtime_error("Unsupported async overflow policy: " + value);
}

RotationType ParseRotationType(const std::string& value) {
    const auto lowered = ToLower(value);
    if (lowered == "daily") {
        return RotationType::kDaily;
    }
    if (lowered == "hourly") {
        return RotationType::kHourly;
    }
    if (lowered == "size") {
        return RotationType::kSize;
    }
    if (lowered == "none" || lowered.empty()) {
        return RotationType::kNone;
    }

    throw std::runtime_error("Unsupported rotation type: " + value);
}

std::size_t ReadSizeField(const nlohmann::json& node,
                          const std::string& field_name,
                          std::size_t default_value) {
    if (!node.contains(field_name)) {
        return default_value;
    }

    const auto& field = node.at(field_name);
    if (!field.is_number()) {
        throw std::runtime_error("Expected numeric field: " + field_name);
    }

    return field.get<std::size_t>();
}

RotationPolicy ParseRotationPolicy(const nlohmann::json& node) {
    if (!node.is_object()) {
        return {};
    }

    RotationPolicy policy;
    policy.type = ParseRotationType(node.value("type", "none"));
    policy.truncate = node.value("truncate", false);
    policy.hour = node.value("hour", 0);
    policy.minute = node.value("minute", 0);
    policy.max_files = ReadSizeField(node, "max_files", 0);
    policy.retain_days = ReadSizeField(node, "retain_days", 0);

    if (policy.type == RotationType::kSize) {
        policy.max_size_bytes = ReadSizeField(node, "max_size", 0);
        if (policy.max_size_bytes == 0) {
            const auto megabytes = ReadSizeField(node, "max_size_mb", 0);
            policy.max_size_bytes = megabytes * 1024 * 1024;
        }
        if (policy.max_size_bytes == 0) {
            throw std::runtime_error("size rotation requires max_size/max_size_mb");
        }
    }

    return policy;
}

AsyncPolicy ParseAsyncPolicy(const nlohmann::json& node) {
    if (!node.is_object()) {
        return {};
    }

    AsyncPolicy policy;
    policy.enabled = node.value("enabled", true);
    policy.queue_size = ReadSizeField(node, "queue_size", policy.queue_size);
    policy.thread_count = ReadSizeField(node, "thread_count", policy.thread_count);

    if (node.contains("overflow_policy")) {
        policy.overflow_policy = ParseOverflowPolicy(node.at("overflow_policy").get<std::string>());
    }

    return policy;
}

LoggerConfig ParseLoggerConfig(const nlohmann::json& node) {
    if (!node.is_object()) {
        throw std::runtime_error("Each logger entry must be an object");
    }

    LoggerConfig config;
    config.name = node.value("name", "");
    if (config.name.empty()) {
        throw std::runtime_error("Logger name is required");
    }

    config.file_path = node.value("file", std::string{});
    config.enable_console = node.value("console", false);
    if (config.file_path.empty() && !config.enable_console) {
        throw std::runtime_error("Logger " + config.name +
                                 " must enable console output or provide a file path");
    }

    if (node.contains("pattern")) {
        config.pattern = node.at("pattern").get<std::string>();
    }

    if (node.contains("level")) {
        config.level = ParseLevel(node.at("level").get<std::string>());
    }

    if (!config.file_path.empty() && node.contains("rotation")) {
        config.rotation = ParseRotationPolicy(node.at("rotation"));
    }

    if (node.contains("async")) {
        config.async = ParseAsyncPolicy(node.at("async"));
    }

    return config;
}

}  // namespace

LoggingConfig LoadLoggingConfigFromJson(const nlohmann::json& json) {
    if (!json.contains("loggers") || !json.at("loggers").is_array()) {
        throw std::runtime_error("Logging config must contain a 'loggers' array");
    }

    LoggingConfig config;
    for (const auto& logger_node : json.at("loggers")) {
        config.loggers.emplace_back(ParseLoggerConfig(logger_node));
    }

    if (config.loggers.empty()) {
        throw std::runtime_error("Logging config must declare at least one logger");
    }

    return config;
}

LoggingConfig LoadLoggingConfigFromFile(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open logging config: " + file_path);
    }

    nlohmann::json json;
    input >> json;
    return LoadLoggingConfigFromJson(json);
}

}  // namespace slg::logging
