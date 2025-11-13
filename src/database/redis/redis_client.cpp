#include "database/redis/redis_client.h"

#include <chrono>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace slg::database::redis {

RedisClient::RedisClient(RedisConfig config) : config_(std::move(config)) {}

RedisClient::~RedisClient() {
    Unsubscribe();
}

bool RedisClient::Connect() {
    if (config_.cluster_mode) {
        auto pool_options = BuildPoolOptions();
        for (const auto& endpoint : config_.endpoints) {
            try {
                cluster_ = std::make_shared<sw::redis::RedisCluster>(BuildConnectionOptions(endpoint),
                                                                     pool_options);
                break;
            } catch (const sw::redis::Error&) {
                cluster_.reset();
            }
        }
        if (!cluster_) {
            return false;
        }
    } else {
        if (config_.endpoints.empty()) {
            return false;
        }
        auto options = BuildConnectionOptions(config_.endpoints.front());
        auto pool_options = BuildPoolOptions();
        try {
            redis_ = std::make_shared<sw::redis::Redis>(options, pool_options);
        } catch (const sw::redis::Error&) {
            redis_.reset();
            return false;
        }
    }
    return true;
}

bool RedisClient::EnsureConnected() {
    if (config_.cluster_mode) {
        if (!cluster_) {
            return Connect();
        }
    } else {
        if (!redis_) {
            return Connect();
        }
    }
    return true;
}

std::shared_ptr<sw::redis::Redis> RedisClient::Standalone() {
    EnsureConnected();
    return redis_;
}

std::shared_ptr<sw::redis::RedisCluster> RedisClient::Cluster() {
    EnsureConnected();
    return cluster_;
}

bool RedisClient::Set(std::string_view key,
                      std::string_view value,
                      std::chrono::milliseconds ttl) {
    if (!EnsureConnected()) {
        return false;
    }

    try {
        auto writer = [ttl](auto& redis, std::string_view k, std::string_view v) {
            if (ttl.count() > 0) {
                redis.set(k, v, ttl);
            } else {
                redis.set(k, v);
            }
            return true;
        };

        if (config_.cluster_mode) {
            if (!cluster_) {
                return false;
            }
            writer(*cluster_, key, value);
        } else {
            if (!redis_) {
                return false;
            }
            writer(*redis_, key, value);
        }
    } catch (const sw::redis::Error&) {
        return false;
    }
    return true;
}

std::optional<std::string> RedisClient::Get(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    try {
        if (config_.cluster_mode) {
            return cluster_->get(key);
        }
        return redis_->get(key);
    } catch (const sw::redis::Error&) {
        return std::nullopt;
    }
}

bool RedisClient::Del(std::string_view key) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        std::size_t removed = 0;
        if (config_.cluster_mode) {
            removed = cluster_->del(key);
        } else {
            removed = redis_->del(key);
        }
        return removed > 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

bool RedisClient::AcquireLock(std::string_view key,
                              std::string_view token,
                              std::chrono::milliseconds ttl,
                              std::chrono::milliseconds retry_interval,
                              std::size_t max_retry) {
    if (!EnsureConnected()) {
        return false;
    }

    auto try_set = [&](auto& redis) {
        for (std::size_t attempt = 0; attempt <= max_retry; ++attempt) {
            try {
                auto ok = redis.set(key, token, ttl, sw::redis::UpdateType::NOT_EXIST);
                if (ok) {
                    return true;
                }
            } catch (const sw::redis::Error&) {
                return false;
            }
            std::this_thread::sleep_for(retry_interval);
        }
        return false;
    };

    if (config_.cluster_mode) {
        return try_set(*cluster_);
    }
    return try_set(*redis_);
}

