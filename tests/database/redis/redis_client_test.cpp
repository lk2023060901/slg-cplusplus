#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <cerrno>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "database/redis/redis_client.h"

#include <sw/redis++/redis++.h>

namespace {

using slg::database::redis::RedisClient;
using slg::database::redis::RedisConfig;
using slg::database::redis::RedisEndpoint;

RedisConfig DefaultConfig() {
    RedisConfig config;
    config.cluster_mode = false;
    config.pool_size = 1;
    config.endpoints = {RedisEndpoint{}};
    config.endpoints.front().host = "127.0.0.1";
    config.endpoints.front().port = 6379;
    config.connect_timeout = std::chrono::milliseconds(200);
    config.socket_timeout = std::chrono::milliseconds(200);
    return config;
}

std::string UniqueKey(const std::string& suffix) {
    static std::atomic<std::uint64_t> counter{0};
    const auto now =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()
                                                                  .time_since_epoch())
            .count();
    return "slg_test:" + suffix + ":" + std::to_string(now) + ":" + std::to_string(counter++);
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

class RedisClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!CanReach("127.0.0.1", 6379)) {
            GTEST_SKIP() << "Redis server not reachable at 127.0.0.1:6379";
        }

        RedisConfig cfg = DefaultConfig();
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
            if (error.empty()) {
                GTEST_SKIP() << "Redis server not reachable at 127.0.0.1:6379";
            }
            GTEST_SKIP() << "Redis server unavailable: " << error;
        }

        bool healthy = false;
        const auto health_key = UniqueKey("healthcheck");
        try {
            healthy = client_->Set(health_key, "ping");
            if (healthy) {
                client_->Del(health_key);
            }
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        if (!healthy) {
            GTEST_SKIP() << "Redis server not ready: " << error;
        }
    }

    void TearDown() override {
        if (client_) {
            client_->Unsubscribe();
        }
    }

    std::unique_ptr<RedisClient> client_;
};

TEST_F(RedisClientTest, SetAndGetRoundTrip) {
    const auto key = UniqueKey("set_get");
    ASSERT_TRUE(client_->Set(key, "fiber-value"));
    auto value = client_->Get(key);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "fiber-value");

    ASSERT_TRUE(client_->Expire(key, std::chrono::seconds(1)));
    EXPECT_TRUE(client_->Exists(key));
}

TEST_F(RedisClientTest, AcquireAndReleaseLock) {
    const auto key = UniqueKey("lock");
    EXPECT_TRUE(client_->AcquireLock(key, "token1", std::chrono::milliseconds(500),
                                     std::chrono::milliseconds(50), 2));
    EXPECT_FALSE(client_->AcquireLock(key, "token2", std::chrono::milliseconds(500),
                                      std::chrono::milliseconds(50), 1));
    EXPECT_TRUE(client_->ReleaseLock(key, "token1"));
    EXPECT_TRUE(client_->AcquireLock(key, "token3", std::chrono::milliseconds(500),
                                     std::chrono::milliseconds(50), 1));
    EXPECT_TRUE(client_->ReleaseLock(key, "token3"));
}

TEST_F(RedisClientTest, PublishAndSubscribe) {
    const auto channel = UniqueKey("channel");

    auto received = std::make_shared<std::promise<std::string>>();
    auto future = received->get_future();

    ASSERT_TRUE(client_->Subscribe({channel},
                                   [received](const std::string&, const std::string& message) {
                                       try {
                                           received->set_value(message);
                                       } catch (...) {
                                           // Promise already satisfied.
                                       }
                                   }));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(client_->Publish(channel, "hello-world"));

    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), "hello-world");
}

TEST_F(RedisClientTest, PipelineExecutesCommands) {
    const auto key1 = UniqueKey("pipeline1");
    const auto key2 = UniqueKey("pipeline2");

    ASSERT_TRUE(client_->ExecutePipeline(
        [&](sw::redis::Pipeline& pipe) {
            pipe.set(key1, "value1");
            pipe.set(key2, "value2");
        }));

    auto v1 = client_->Get(key1);
    auto v2 = client_->Get(key2);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v1, "value1");
    EXPECT_EQ(*v2, "value2");
}

TEST_F(RedisClientTest, TransactionExecutesCommands) {
    const auto key = UniqueKey("txn_key");
    const auto counter = UniqueKey("txn_counter");

    EXPECT_TRUE(client_->Set(counter, "0"));

    ASSERT_TRUE(client_->ExecuteTransaction(
        [&](sw::redis::Transaction& tx) {
            tx.set(key, "txn_value");
            tx.incr(counter);
        }));

    auto val = client_->Get(key);
    auto count = client_->Get(counter);
    ASSERT_TRUE(val.has_value());
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*val, "txn_value");
    EXPECT_EQ(*count, "1");
}

TEST_F(RedisClientTest, StreamOperations) {
    const auto stream = UniqueKey("stream");
    std::map<std::string, std::string> values{{"field", "value"}};

    ASSERT_TRUE(client_->XGroupCreate(stream, "group1", "$", true));

    auto id = client_->XAdd(stream, values);
    ASSERT_TRUE(id.has_value());

    auto entries = client_->XReadGroup("group1", "consumer1", {{stream, ">"}},
                                       std::chrono::milliseconds(200), 10);
    ASSERT_FALSE(entries.empty());

    std::vector<std::string> ids;
    for (const auto& entry : entries) {
        ids.push_back(entry.first);
    }

    EXPECT_TRUE(client_->XAck(stream, "group1", ids));
    EXPECT_TRUE(client_->XGroupDestroy(stream, "group1"));
}

TEST_F(RedisClientTest, StreamPendingRecovery) {
    const auto stream = UniqueKey("stream_pending");
    ASSERT_TRUE(client_->XGroupCreate(stream, "group1", "$", true));

    std::map<std::string, std::string> first{{"field", "value1"}};
    std::map<std::string, std::string> second{{"field", "value2"}};
    ASSERT_TRUE(client_->XAdd(stream, first).has_value());
    auto second_id = client_->XAdd(stream, second);
    ASSERT_TRUE(second_id.has_value());

    auto entries = client_->XReadGroup("group1", "consumer1", {{stream, ">"}},
                                       std::chrono::milliseconds(200), 10);
    ASSERT_EQ(entries.size(), 2u);

    auto pending = client_->XReadGroup("group1", "consumer1", {{stream, "0-0"}},
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
    EXPECT_TRUE(client_->XAck(stream, "group1", ids));
    EXPECT_TRUE(client_->XGroupDestroy(stream, "group1"));
}

TEST_F(RedisClientTest, RecoversAfterConnectionKill) {
    const auto key1 = UniqueKey("recover_first");
    ASSERT_TRUE(client_->Set(key1, "value"));

    KillClients("127.0.0.1", 6379);
    ASSERT_TRUE(client_->Connect());

    const auto key2 = UniqueKey("recover_second");
    ASSERT_TRUE(client_->Set(key2, "value2"));
    auto value = client_->Get(key2);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "value2");
}

}  // namespace
