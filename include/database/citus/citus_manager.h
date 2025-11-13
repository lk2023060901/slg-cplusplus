#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/citus/citus_config.h"
#include "database/citus/citus_connection.h"

namespace slg::database::citus {

class CitusManager {
public:
    SLG_CITUS_API explicit CitusManager(CitusConfig config);
    SLG_CITUS_API ~CitusManager();

    SLG_CITUS_API bool Connect();

    SLG_CITUS_API bool RegisterWorker(const CitusNodeConfig& node);
    SLG_CITUS_API bool RemoveWorker(const std::string& node_name);
    SLG_CITUS_API bool RefreshNodeCache();

    SLG_CITUS_API bool CreateDistributedTable(const std::string& table,
                                              const std::string& distribution_column,
                                              const std::string& colocate_with = "");
    SLG_CITUS_API bool CreateReferenceTable(const std::string& table);
    SLG_CITUS_API bool RebalanceTable(const std::string& table);
    SLG_CITUS_API bool RebalanceCluster();

    SLG_CITUS_API std::vector<CitusNodeConfig> KnownWorkers() const;

    SLG_CITUS_API bool ExecuteCommand(const std::string& sql);

private:
    bool EnsureConnected();
    bool RegisterBootstrapNodes();

    CitusConfig config_;
    mutable std::mutex mutex_;
    CitusConnection coordinator_;
    std::unordered_map<std::string, CitusNodeConfig> worker_cache_;
    bool connected_{false};
};

}  // namespace slg::database::citus
