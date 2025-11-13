#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sw/redis++/redis++.h>

#include "database/redis/redis_config.h"
#include "database/redis/redis_export.h"

namespace slg::database::redis {

class RedisClient {
public:
    SLG_REDIS_API explicit RedisClient(RedisConfig config);
    SLG_REDIS_API ~RedisClient();

    SLG_REDIS_API bool Connect();
    SLG_REDIS_API bool IsCluster() const noexcept { return config_.cluster_mode; }

    SLG_REDIS_API std::shared_ptr<sw::redis::Redis> Standalone();
    SLG_REDIS_API std::shared_ptr<sw::redis::RedisCluster> Cluster();

    SLG_REDIS_API bool Set(std::string_view key,
                           std::string_view value,
                           std::chrono::milliseconds ttl = std::chrono::milliseconds(0));
    SLG_REDIS_API std::optional<std::string> Get(std::string_view key);
    SLG_REDIS_API bool Del(std::string_view key);
    SLG_REDIS_API bool Exists(std::string_view key);
    SLG_REDIS_API bool Expire(std::string_view key, std::chrono::seconds ttl);

    SLG_REDIS_API bool HSet(std::string_view key, std::string_view field, std::string_view value);
    SLG_REDIS_API std::optional<std::string> HGet(std::string_view key, std::string_view field);
    SLG_REDIS_API bool HDel(std::string_view key, std::string_view field);
    SLG_REDIS_API std::unordered_map<std::string, std::string> HGetAll(std::string_view key);

    SLG_REDIS_API bool LPush(std::string_view key, std::string_view value);
    SLG_REDIS_API bool RPush(std::string_view key, std::string_view value);
    SLG_REDIS_API std::optional<std::string> LPop(std::string_view key);
    SLG_REDIS_API std::optional<std::string> RPop(std::string_view key);

    using PipelineHandler = std::function<void(sw::redis::Pipeline&)>;
    using ScriptArgs = std::vector<std::string>;
    SLG_REDIS_API bool ExecutePipeline(const PipelineHandler& handler, std::string_view hash_tag = {});
    SLG_REDIS_API std::optional<std::string> Eval(const std::string& script,
                                                  const std::vector<std::string>& keys,
                                                  const std::vector<std::string>& args);

    using PubSubCallback = std::function<void(const std::string& channel, const std::string& message)>;
    SLG_REDIS_API bool Publish(std::string_view channel, std::string_view message);
    SLG_REDIS_API bool Subscribe(const std::vector<std::string>& channels, PubSubCallback callback);
    SLG_REDIS_API bool Unsubscribe();

    using StreamKey = std::pair<std::string, std::string>;
    using StreamEntries = std::vector<std::pair<std::string, std::map<std::string, std::string>>>;
    SLG_REDIS_API std::optional<std::string> XAdd(std::string_view stream,
                                                  const std::map<std::string, std::string>& values,
                                                  std::optional<std::size_t> max_len = std::nullopt,
                                                  bool exact_trim = false);
    SLG_REDIS_API StreamEntries XRead(const std::vector<StreamKey>& streams,
                                      std::chrono::milliseconds timeout = std::chrono::milliseconds(0),
                                      std::size_t count = 0);
    SLG_REDIS_API bool XAck(std::string_view stream,
                            std::string_view group,
                            const std::vector<std::string>& ids);
    SLG_REDIS_API bool XGroupCreate(std::string_view stream,
                                    std::string_view group,
                                    std::string_view id = "$",
                                    bool mkstream = true);
    SLG_REDIS_API bool XGroupDestroy(std::string_view stream, std::string_view group);

    using TransactionHandler = std::function<void(sw::redis::Transaction&)>;
    SLG_REDIS_API bool ExecuteTransaction(const TransactionHandler& handler,
                                          std::string_view hash_tag = {},
                                          bool piped = false);

    SLG_REDIS_API bool AcquireLock(std::string_view key,
                                   std::string_view token,
                                   std::chrono::milliseconds ttl,
                                   std::chrono::milliseconds retry_interval =
                                       std::chrono::milliseconds(100),
                                   std::size_t max_retry = 3);
    SLG_REDIS_API bool ReleaseLock(std::string_view key, std::string_view token);

private:
    bool EnsureConnected();
    sw::redis::ConnectionOptions BuildConnectionOptions(const RedisEndpoint& endpoint) const;
    sw::redis::ConnectionPoolOptions BuildPoolOptions() const;

    RedisConfig config_;
    std::shared_ptr<sw::redis::Redis> redis_;
    std::shared_ptr<sw::redis::RedisCluster> cluster_;
    std::unique_ptr<sw::redis::Subscriber> subscriber_;
    std::mutex subscriber_mutex_;
    std::thread subscriber_thread_;
    std::atomic<bool> subscriber_running_{false};
    PubSubCallback subscriber_callback_;
};

}  // namespace slg::database::redis
