#include "database/citus/citus_config.h"

namespace slg::database::citus {
namespace {

std::optional<std::string> GetString(const json::JsonValue& value, std::string_view key) {
    auto nested = value.Get(key);
    if (!nested.has_value()) {
        return std::nullopt;
    }
    return nested->As<std::string>();
}

std::optional<std::uint16_t> GetUint16(const json::JsonValue& value, std::string_view key) {
    auto nested = value.Get(key);
    if (!nested.has_value()) {
        return std::nullopt;
    }
    auto port = nested->As<std::uint16_t>();
    if (!port.has_value()) {
        return std::nullopt;
    }
    return port;
}

}  // namespace

std::optional<CitusNodeConfig> CitusNodeConfig::FromJson(const json::JsonValue& value) {
    if (!value.IsObject()) {
        return std::nullopt;
    }

    CitusNodeConfig node;
    auto name = GetString(value, "name");
    auto host = GetString(value, "host");
    auto port = GetUint16(value, "port");
    if (!name || !host) {
        return std::nullopt;
    }
    node.name = *name;
    node.host = *host;
    node.port = port.value_or(5432);
    return node;
}

std::optional<CitusConfig> CitusConfig::FromJson(const json::JsonValue& value) {
    if (!value.IsObject()) {
        return std::nullopt;
    }

    CitusConfig config;
    auto conninfo = GetString(value, "coordinator_conninfo");
    if (!conninfo) {
        return std::nullopt;
    }
    config.coordinator_conninfo = *conninfo;

    if (auto metadata_db = GetString(value, "metadata_database")) {
        config.metadata_database = *metadata_db;
    }

    if (auto auto_register = value.Get("auto_register_workers")) {
        if (auto enabled = auto_register->As<bool>()) {
            config.auto_register_workers = *enabled;
        }
    }

    if (auto nodes = value.Get("bootstrap_nodes")) {
        if (nodes->IsArray()) {
            for (std::size_t i = 0; i < nodes->Raw().size(); ++i) {
                auto item = nodes->Get(i);
                if (!item.has_value()) {
                    continue;
                }
                auto parsed = CitusNodeConfig::FromJson(*item);
                if (parsed.has_value()) {
                    config.bootstrap_nodes.push_back(*parsed);
                }
            }
        }
    }

    return config;
}

}  // namespace slg::database::citus
