#include "database/redis/redis_client.h"

#include <chrono>
#include <iterator>
#include <stdexcept>

#include <boost/fiber/operations.hpp>

namespace slg::database::redis {

namespace {

template <typename T>
std::optional<T> ToStdOptional(const sw::redis::Optional<T>& value) {
    if (value) {
        return *value;
    }
    return std::nullopt;
}

}  // namespace

RedisClient::RedisClient(RedisConfig config)
    : RedisClient(std::move(config), nullptr) {}

RedisClient::RedisClient(RedisConfig config,
                         std::shared_ptr<slg::coroutine::CoroutineScheduler> scheduler)
    : config_(std::move(config)), scheduler_(std::move(scheduler)) {
    if (!scheduler_) {
        scheduler_ = std::make_shared<slg::coroutine::CoroutineScheduler>();
        owns_scheduler_ = true;
    }
    event_loop_ = std::make_shared<sw::redis::EventLoop>();
}

RedisClient::~RedisClient() {
    Unsubscribe();
}

bool RedisClient::Connect() {
    if (!event_loop_) {
        event_loop_ = std::make_shared<sw::redis::EventLoop>();
    }

    if (config_.cluster_mode) {
        auto pool_options = BuildPoolOptions();
        async_cluster_.reset();
        sync_cluster_.reset();
        for (const auto& endpoint : config_.endpoints) {
            try {
                async_cluster_ = std::make_shared<sw::redis::AsyncRedisCluster>(
                    BuildConnectionOptions(endpoint), pool_options, sw::redis::Role::MASTER,
                    event_loop_);
                sync_cluster_ = std::make_shared<sw::redis::RedisCluster>(
                    BuildConnectionOptions(endpoint), pool_options);
                break;
            } catch (const sw::redis::Error&) {
                async_cluster_.reset();
                sync_cluster_.reset();
            }
        }
        return async_cluster_ != nullptr && sync_cluster_ != nullptr;
    }

    if (config_.endpoints.empty()) {
        return false;
    }

    auto options = BuildConnectionOptions(config_.endpoints.front());
    auto pool_options = BuildPoolOptions();

    try {
        async_redis_ = std::make_shared<sw::redis::AsyncRedis>(options, pool_options, event_loop_);
        sync_redis_ = std::make_shared<sw::redis::Redis>(options, pool_options);
    } catch (const sw::redis::Error&) {
        async_redis_.reset();
        sync_redis_.reset();
        return false;
    }

    return true;
}

bool RedisClient::EnsureConnected() {
    if (config_.cluster_mode) {
        if (!async_cluster_ || !sync_cluster_) {
            return Connect();
        }
    } else {
        if (!async_redis_ || !sync_redis_) {
            return Connect();
        }
    }
    return true;
}

bool RedisClient::Set(std::string_view key,
                      std::string_view value,
                      std::chrono::milliseconds ttl) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode
                      ? async_cluster_->set(key, value, ttl, sw::redis::UpdateType::ALWAYS)
                      : async_redis_->set(key, value, ttl, sw::redis::UpdateType::ALWAYS);
    return WaitFuture(std::move(future));
}

std::optional<std::string> RedisClient::Get(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto future = config_.cluster_mode ? async_cluster_->get(key) : async_redis_->get(key);
    return ToStdOptional(WaitFuture(std::move(future)));
}

bool RedisClient::Del(std::string_view key) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->del(key) : async_redis_->del(key);
    return WaitFuture(std::move(future)) > 0;
}

bool RedisClient::AcquireLock(std::string_view key,
                               std::string_view token,
                               std::chrono::milliseconds ttl,
                               std::chrono::milliseconds retry_interval,
                               std::size_t max_retry) {
    if (!EnsureConnected()) {
        return false;
    }

    for (std::size_t attempt = 0; attempt <= max_retry; ++attempt) {
        auto future = config_.cluster_mode
                          ? async_cluster_->set(key, token, ttl, sw::redis::UpdateType::NOT_EXIST)
                          : async_redis_->set(key, token, ttl, sw::redis::UpdateType::NOT_EXIST);
        auto acquired = WaitFuture(std::move(future));

        if (acquired) {
            return true;
        }

        if (attempt < max_retry) {
            boost::this_fiber::sleep_for(retry_interval);
        }
    }

    return false;
}

