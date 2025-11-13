#include "threading/thread.h"

#include <stdexcept>

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
  #include <pthread.h>
#endif

namespace slg::threading {
namespace {

std::string TruncateName(std::string_view name) {
#if defined(__APPLE__)
    constexpr std::size_t kMax = 63;
#elif defined(__linux__)
    constexpr std::size_t kMax = 15;
#else
    constexpr std::size_t kMax = 63;
#endif
    if (name.size() <= kMax) {
        return std::string(name);
    }
    return std::string(name.substr(0, kMax));
}

void SetNativeThreadName(const std::string& name) {
#if defined(_WIN32)
    const auto wide_name = std::wstring(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wide_name.c_str());
#elif defined(__APPLE__)
    pthread_setname_np(name.c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name.c_str());
#else
    (void)name;
#endif
}

}  // namespace

Thread::Thread(std::string name) : name_(std::move(name)) {}

Thread::Thread(Thread&& other) noexcept : name_(std::move(other.name_)), thread_(std::move(other.thread_)) {}

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    name_ = std::move(other.name_);
    thread_ = std::move(other.thread_);
    return *this;
}

Thread::~Thread() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Thread::Join() {
    if (!thread_.joinable()) {
        throw std::logic_error("Thread not joinable");
    }
    thread_.join();
}

void Thread::Detach() {
    if (!thread_.joinable()) {
        throw std::logic_error("Thread not joinable");
    }
    thread_.detach();
}

bool Thread::Joinable() const noexcept {
    return thread_.joinable();
}

boost::thread::id Thread::GetId() const noexcept {
    return thread_.get_id();
}

void Thread::SetName(std::string name) {
    name_ = std::move(name);
    if (!name_.empty() && thread_.joinable()) {
        // Cannot rename an already running thread on macOS, but calling SetCurrentThreadName
        // within the thread body when it starts ensures a name is set.
    }
}

const std::string& Thread::GetName() const noexcept {
    return name_;
}

boost::thread::id Thread::CurrentId() {
    return boost::this_thread::get_id();
}

void Thread::Yield() {
    boost::this_thread::yield();
}

void Thread::SetCurrentThreadName(std::string_view name) {
    if (name.empty()) {
        return;
    }
    SetNativeThreadName(TruncateName(name));
}

}  // namespace slg::threading
