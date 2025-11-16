#pragma once

#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "coroutine/actor.h"

namespace slg::coroutine {

template <typename Key>
class ActorManager {
public:
    using ActorPtr = std::shared_ptr<Actor>;

    bool Register(Key key, ActorPtr actor) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto [it, inserted] = actors_.emplace(std::move(key), std::move(actor));
        return inserted;
    }

    ActorPtr Find(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = actors_.find(key);
        if (it == actors_.end()) {
            return nullptr;
        }
        return it->second;
    }

    bool Remove(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return actors_.erase(key) > 0;
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& [key, actor] : actors_) {
            fn(key, actor);
        }
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, ActorPtr> actors_;
};

}  // namespace slg::coroutine
