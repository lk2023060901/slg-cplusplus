#include "database/redis/redis_config.h"

namespace slg::database::redis {
namespace {

std::optional<std::string> ReadString(const json::JsonValue& value, std::string_view key) {
    auto maybe = value.Get(key);
    if (!maybe.has_value()) {
        return std::nullopt;
    }
    return maybe->As<std::string>();
}

std::optional<int> ReadInt(const json::JsonValue& value, std::string_view key) {
    auto maybe = value.Get(key);
    if (!maybe.has_value()) {
        return std::nullopt;
    }
    return maybe->As<int>();
}

std::optional<std::uint16_t> ReadUint16(const json::JsonValue& value, std::string_view key) {
    auto maybe = value.Get(key);
    if (!maybe.has_value()) {
        return std::nullopt;
    }
    return maybe->As<std::uint16_t>();
}

}  // namespace

std::optional<RedisConfig> RedisConfig::FromJson(const json::JsonValue& value) {
    if (!value.IsObject()) {
        return std::nullopt;
    }

    RedisConfig config;
    if (auto cluster = value.Get("cluster")) {
        if (auto flag = cluster->As<bool>()) {
            config.cluster_mode = *flag;
        }
    }

    if (auto pool = ReadInt(value, "pool_size")) {
        config.pool_size = static_cast<std::size_t>(std::max(1, *pool));
    }

    if (auto username = ReadString(value, "username")) {
        config.username = username;
    }
    if (auto password = ReadString(value, "password")) {
        config.password = password;
    }

    if (auto connect_timeout_ms = ReadInt(value, "connect_timeout_ms")) {
        config.connect_timeout = std::chrono::milliseconds(*connect_timeout_ms);
    }
    if (auto socket_timeout_ms = ReadInt(value, "socket_timeout_ms")) {
        config.socket_timeout = std::chrono::milliseconds(*socket_timeout_ms);
    }

    auto endpoints = value.Get("endpoints");
    if (endpoints.has_value() && endpoints->IsArray()) {
        auto size = endpoints->Raw().size();
        for (std::size_t i = 0; i < size; ++i) {
            auto entry = endpoints->Get(i);
            if (!entry.has_value() || !entry->IsObject()) {
                continue;
            }
            RedisEndpoint endpoint;
            if (auto host = ReadString(*entry, "host")) {
                endpoint.host = *host;
            }
            if (auto port = ReadUint16(*entry, "port")) {
                endpoint.port = *port;
            }
            if (auto db = ReadInt(*entry, "db")) {
                endpoint.db = *db;
            }
            if (auto pwd = ReadString(*entry, "password")) {
                endpoint.password = *pwd;
            }
            config.endpoints.push_back(std::move(endpoint));
        }
    }

    if (config.endpoints.empty()) {
        RedisEndpoint endpoint;
        if (auto host = ReadString(value, "host")) {
            endpoint.host = *host;
        }
        if (auto port = ReadUint16(value, "port")) {
            endpoint.port = *port;
        }
        if (auto db = ReadInt(value, "db")) {
            endpoint.db = *db;
        }
        if (auto pwd = ReadString(value, "password")) {
            endpoint.password = *pwd;
        }
        config.endpoints.push_back(std::move(endpoint));
    }

    return config;
}

}  // namespace slg::database::redis
