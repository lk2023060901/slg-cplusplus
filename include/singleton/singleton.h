#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <utility>

#include "singleton/singleton_export.h"

namespace slg::singleton {

struct NullMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
};

namespace detail {
[[noreturn]] SLG_SINGLETON_API void ThrowUninitializedAccess(const char* type_name);
}

template <typename T, typename Mutex = NullMutex>
class Singleton {
public:
    Singleton() = delete;
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    template <typename... Args>
    static T& Instance(Args&&... args) {
        T* instance = instance_ptr_.load(std::memory_order_acquire);
        if (instance == nullptr) {
            CreateInstance(std::forward<Args>(args)...);
            instance = instance_ptr_.load(std::memory_order_acquire);
        }
        return *instance;
    }

    static T& Get() {
        T* instance = instance_ptr_.load(std::memory_order_acquire);
        if (!instance) {
            detail::ThrowUninitializedAccess(typeid(T).name());
        }
        return *instance;
    }

    static bool IsInitialized() noexcept {
        return instance_ptr_.load(std::memory_order_acquire) != nullptr;
    }

    template <typename... Args>
    static void Reset(Args&&... args) {
        std::lock_guard<Mutex> lock(GetMutex());
        instance_holder_.reset();
        instance_ptr_.store(nullptr, std::memory_order_release);
        if constexpr (sizeof...(Args) > 0) {
            instance_holder_ = std::make_unique<T>(std::forward<Args>(args)...);
            instance_ptr_.store(instance_holder_.get(), std::memory_order_release);
        }
    }

    static void Destroy() {
        Reset();
    }

private:
    template <typename... Args>
    static void CreateInstance(Args&&... args) {
        std::lock_guard<Mutex> lock(GetMutex());
        if (!instance_holder_) {
            instance_holder_ = std::make_unique<T>(std::forward<Args>(args)...);
            instance_ptr_.store(instance_holder_.get(), std::memory_order_release);
        }
    }

    static Mutex& GetMutex() {
        static Mutex mutex;
        return mutex;
    }

    inline static std::unique_ptr<T> instance_holder_;
    inline static std::atomic<T*> instance_ptr_{nullptr};
};

}  // namespace slg::singleton
