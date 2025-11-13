#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <boost/thread.hpp>

#include "threading/threading_export.h"

namespace slg::threading {

class Thread {
public:
    Thread() = default;
    explicit Thread(std::string name);

    template <typename Fn, typename... Args>
    explicit Thread(Fn&& fn, Args&&... args) {
        Start(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    template <typename Fn, typename... Args>
    Thread(std::string name, Fn&& fn, Args&&... args) : name_(std::move(name)) {
        Start(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    ~Thread();

    template <typename Fn, typename... Args>
    void Start(Fn&& fn, Args&&... args) {
        if (thread_.joinable()) {
            throw std::logic_error("Thread already running");
        }

        auto bound = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
        const auto name = name_;
        thread_ = boost::thread([task = std::move(bound), name]() mutable {
            if (!name.empty()) {
                Thread::SetCurrentThreadName(name);
            }
            task();
        });
    }

    SLG_THREADING_API void Join();
    SLG_THREADING_API void Detach();
    SLG_THREADING_API bool Joinable() const noexcept;

    SLG_THREADING_API boost::thread::id GetId() const noexcept;

    SLG_THREADING_API void SetName(std::string name);
    SLG_THREADING_API const std::string& GetName() const noexcept;

    SLG_THREADING_API static boost::thread::id CurrentId();
    SLG_THREADING_API static void Yield();

    template <typename Rep, typename Period>
    static void SleepFor(const std::chrono::duration<Rep, Period>& duration) {
        boost::this_thread::sleep_for(duration);
    }

    SLG_THREADING_API static void SetCurrentThreadName(std::string_view name);

private:
    std::string name_;
    boost::thread thread_;
};

}  // namespace slg::threading
