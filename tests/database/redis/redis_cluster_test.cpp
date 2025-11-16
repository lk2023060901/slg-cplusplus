#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include <cstdlib>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "database/redis/redis_client.h"

namespace {

using slg::database::redis::RedisClient;
using slg::database::redis::RedisConfig;
using slg::database::redis::RedisEndpoint;

std::string ClusterHost() {
    if (const char* env = std::getenv("SLG_REDIS_CLUSTER_HOST")) {
        if (*env != '\0') {
            return env;
        }
    }
    return "127.0.0.1";
}

RedisConfig ClusterConfig() {
    RedisConfig config;
    config.cluster_mode = true;
    config.pool_size = 4;
    config.connect_timeout = std::chrono::milliseconds(500);
    config.socket_timeout = std::chrono::milliseconds(500);
    const auto host = ClusterHost();
    for (int idx = 0; idx < 6; ++idx) {
        RedisEndpoint endpoint;
        endpoint.host = host;
        endpoint.port = static_cast<std::uint16_t>(10000 + idx);
        config.endpoints.push_back(endpoint);
    }
    return config;
}

std::string ClusterUniqueKey(const std::string& suffix) {
    static std::atomic<std::uint64_t> counter{0};
    const auto now =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()
                                                                  .time_since_epoch())
            .count();
    return "slg_cluster_test:" + suffix + ":" + std::to_string(now) + ":" + std::to_string(counter++);
}

std::string HashTag(const std::string& token) {
    return "{" + token + "}";
}

std::string KeyWithHashTag(const std::string& hash_tag, const std::string& suffix) {
    static std::atomic<std::uint64_t> counter{0};
    return "slg_cluster_test:" + hash_tag + ":" + suffix + ":" + std::to_string(counter++);
}

void KillClients(const std::string& host, std::uint16_t port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    sw::redis::Redis admin(opts);
    try {
        admin.command("CLIENT", "KILL", "TYPE", "normal", "SKIPME", "yes");
    } catch (...) {
    }
}

void KillClusterClients(const std::string& host) {
    for (std::uint16_t port = 10000; port < 10006; ++port) {
        KillClients(host, port);
    }
}

bool CanReach(const std::string& host, std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        ::close(fd);
        return true;
    }
    if (errno != EINPROGRESS) {
        ::close(fd);
        return false;
    }
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000;
    rc = ::select(fd + 1, nullptr, &write_set, nullptr, &tv);
    if (rc <= 0) {
        ::close(fd);
        return false;
    }
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        ::close(fd);
        return false;
    }
    ::close(fd);
    return err == 0;
}

class RedisClusterClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto host = ClusterHost();
        bool reachable = true;
        for (const auto& endpoint : ClusterConfig().endpoints) {
            if (!CanReach(host, endpoint.port)) {
                reachable = false;
                break;
            }
        }
        if (!reachable) {
            GTEST_SKIP() << "Redis cluster ports 10000-10005 are not reachable from host.";
        }

        RedisConfig cfg = ClusterConfig();
        client_ = std::make_unique<RedisClient>(cfg);
        bool connected = false;
        std::string error;
        try {
            connected = client_->Connect();
        } catch (const std::exception& ex) {
            error = ex.what();
        } catch (...) {
            error = "unknown exception";
        }

        if (!connected) {
            GTEST_SKIP() << "Redis cluster not reachable on 127.0.0.1:10000-10005"
                         << (error.empty() ? "" : " - " + error);
        }

        if (!client_->IsCluster()) {
            GTEST_SKIP() << "Redis client not configured for cluster mode.";
        }

        bool healthy = false;
        const auto health_key = ClusterUniqueKey("health");
        try {
            healthy = client_->Set(health_key, "ping");
            if (healthy) {
                client_->Del(health_key);
            }
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        if (!healthy) {
            GTEST_SKIP() << "Redis cluster not ready: " << error;
        }
    }

    void TearDown() override {
        if (client_) {
            client_->Unsubscribe();
        }
    }

    std::unique_ptr<RedisClient> client_;
};

TEST_F(RedisClusterClientTest, SetAndGetDifferentSlots) {
    std::vector<std::string> keys;
    for (int i = 0; i < 10; ++i) {
        keys.emplace_back(ClusterUniqueKey("set_get_" + std::to_string(i)));
    }

    for (std::size_t i = 0; i < keys.size(); ++i) {
        ASSERT_TRUE(client_->Set(keys[i], "value-" + std::to_string(i)));
    }

    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto value = client_->Get(keys[i]);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, "value-" + std::to_string(i));
    }
}

TEST_F(RedisClusterClientTest, PipelineWithinHashSlot) {
    const std::string hash_tag = HashTag("cluster_pipeline");
    const std::string key1 = KeyWithHashTag(hash_tag, "key1");
    const std::string key2 = KeyWithHashTag(hash_tag, "key2");

    ASSERT_TRUE(client_->ExecutePipeline(
        [&](sw::redis::Pipeline& pipeline) {
            pipeline.set(key1, "pipeline-value-1");
            pipeline.set(key2, "pipeline-value-2");
        },
        hash_tag));

    auto v1 = client_->Get(key1);
    auto v2 = client_->Get(key2);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v1, "pipeline-value-1");
    EXPECT_EQ(*v2, "pipeline-value-2");
}

