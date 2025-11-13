#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "database/redis/redis_export.h"
#include "json/json_value.h"

namespace slg::database::redis {

struct RedisEndpoint {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6379};
    int db{0};
    std::optional<std::string> password;
};

inline constexpr int kRedisDefaultTimeoutMs = 2000;

struct RedisConfig {
    bool cluster_mode{false};
    std::vector<RedisEndpoint> endpoints;
    std::size_t pool_size{4};
    std::chrono::milliseconds connect_timeout{std::chrono::milliseconds(kRedisDefaultTimeoutMs)};
    std::chrono::milliseconds socket_timeout{std::chrono::milliseconds(kRedisDefaultTimeoutMs)};
    std::optional<std::string> username;
    std::optional<std::string> password;

    SLG_REDIS_API static std::optional<RedisConfig> FromJson(const json::JsonValue& value);
};

}  // namespace slg::database::redis
