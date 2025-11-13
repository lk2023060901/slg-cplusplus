#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace slg::application {

class DependencyContainer {
public:
    template <typename T>
    void Register(std::shared_ptr<T> instance, std::string_view key = {}) {
        const DependencyKey dep_key{std::type_index(typeid(T)), std::string(key)};
        storage_[dep_key] = std::move(instance);
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> Emplace(std::string_view key, Args&&... args) {
        auto instance = std::make_shared<T>(std::forward<Args>(args)...);
        Register(instance, key);
        return instance;
    }

    template <typename T>
    bool Contains(std::string_view key = {}) const {
        const DependencyKey dep_key{std::type_index(typeid(T)), std::string(key)};
        return storage_.find(dep_key) != storage_.end();
    }

    template <typename T>
    std::shared_ptr<T> Resolve(std::string_view key = {}) const {
        const DependencyKey dep_key{std::type_index(typeid(T)), std::string(key)};
        auto iter = storage_.find(dep_key);
        if (iter == storage_.end()) {
            return nullptr;
        }

        return std::static_pointer_cast<T>(iter->second);
    }

private:
    struct DependencyKey {
        std::type_index type;
        std::string tag;

        bool operator==(const DependencyKey& other) const noexcept {
            return type == other.type && tag == other.tag;
        }
    };

    struct DependencyKeyHash {
        std::size_t operator()(const DependencyKey& key) const noexcept {
            return std::hash<std::type_index>{}(key.type) ^ (std::hash<std::string>{}(key.tag) << 1);
        }
    };

    std::unordered_map<DependencyKey, std::shared_ptr<void>, DependencyKeyHash> storage_;
};

}  // namespace slg::application
