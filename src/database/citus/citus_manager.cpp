#include "database/citus/citus_manager.h"

#include <sstream>

namespace slg::database::citus {

CitusManager::CitusManager(CitusConfig config)
    : CitusManager(std::move(config), nullptr, nullptr) {}

CitusManager::CitusManager(CitusConfig config,
                           std::shared_ptr<slg::network::tcp::TcpIoContext> io_context,
                           std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler)
    : config_(std::move(config)),
      io_context_(std::move(io_context)),
      scheduler_(std::move(scheduler)),
      coordinator_(EnsureIoContext().GetContext(), EnsureScheduler()) {
    EnsureIoContext().Start();
}

CitusManager::~CitusManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        coordinator_.Disconnect();
    }

    if (owns_io_context_ && io_context_) {
        io_context_->Stop();
        io_context_->Join();
    }
}

bool CitusManager::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = coordinator_.Connect(config_.coordinator_conninfo);
    if (connected_ && config_.auto_register_workers) {
        RegisterBootstrapNodes();
        RefreshNodeCache();
    }
    return connected_;
}

bool CitusManager::EnsureConnected() {
    if (!connected_ || !coordinator_.IsConnected()) {
        connected_ = coordinator_.Connect(config_.coordinator_conninfo);
    }
    return connected_;
}

bool CitusManager::ExecuteCommand(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureConnected()) {
        return false;
    }
    return coordinator_.Execute(sql);
}

bool CitusManager::RegisterWorker(const CitusNodeConfig& node) {
    std::ostringstream ss;
    ss << "SELECT master_add_node('" << node.host << "', " << node.port << ", '" << node.name
       << "');";
    if (!ExecuteCommand(ss.str())) {
        return false;
    }
    worker_cache_[node.name] = node;
    return true;
}

bool CitusManager::RemoveWorker(const std::string& node_name) {
    std::ostringstream ss;
    ss << "SELECT master_remove_node('" << node_name << "');";
    if (!ExecuteCommand(ss.str())) {
        return false;
    }
    worker_cache_.erase(node_name);
    return true;
}

bool CitusManager::RefreshNodeCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureConnected()) {
        return false;
    }
    worker_cache_.clear();
    const char* query =
        "SELECT node_name, node_host, node_port FROM pg_dist_node WHERE node_role = 'primary'";
    PGresult* result = PQexec(coordinator_.Handle(), query);
    if (!result) {
        return false;
    }
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        PQclear(result);
        return false;
    }
    int rows = PQntuples(result);
    for (int i = 0; i < rows; ++i) {
        CitusNodeConfig node;
        node.name = PQgetvalue(result, i, 0);
        node.host = PQgetvalue(result, i, 1);
        node.port = static_cast<std::uint16_t>(std::stoi(PQgetvalue(result, i, 2)));
        worker_cache_[node.name] = node;
    }
    PQclear(result);
    return true;
}

bool CitusManager::CreateDistributedTable(const std::string& table,
                                          const std::string& distribution_column,
                                          const std::string& colocate_with) {
    std::ostringstream ss;
    ss << "SELECT create_distributed_table('" << table << "', '" << distribution_column << "'";
    if (!colocate_with.empty()) {
        ss << ", colocate_with => '" << colocate_with << "'";
    }
    ss << ");";
    return ExecuteCommand(ss.str());
}

bool CitusManager::CreateReferenceTable(const std::string& table) {
    std::ostringstream ss;
    ss << "SELECT create_reference_table('" << table << "');";
    return ExecuteCommand(ss.str());
}

bool CitusManager::RebalanceTable(const std::string& table) {
    std::ostringstream ss;
    ss << "SELECT rebalance_table_shards('" << table << "');";
    return ExecuteCommand(ss.str());
}

bool CitusManager::RebalanceCluster() {
    return ExecuteCommand("SELECT rebalance_table_shards();");
}

std::vector<CitusNodeConfig> CitusManager::KnownWorkers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CitusNodeConfig> nodes;
    nodes.reserve(worker_cache_.size());
    for (const auto& [_, node] : worker_cache_) {
        nodes.push_back(node);
    }
    return nodes;
}

bool CitusManager::RegisterBootstrapNodes() {
    bool ok = true;
    for (const auto& node : config_.bootstrap_nodes) {
        ok &= RegisterWorker(node);
    }
    return ok;
}

slg::network::tcp::TcpIoContext& CitusManager::EnsureIoContext() {
    if (!io_context_) {
        io_context_ = std::make_shared<slg::network::tcp::TcpIoContext>(1);
        owns_io_context_ = true;
    }
    return *io_context_;
}

slg::coroutine::CoroutineScheduler& CitusManager::EnsureScheduler() {
    if (!scheduler_) {
        scheduler_ = std::make_shared<slg::coroutine::CoroutineScheduler>();
    }
    return *scheduler_;
}

}  // namespace slg::database::citus
