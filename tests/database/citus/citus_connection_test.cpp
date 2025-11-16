#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <libpq-fe.h>

#include "coroutine/scheduler.h"
#include "database/citus/citus_connection.h"
#include "database/citus/citus_manager.h"
#include "database/citus/citus_config.h"
#include "network/tcp/tcp_io_context.h"

namespace {

using slg::database::citus::CitusConnection;
using slg::database::citus::CitusConfig;
using slg::database::citus::CitusManager;
using slg::network::tcp::TcpIoContext;

std::string PostgresConnInfo() {
    if (const char* env = std::getenv("SLG_TEST_PG_CONNINFO")) {
        return std::string(env);
    }
    return "host=127.0.0.1 port=5432 dbname=slgdb user=slguser password=slgpass";
}

std::string UniqueTableName(const std::string& prefix) {
    static std::atomic<std::uint64_t> counter{0};
    return "slg_citus_" + prefix + "_" + std::to_string(counter++);
}

bool HasCitusExtension(const std::string& conninfo,
                       TcpIoContext& io_context,
                       slg::coroutine::CoroutineScheduler& scheduler) {
    CitusConnection connection(io_context.GetContext(), scheduler);
    if (!connection.Connect(conninfo)) {
        return false;
    }
    auto result = connection.ExecuteScalar(
        "SELECT extname FROM pg_extension WHERE extname = 'citus' LIMIT 1");
    return !result.empty();
}

bool EnsureCitusExtension(const std::string& conninfo,
                          TcpIoContext& io_context,
                          slg::coroutine::CoroutineScheduler& scheduler) {
    CitusConnection connection(io_context.GetContext(), scheduler);
    if (!connection.Connect(conninfo)) {
        return false;
    }
    return connection.Execute("CREATE EXTENSION IF NOT EXISTS citus");
}

class CitusTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        io_context_ = std::make_shared<TcpIoContext>(1);
        io_context_->Start();
        scheduler_ = std::make_shared<slg::coroutine::CoroutineScheduler>(1);
        conninfo_ = PostgresConnInfo();
    }

    void TearDown() override {
        io_context_->Stop();
        io_context_->Join();
    }

    std::shared_ptr<TcpIoContext> io_context_;
    std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler_;
    std::string conninfo_;
};

TEST_F(CitusTestBase, ExecuteAndQuery) {
    CitusConnection connection(io_context_->GetContext(), *scheduler_);
    if (!connection.Connect(conninfo_)) {
        GTEST_SKIP() << "Unable to connect to Postgres at " << conninfo_;
    }

    const std::string table_name = "slg_test_fiber_" + std::to_string(std::rand());
    const std::string create_sql =
        "CREATE TABLE IF NOT EXISTS " + table_name + " (id SERIAL PRIMARY KEY, val TEXT)";
    ASSERT_TRUE(connection.Execute(create_sql));

    ASSERT_TRUE(connection.Execute("INSERT INTO " + table_name + " (val) VALUES ('fiber-row')"));
    auto value = connection.ExecuteScalar("SELECT val FROM " + table_name +
                                          " ORDER BY id DESC LIMIT 1");
    EXPECT_EQ(value, "fiber-row");

    ASSERT_TRUE(connection.Execute("DROP TABLE IF EXISTS " + table_name));
}

TEST_F(CitusTestBase, QueryHandlerReceivesRows) {
    CitusConnection connection(io_context_->GetContext(), *scheduler_);
    if (!connection.Connect(conninfo_)) {
        GTEST_SKIP() << "Unable to connect to Postgres at " << conninfo_;
    }

    const auto table_name = UniqueTableName("query_rows");
    ASSERT_TRUE(connection.Execute("CREATE TABLE " + table_name + " (id SERIAL, val INT)"));
    ASSERT_TRUE(connection.Execute("INSERT INTO " + table_name + " (val) VALUES (1), (2), (3)"));

    int rows = 0;
    ASSERT_TRUE(connection.Query("SELECT * FROM " + table_name,
                                 [&rows](PGresult* result) {
                                     rows += PQntuples(result);
                                     return true;
                                 }));
    EXPECT_EQ(rows, 3);

    ASSERT_TRUE(connection.Execute("DROP TABLE IF EXISTS " + table_name));
}