bool RedisClient::ReleaseLock(std::string_view key, std::string_view token) {
    if (!EnsureConnected()) {
        return false;
    }
    static const char* lua_script =
        "if redis.call('get', KEYS[1]) == ARGV[1] then return redis.call('del', KEYS[1]) else return 0 end";

    auto run_script = [&](auto& redis) {
        try {
            long long removed = redis.template eval<long long>(lua_script, {std::string(key)}, {std::string(token)});
            return removed > 0;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };

    if (config_.cluster_mode) {
        return run_script(*cluster_);
    }
    return run_script(*redis_);
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
    try {
        std::size_t count = 0;
        if (config_.cluster_mode) {
            count = cluster_->exists(key);
        } else {
            count = redis_->exists(key);
        }
        return count > 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

bool RedisClient::Expire(std::string_view key, std::chrono::seconds ttl) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        if (config_.cluster_mode) {
            return cluster_->expire(key, ttl);
        }
        return redis_->expire(key, ttl);
    } catch (const sw::redis::Error&) {
        return false;
    }
}

bool RedisClient::HSet(std::string_view key, std::string_view field, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        long long updated = 0;
        if (config_.cluster_mode) {
            updated = cluster_->hset(key, field, value);
        } else {
            updated = redis_->hset(key, field, value);
        }
        return updated >= 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

std::optional<std::string> RedisClient::HGet(std::string_view key, std::string_view field) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    try {
        sw::redis::OptionalString val;
        if (config_.cluster_mode) {
            val = cluster_->hget(key, field);
        } else {
            val = redis_->hget(key, field);
        }
        if (val) {
            return *val;
        }
        return std::nullopt;
    } catch (const sw::redis::Error&) {
        return std::nullopt;
    }
}

bool RedisClient::HDel(std::string_view key, std::string_view field) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        long long removed = 0;
        if (config_.cluster_mode) {
            removed = cluster_->hdel(key, field);
        } else {
            removed = redis_->hdel(key, field);
        }
        return removed > 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

std::unordered_map<std::string, std::string> RedisClient::HGetAll(std::string_view key) {
    std::unordered_map<std::string, std::string> result;
    if (!EnsureConnected()) {
        return result;
    }
    try {
        auto inserter = std::inserter(result, result.begin());
        if (config_.cluster_mode) {
            cluster_->hgetall(key, inserter);
        } else {
            redis_->hgetall(key, inserter);
        }
    } catch (const sw::redis::Error&) {
        result.clear();
    }
    return result;
}

bool RedisClient::LPush(std::string_view key, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        long long count = 0;
        if (config_.cluster_mode) {
            count = cluster_->lpush(key, value);
        } else {
            count = redis_->lpush(key, value);
        }
        return count > 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

bool RedisClient::RPush(std::string_view key, std::string_view value) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        long long count = 0;
        if (config_.cluster_mode) {
            count = cluster_->rpush(key, value);
        } else {
            count = redis_->rpush(key, value);
        }
        return count > 0;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

std::optional<std::string> RedisClient::LPop(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    try {
        sw::redis::OptionalString val;
        if (config_.cluster_mode) {
            val = cluster_->lpop(key);
        } else {
            val = redis_->lpop(key);
        }
        if (val) {
            return *val;
        }
    } catch (const sw::redis::Error&) {
    }
    return std::nullopt;
}

std::optional<std::string> RedisClient::RPop(std::string_view key) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    try {
        sw::redis::OptionalString val;
        if (config_.cluster_mode) {
            val = cluster_->rpop(key);
        } else {
            val = redis_->rpop(key);
        }
        if (val) {
            return *val;
        }
    } catch (const sw::redis::Error&) {
    }
    return std::nullopt;
}

bool RedisClient::ExecutePipeline(const PipelineHandler& handler, std::string_view hash_tag) {
    if (!EnsureConnected() || !handler) {
        return false;
    }
    try {
        if (config_.cluster_mode) {
            if (hash_tag.empty()) {
                return false;
            }
            auto pipe = cluster_->pipeline(hash_tag);
            handler(pipe);
            pipe.exec();
        } else {
            auto pipe = redis_->pipeline();
            handler(pipe);
            pipe.exec();
        }
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
    auto run = [&](auto& client) -> std::optional<std::string> {
        try {
            auto result = client.template eval<sw::redis::OptionalString>(script, keys.begin(), keys.end(), args.begin(), args.end());
            if (result) {
                return *result;
            }
        } catch (const sw::redis::Error&) {
        }
        return std::nullopt;
    };

    if (config_.cluster_mode) {
        return run(*cluster_);
    }
    return run(*redis_);
}

bool RedisClient::Publish(std::string_view channel, std::string_view message) {
    if (!EnsureConnected()) {
        return false;
    }
    try {
        if (config_.cluster_mode) {
            cluster_->publish(channel, message);
        } else {
            redis_->publish(channel, message);
        }
        return true;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

bool RedisClient::Subscribe(const std::vector<std::string>& channels, PubSubCallback callback) {
    if (!EnsureConnected() || channels.empty() || !callback) {
        return false;
    }

    Unsubscribe();

    std::lock_guard<std::mutex> lock(subscriber_mutex_);
    try {
        if (config_.cluster_mode) {
            subscriber_ = std::make_unique<sw::redis::Subscriber>(cluster_->subscriber());
        } else {
            subscriber_ = std::make_unique<sw::redis::Subscriber>(redis_->subscriber());
        }
        for (const auto& ch : channels) {
            subscriber_->subscribe(ch);
        }
        subscriber_callback_ = callback;
        subscriber_->on_message([this](std::string channel, std::string msg) {
            if (subscriber_callback_) {
                subscriber_callback_(channel, msg);
            }
        });
        subscriber_running_ = true;
        subscriber_thread_ = std::thread([this]() {
            while (subscriber_running_) {
                try {
                    subscriber_->consume();
                } catch (const sw::redis::Error&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        });
    } catch (const sw::redis::Error&) {
        subscriber_running_ = false;
        subscriber_.reset();
        subscriber_callback_ = nullptr;
        return false;
    }
    return true;
}

bool RedisClient::Unsubscribe() {
    std::lock_guard<std::mutex> lock(subscriber_mutex_);
    if (subscriber_running_) {
        subscriber_running_ = false;
    }
    if (subscriber_) {
        try {
            subscriber_->unsubscribe();
        } catch (const sw::redis::Error&) {
        }
    }
    if (subscriber_thread_.joinable()) {
        subscriber_thread_.join();
    }
    subscriber_.reset();
    subscriber_callback_ = nullptr;
    return true;
}

std::optional<std::string> RedisClient::XAdd(std::string_view stream,
                                             const std::map<std::string, std::string>& values,
                                             std::optional<std::size_t> max_len,
                                             bool exact_trim) {
    if (!EnsureConnected()) {
        return std::nullopt;
    }
    auto exec = [&](auto& client) -> std::optional<std::string> {
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
        return exec(*cluster_);
    }
    return exec(*redis_);
}

RedisClient::StreamEntries RedisClient::XRead(const std::vector<StreamKey>& streams,
                                              std::chrono::milliseconds timeout,
                                              std::size_t count) {
    StreamEntries entries;
    if (!EnsureConnected() || streams.empty()) {
        return entries;
    }

    auto read_single = [&](auto& client, const StreamKey& key_pair) {
        std::vector<std::pair<std::string, std::map<std::string, std::string>>> messages;
        try {
            if (timeout.count() > 0) {
                if (count > 0) {
                    client.xread(key_pair.first, key_pair.second, timeout,
                                 static_cast<long long>(count), std::back_inserter(messages));
                } else {
                    client.xread(key_pair.first, key_pair.second, timeout, std::back_inserter(messages));
                }
            } else if (count > 0) {
                client.xread(key_pair.first, key_pair.second, static_cast<long long>(count),
                             std::back_inserter(messages));
            } else {
                client.xread(key_pair.first, key_pair.second, std::back_inserter(messages));
            }
            for (auto& msg : messages) {
                entries.emplace_back(std::move(msg));
            }
        } catch (const sw::redis::Error&) {
            entries.clear();
        }
    };

    if (config_.cluster_mode) {
        for (const auto& stream : streams) {
            read_single(*cluster_, stream);
        }
    } else {
        for (const auto& stream : streams) {
            read_single(*redis_, stream);
        }
    }

    return entries;
}

bool RedisClient::XAck(std::string_view stream,
                       std::string_view group,
                       const std::vector<std::string>& ids) {
    if (!EnsureConnected() || ids.empty()) {
        return false;
    }
    auto ack = [&](auto& client) {
        try {
            auto removed = client.xack(stream, group, ids.begin(), ids.end());
            return removed > 0;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };
    if (config_.cluster_mode) {
        return ack(*cluster_);
    }
    return ack(*redis_);
}

bool RedisClient::XGroupCreate(std::string_view stream,
                               std::string_view group,
                               std::string_view id,
                               bool mkstream) {
    if (!EnsureConnected()) {
        return false;
    }
    auto create = [&](auto& client) {
        try {
            client.xgroup_create(stream, group, id, mkstream);
            return true;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };
    if (config_.cluster_mode) {
        return create(*cluster_);
    }
    return create(*redis_);
}

bool RedisClient::XGroupDestroy(std::string_view stream, std::string_view group) {
    if (!EnsureConnected()) {
        return false;
    }
    auto destroy = [&](auto& client) {
        try {
            return client.xgroup_destroy(stream, group) > 0;
        } catch (const sw::redis::Error&) {
            return false;
        }
    };
    if (config_.cluster_mode) {
        return destroy(*cluster_);
    }
    return destroy(*redis_);
}

bool RedisClient::ExecuteTransaction(const TransactionHandler& handler,
                                     std::string_view hash_tag,
                                     bool piped) {
    if (!EnsureConnected() || !handler) {
        return false;
    }

    if (config_.cluster_mode) {
        if (hash_tag.empty()) {
            return false;
        }
        try {
            auto tx = cluster_->transaction(hash_tag, piped, true);
            handler(tx);
            tx.exec();
            return true;
        } catch (const sw::redis::Error&) {
            return false;
        }
    }

    try {
        auto tx = redis_->transaction(piped, true);
        handler(tx);
        tx.exec();
        return true;
    } catch (const sw::redis::Error&) {
        return false;
    }
}

}  // namespace slg::database::redis
