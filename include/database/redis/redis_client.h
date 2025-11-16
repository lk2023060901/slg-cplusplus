#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <boost/fiber/future.hpp>
#include <boost/fiber/operations.hpp>
#include <sw/redis++/async_redis++.h>
#include <sw/redis++/redis++.h>

#include "coroutine/scheduler.h"
#include "database/redis/redis_config.h"
#include "database/redis/redis_export.h"

namespace slg::database::redis {

class RedisClient {
public:
    SLG_REDIS_API explicit RedisClient(RedisConfig config);
    SLG_REDIS_API RedisClient(RedisConfig config,
                              std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler);
    SLG_REDIS_API ~RedisClient();

    SLG_REDIS_API bool Connect();
    SLG_REDIS_API bool IsCluster() const noexcept { return config_.cluster_mode; }

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
    SLG_REDIS_API StreamEntries XReadGroup(const std::string& group,
                                           const std::string& consumer,
                                           const std::vector<StreamKey>& streams,
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
    template <typename Result>
    Result WaitFuture(sw::redis::Future<Result>&& future);

    bool EnsureConnected();
    sw::redis::ConnectionOptions BuildConnectionOptions(const RedisEndpoint& endpoint) const;
    sw::redis::ConnectionPoolOptions BuildPoolOptions() const;
    RedisConfig config_;
    std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler_;
    bool owns_scheduler_{false};
    sw::redis::EventLoopSPtr event_loop_;
    std::shared_ptr<sw::redis::Redis> sync_redis_;
    std::shared_ptr<sw::redis::RedisCluster> sync_cluster_;
    std::shared_ptr<sw::redis::AsyncRedis> async_redis_;
    std::shared_ptr<sw::redis::AsyncRedisCluster> async_cluster_;
    std::unique_ptr<sw::redis::AsyncSubscriber> subscriber_;
    std::mutex subscriber_mutex_;
    PubSubCallback subscriber_callback_;
};

}  // namespace slg::database::redis

template <typename Result>
Result slg::database::redis::RedisClient::WaitFuture(sw::redis::Future<Result>&& future) {
    using namespace std::chrono_literals;
    while (future.wait_for(0ms) != std::future_status::ready) {
        boost::this_fiber::yield();
    }
    if constexpr (std::is_void_v<Result>) {
        future.get();
    } else {
        return future.get();
    }
}