TEST_F(CitusTestBase, SupportsConcurrentConnections) {
    const auto table_name = UniqueTableName("parallel");
    {
        CitusConnection connection(io_context_->GetContext(), *scheduler_);
        if (!connection.Connect(conninfo_)) {
            GTEST_SKIP() << "Unable to connect to Postgres at " << conninfo_;
        }
        connection.Execute("DROP TABLE IF EXISTS " + table_name);
        ASSERT_TRUE(
            connection.Execute("CREATE TABLE IF NOT EXISTS " + table_name + " (id SERIAL, val INT)"));
    }

    constexpr int kInserts = 4;
    for (int i = 0; i < kInserts; ++i) {
        CitusConnection conn(io_context_->GetContext(), *scheduler_);
        ASSERT_TRUE(conn.Connect(conninfo_));
        ASSERT_TRUE(conn.Execute("INSERT INTO " + table_name + " (val) VALUES (" +
                                 std::to_string(i) + ")"));
    }

    {
        CitusConnection connection(io_context_->GetContext(), *scheduler_);
        ASSERT_TRUE(connection.Connect(conninfo_));
        auto count = connection.ExecuteScalar("SELECT COUNT(*) FROM " + table_name);
        EXPECT_EQ(count, std::to_string(kInserts));
        ASSERT_TRUE(connection.Execute("DROP TABLE IF EXISTS " + table_name));
    }
}

TEST_F(CitusTestBase, ManagerExecutesCommands) {
    CitusConfig config;
    config.coordinator_conninfo = conninfo_;
    config.auto_register_workers = false;
    auto manager = std::make_unique<CitusManager>(config, io_context_, scheduler_);
    if (!manager->Connect()) {
        GTEST_SKIP() << "Unable to connect to Postgres at " << conninfo_;
    }

    const std::string table_name = "slg_manager_test_" + std::to_string(std::rand());
    EXPECT_TRUE(
        manager->ExecuteCommand("CREATE TABLE IF NOT EXISTS " + table_name + " (val TEXT)"));
    EXPECT_TRUE(manager->ExecuteCommand("INSERT INTO " + table_name + " (val) VALUES ('mgr')"));
    EXPECT_TRUE(manager->ExecuteCommand("DROP TABLE IF EXISTS " + table_name));
}

TEST_F(CitusTestBase, ManagerRefreshNodeCacheHandlesAbsence) {
    CitusConfig config;
    config.coordinator_conninfo = conninfo_;
    config.auto_register_workers = false;
    auto manager = std::make_unique<CitusManager>(config, io_context_, scheduler_);
    if (!manager->Connect()) {
        GTEST_SKIP() << "Unable to connect to Postgres at " << conninfo_;
    }

    const bool has_extension = HasCitusExtension(conninfo_, *io_context_, *scheduler_);
    bool refreshed = manager->RefreshNodeCache();
    if (has_extension) {
        EXPECT_TRUE(refreshed);
    } else {
        EXPECT_FALSE(refreshed);
    }
}

TEST_F(CitusTestBase, ManagerCreatesReferenceTableWhenCitusAvailable) {
    if (!EnsureCitusExtension(conninfo_, *io_context_, *scheduler_)) {
        GTEST_SKIP() << "Citus extension not available in this Postgres instance.";
    }

    CitusConfig config;
    config.coordinator_conninfo = conninfo_;
    config.auto_register_workers = false;
    auto manager = std::make_unique<CitusManager>(config, io_context_, scheduler_);
    ASSERT_TRUE(manager->Connect());

    const auto table_name = UniqueTableName("reference");
    auto connection = std::make_unique<CitusConnection>(io_context_->GetContext(), *scheduler_);
    ASSERT_TRUE(connection->Connect(conninfo_));
    ASSERT_TRUE(connection->Execute("CREATE TABLE " + table_name + " (id BIGINT PRIMARY KEY)"));
    EXPECT_TRUE(manager->CreateReferenceTable(table_name));
    EXPECT_TRUE(connection->Execute("DROP TABLE IF EXISTS " + table_name));
}

}  // namespace
