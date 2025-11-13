#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "database/citus/citus_export.h"
#include "json/json_value.h"

namespace slg::database::citus {

struct CitusNodeConfig {
    std::string name;
    std::string host;
    std::uint16_t port{5432};

    SLG_CITUS_API static std::optional<CitusNodeConfig> FromJson(const json::JsonValue& value);
};

struct CitusConfig {
    std::string coordinator_conninfo;
    std::vector<CitusNodeConfig> bootstrap_nodes;
    std::string metadata_database{"postgres"};
    bool auto_register_workers{true};

    SLG_CITUS_API static std::optional<CitusConfig> FromJson(const json::JsonValue& value);
};

}  // namespace slg::database::citus
