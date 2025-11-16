#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "coroutine/scheduler.h"
#include "database/citus/citus_config.h"
#include "database/citus/citus_connection.h"
#include "network/tcp/tcp_io_context.h"

namespace slg::database::citus {

class CitusManager {
public:
    SLG_CITUS_API explicit CitusManager(CitusConfig config);
    SLG_CITUS_API CitusManager(CitusConfig config,
                               std::shared_ptr<slg::network::tcp::TcpIoContext> io_context,
                               std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler);
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
    slg::network::tcp::TcpIoContext& EnsureIoContext();
    slg::coroutine::CoroutineScheduler& EnsureScheduler();

    CitusConfig config_;
    mutable std::mutex mutex_;
    std::shared_ptr<slg::network::tcp::TcpIoContext> io_context_;
    std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler_;
    bool owns_io_context_{false};
    CitusConnection coordinator_;
    std::unordered_map<std::string, CitusNodeConfig> worker_cache_;
    bool connected_{false};
};

}  // namespace slg::database::citus