bool RedisClient::ReleaseLock(std::string_view key, std::string_view token) {
    if (!EnsureConnected()) {
        return false;
    }
    static const char* lua_script =
        "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end";

    std::vector<std::string> keys{std::string(key)};
    std::vector<std::string> args{std::string(token)};
    auto future = config_.cluster_mode
                      ? async_cluster_->eval<long long>(lua_script, keys.begin(), keys.end(),
                                                        args.begin(), args.end())
                      : async_redis_->eval<long long>(lua_script, keys.begin(), keys.end(),
                                                      args.begin(), args.end());
    return WaitFuture(std::move(future)) > 0;
}

sw::redis::ConnectionOptions RedisClient::BuildConnectionOptions(const RedisEndpoint& endpoint) const {
    sw::redis::ConnectionOptions options;
    options.host = endpoint.host;
    options.port = endpoint.port;
    options.db = endpoint.db;
    options.socket_timeout = config_.socket_timeout;
    options.connect_timeout = config_.connect_timeout;
    if (endpoint.password.has_value()) {
        options.password = *endpoint.password;
    } else if (config_.password.has_value()) {
        options.password = *config_.password;
    }
    if (config_.username.has_value()) {
        options.user = *config_.username;
    }
    return options;
}

sw::redis::ConnectionPoolOptions RedisClient::BuildPoolOptions() const {
    sw::redis::ConnectionPoolOptions pool;
    pool.size = config_.pool_size;
    pool.wait_timeout = config_.socket_timeout;
    return pool;
}

bool RedisClient::Exists(std::string_view key) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->exists(key) : async_redis_->exists(key);
    return WaitFuture(std::move(future)) > 0;
}

bool RedisClient::Expire(std::string_view key, std::chrono::seconds ttl) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->expire(key, ttl)
                                       : async_redis_->expire(key, ttl);
    return WaitFuture(std::move(future));
}

bool RedisClient::HSet(std::string_view key, std::string_view field, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->hset(key, field, value)
                                       : async_redis_->hset(key, field, value);
    return WaitFuture(std::move(future)) >= 0;
}

std::optional<std::string> RedisClient::HGet(std::string_view key, std::string_view field) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto future = config_.cluster_mode ? async_cluster_->hget(key, field)
                                       : async_redis_->hget(key, field);
    return ToStdOptional(WaitFuture(std::move(future)));
}

bool RedisClient::HDel(std::string_view key, std::string_view field) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->hdel(key, field)
                                       : async_redis_->hdel(key, field);
    return WaitFuture(std::move(future)) > 0;
}

std::unordered_map<std::string, std::string> RedisClient::HGetAll(std::string_view key) {
    if (!EnsureConnected()) {
        return {};
    }
    auto future =
        config_.cluster_mode
            ? async_cluster_->hgetall<std::unordered_map<std::string, std::string>>(key)
            : async_redis_->hgetall<std::unordered_map<std::string, std::string>>(key);
    return WaitFuture(std::move(future));
}

bool RedisClient::LPush(std::string_view key, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->lpush(key, value)
                                       : async_redis_->lpush(key, value);
    return WaitFuture(std::move(future)) > 0;
}

bool RedisClient::RPush(std::string_view key, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->rpush(key, value)
                                       : async_redis_->rpush(key, value);
    return WaitFuture(std::move(future)) > 0;
}

std::optional<std::string> RedisClient::LPop(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto future = config_.cluster_mode ? async_cluster_->lpop(key) : async_redis_->lpop(key);
    return ToStdOptional(WaitFuture(std::move(future)));
}

std::optional<std::string> RedisClient::RPop(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto future = config_.cluster_mode ? async_cluster_->rpop(key) : async_redis_->rpop(key);
    return ToStdOptional(WaitFuture(std::move(future)));
}