TEST_F(RedisClusterClientTest, TransactionWithinHashSlot) {
    const std::string hash_tag = HashTag("cluster_tx");
    const std::string key = KeyWithHashTag(hash_tag, "key");
    const std::string counter = KeyWithHashTag(hash_tag, "counter");

    ASSERT_TRUE(client_->Set(counter, "0"));

    ASSERT_TRUE(client_->ExecuteTransaction(
        [&](sw::redis::Transaction& tx) {
            tx.set(key, "cluster-txn");
            tx.incr(counter);
        },
        hash_tag));

    auto value = client_->Get(key);
    auto count = client_->Get(counter);
    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*value, "cluster-txn");
    EXPECT_EQ(*count, "1");
}

TEST_F(RedisClusterClientTest, PublishAndSubscribeWorks) {
    const auto channel = ClusterUniqueKey("channel");
    auto received = std::make_shared<std::promise<std::string>>();
    auto future = received->get_future();

    ASSERT_TRUE(client_->Subscribe({channel},
                                   [received](const std::string&, const std::string& payload) {
                                       try {
                                           received->set_value(payload);
                                       } catch (...) {
                                       }
                                   }));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!client_->Publish(channel, "cluster-message")) {
        GTEST_SKIP() << "Cluster publish had no local subscribers (Redis Cluster only delivers to "
                        "clients connected to the same node)";
    }

    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(future.get(), "cluster-message");
}

TEST_F(RedisClusterClientTest, StreamOperationsAcrossNodes) {
    const auto stream = ClusterUniqueKey("stream");
    ASSERT_TRUE(client_->XGroupCreate(stream, "cluster-group", "$", true));

    std::map<std::string, std::string> payload{{"field", "value"}};
    auto id = client_->XAdd(stream, payload);
    ASSERT_TRUE(id.has_value());

    auto entries = client_->XReadGroup("cluster-group", "consumer-a", {{stream, ">"}},
                                       std::chrono::milliseconds(200), 10);
    ASSERT_FALSE(entries.empty());

    std::vector<std::string> ids;
    for (const auto& entry : entries) {
        ids.push_back(entry.first);
    }

    ASSERT_TRUE(client_->XAck(stream, "cluster-group", ids));
    ASSERT_TRUE(client_->XGroupDestroy(stream, "cluster-group"));
}

TEST_F(RedisClusterClientTest, StreamPendingRecovery) {
    const auto stream = ClusterUniqueKey("stream_pending");
    ASSERT_TRUE(client_->XGroupCreate(stream, "cluster-group", "$", true));

    std::map<std::string, std::string> first{{"field", "one"}};
    std::map<std::string, std::string> second{{"field", "two"}};
    ASSERT_TRUE(client_->XAdd(stream, first).has_value());
    auto second_id = client_->XAdd(stream, second);
    ASSERT_TRUE(second_id.has_value());

    auto entries = client_->XReadGroup("cluster-group", "consumer-a", {{stream, ">"}},
                                       std::chrono::milliseconds(200), 10);
    ASSERT_EQ(entries.size(), 2u);

    auto pending = client_->XReadGroup("cluster-group", "consumer-a", {{stream, "0-0"}},
                                       std::chrono::milliseconds(200), 10);
    ASSERT_EQ(pending.size(), entries.size());

    bool found = false;
    std::vector<std::string> ids;
    for (const auto& entry : pending) {
        ids.push_back(entry.first);
        if (entry.first == *second_id) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    EXPECT_TRUE(client_->XAck(stream, "cluster-group", ids));
    EXPECT_TRUE(client_->XGroupDestroy(stream, "cluster-group"));
}

TEST_F(RedisClusterClientTest, RecoversAfterConnectionKill) {
    const auto key1 = ClusterUniqueKey("recover_first");
    ASSERT_TRUE(client_->Set(key1, "value"));

    KillClusterClients(ClusterHost());
    ASSERT_TRUE(client_->Connect());

    const auto key2 = ClusterUniqueKey("recover_second");
    ASSERT_TRUE(client_->Set(key2, "value2"));
    auto value = client_->Get(key2);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value2");
}

TEST_F(RedisClusterClientTest, ConcurrentClientsOperateWithoutMovedErrors) {
    constexpr int kClients = 8;
    constexpr int kIterations = 100;

    std::vector<std::future<bool>> futures;
    futures.reserve(kClients);

    for (int i = 0; i < kClients; ++i) {
        futures.emplace_back(std::async(std::launch::async, [i]() {
            RedisClient client(ClusterConfig());
            if (!client.Connect()) {
                return false;
            }
            for (int iter = 0; iter < kIterations; ++iter) {
                const auto key = ClusterUniqueKey("concurrency_" + std::to_string(i));
                if (!client.Set(key, std::to_string(iter))) {
                    return false;
                }
                auto value = client.Get(key);
                if (!value.has_value() || *value != std::to_string(iter)) {
                    return false;
                }
            }
            return true;
        }));
    }

    for (auto& fut : futures) {
        EXPECT_TRUE(fut.get());
    }
}

}  // namespace