bool RedisClient::ExecutePipeline(const PipelineHandler& handler, std::string_view hash_tag) {
    if (!EnsureConnected() || !handler) {
        return false;
    }
    try {
        if (config_.cluster_mode) {
            if (!sync_cluster_ || hash_tag.empty()) {
                return false;
            }
            auto pipe = sync_cluster_->pipeline(hash_tag);
            handler(pipe);
            pipe.exec();
            return true;
        }
        if (!sync_redis_) {
            return false;
        }
        auto pipe = sync_redis_->pipeline();
        handler(pipe);
        pipe.exec();
        return true;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

std::optional<std::string> RedisClient::Eval(const std::string& script,
                                             const std::vector<std::string>& keys,
                                             const std::vector<std::string>& args) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto future =
        config_.cluster_mode
            ? async_cluster_->eval<sw::redis::OptionalString>(script, keys.begin(), keys.end(),
                                                              args.begin(), args.end())
            : async_redis_->eval<sw::redis::OptionalString>(script, keys.begin(), keys.end(),
                                                            args.begin(), args.end());
    return ToStdOptional(WaitFuture(std::move(future)));
}

bool RedisClient::Publish(std::string_view channel, std::string_view message) {
    if (!EnsureConnected()) {
        return false;
    }
    auto future = config_.cluster_mode ? async_cluster_->publish(channel, message)
                                       : async_redis_->publish(channel, message);
    return WaitFuture(std::move(future)) > 0;
}

bool RedisClient::Subscribe(const std::vector<std::string>& channels, PubSubCallback callback) {
    if (!EnsureConnected() || channels.empty() || !callback) {
        return false;
    }

    Unsubscribe();

    std::lock_guard<std::mutex> lock(subscriber_mutex_);
    try {
        if (config_.cluster_mode) {
            subscriber_ = std::make_unique<sw::redis::AsyncSubscriber>(async_cluster_->subscriber());
        } else {
            subscriber_ = std::make_unique<sw::redis::AsyncSubscriber>(async_redis_->subscriber());
        }
        subscriber_callback_ = callback;
        subscriber_->on_message([this](std::string channel, std::string msg) {
            auto handler = subscriber_callback_;
            if (!handler) {
                return;
            }
            scheduler_->Schedule([handler, channel = std::move(channel), msg = std::move(msg)]() mutable {
                handler(channel, msg);
            });
        });
        for (const auto& ch : channels) {
            WaitFuture(subscriber_->subscribe(ch));
        }
    } catch (...) {
        subscriber_.reset();
        subscriber_callback_ = nullptr;
        return false;
    }
    return true;
}

bool RedisClient::Unsubscribe() {
    std::lock_guard<std::mutex> lock(subscriber_mutex_);
    if (subscriber_) {
        try {
            WaitFuture(subscriber_->unsubscribe());
        } catch (...) {
        }
    }
    subscriber_.reset();
    subscriber_callback_ = nullptr;
    return true;
}

std::optional<std::string> RedisClient::XAdd(std::string_view stream,
                                             const std::map<std::string, std::string>& values,
                                             std::optional<std::size_t> max_len,
                                             bool exact_trim) {
    if (!EnsureConnected() || values.empty()) {
        return std::nullopt;
    }

    auto execute = [&](auto& client) -> std::optional<std::string> {
        try {
            if (max_len && *max_len > 0) {
                return client.xadd(stream, "*", values.begin(), values.end(),
                                   static_cast<long long>(*max_len), !exact_trim);
            }
            return client.xadd(stream, "*", values.begin(), values.end());
        } catch (const sw::redis::Error&) {
            return std::nullopt;
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return std::nullopt;
        }
        return execute(*sync_cluster_);
    }

    if (!sync_redis_) {
        return std::nullopt;
    }
    return execute(*sync_redis_);
}

RedisClient::StreamEntries RedisClient::XRead(const std::vector<StreamKey>& streams,
                                              std::chrono::milliseconds timeout,
                                              std::size_t count) {
    StreamEntries entries;
    if (!EnsureConnected() || streams.empty()) {
        return entries;
    }

    auto read = [&](auto& client, const StreamKey& stream) {
        std::vector<std::pair<std::string, std::map<std::string, std::string>>> messages;
        try {
            const std::string start_id = stream.second.empty() ? "-" : std::string(stream.second);
            if (count > 0) {
                client.xrange(stream.first, start_id, "+", static_cast<long long>(count),
                              std::back_inserter(messages));
            } else {
                client.xrange(stream.first, start_id, "+", std::back_inserter(messages));
            }
        } catch (const sw::redis::Error&) {
            messages.clear();
        }
        for (auto& msg : messages) {
            entries.emplace_back(std::move(msg));
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return entries;
        }
        for (const auto& stream : streams) {
            read(*sync_cluster_, stream);
        }
        return entries;
    }

    if (!sync_redis_) {
        return entries;
    }
    for (const auto& stream : streams) {
        read(*sync_redis_, stream);
    }
    return entries;
}

RedisClient::StreamEntries RedisClient::XReadGroup(const std::string& group,
                                                   const std::string& consumer,
                                                   const std::vector<StreamKey>& streams,
                                                   std::chrono::milliseconds timeout,
                                                   std::size_t count) {
    StreamEntries entries;
    if (!EnsureConnected() || streams.empty() || group.empty() || consumer.empty()) {
        return entries;
    }

    auto read_group = [&](auto& client) {
        for (const auto& stream : streams) {
            try {
                std::vector<std::pair<std::string,
                                      std::vector<std::pair<std::string,
                                                            std::map<std::string, std::string>>>>>
                    messages;
                std::chrono::milliseconds block = timeout;
                if (block.count() < 0) {
                    block = std::chrono::milliseconds(0);
                }
                const long long max_count = count > 0 ? static_cast<long long>(count) : 1;
                const std::string id = stream.second.empty() ? ">" : stream.second;
                client.xreadgroup(group, consumer, stream.first, id, block, max_count, false,
                                  std::back_inserter(messages));
                for (auto& message_set : messages) {
                    for (auto& message : message_set.second) {
                        entries.emplace_back(std::move(message));
                    }
                }
            } catch (const sw::redis::Error&) {
                entries.clear();
                break;
            }
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return entries;
        }
        read_group(*sync_cluster_);
        return entries;
    }

    if (!sync_redis_) {
        return entries;
    }
    read_group(*sync_redis_);
    return entries;
}

bool RedisClient::XAck(std::string_view stream,
                       std::string_view group,
                       const std::vector<std::string>& ids) {
    if (!EnsureConnected() || ids.empty()) {
        return false;
    }

    auto ack = [&](auto& client) -> bool {
        try {
            auto removed = client.xack(stream, group, ids.begin(), ids.end());
            return removed > 0;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return false;
        }
        return ack(*sync_cluster_);
    }

    if (!sync_redis_) {
        return false;
    }
    return ack(*sync_redis_);
}

bool RedisClient::XGroupCreate(std::string_view stream,
                               std::string_view group,
                               std::string_view id,
                               bool mkstream) {
    if (!EnsureConnected()) {
        return false;
    }

    auto create = [&](auto& client) -> bool {
        try {
            client.xgroup_create(stream, group, id, mkstream);
            return true;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return false;
        }
        return create(*sync_cluster_);
    }

    if (!sync_redis_) {
        return false;
    }
    return create(*sync_redis_);
}

bool RedisClient::XGroupDestroy(std::string_view stream, std::string_view group) {
    if (!EnsureConnected()) {
        return false;
    }

    auto destroy = [&](auto& client) -> bool {
        try {
            return client.xgroup_destroy(stream, group) > 0;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };

    if (config_.cluster_mode) {
        if (!sync_cluster_) {
            return false;
        }
        return destroy(*sync_cluster_);
    }

    if (!sync_redis_) {
        return false;
    }
    return destroy(*sync_redis_);
}

bool RedisClient::ExecuteTransaction(const TransactionHandler& handler,
                                     std::string_view hash_tag,
                                     bool piped) {
    if (!EnsureConnected() || !handler) {
        return false;
    }
    try {
        if (config_.cluster_mode) {
            if (!sync_cluster_ || hash_tag.empty()) {
                return false;
            }
            auto tx = sync_cluster_->transaction(hash_tag, piped, true);
            handler(tx);
            tx.exec();
            return true;
        }
        if (!sync_redis_) {
            return false;
        }
        auto tx = sync_redis_->transaction(piped, true);
        handler(tx);
        tx.exec();
        return true;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

}  // namespace slg::database::redis
