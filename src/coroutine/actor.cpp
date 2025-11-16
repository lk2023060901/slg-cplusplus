#include "coroutine/actor.h"

#include <utility>

#include <boost/fiber/operations.hpp>

namespace slg::coroutine {

Actor::Actor(CoroutineScheduler& scheduler, std::string name, std::size_t mailbox_capacity)
    : scheduler_(scheduler), name_(std::move(name)), mailbox_(mailbox_capacity) {}

Actor::~Actor() {
    Stop();
}

void Actor::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (running_) {
        return;
    }
    stopping_ = false;
    running_ = true;
    auto self = shared_from_this();
    scheduler_.Schedule([self]() { self->Run(); });
}

void Actor::Stop() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (!running_) {
            return;
        }
        stopping_ = true;
    }
    mailbox_.Stop();
}

bool Actor::Post(Message message) {
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }
    return mailbox_.Push(std::move(message));
}

bool Actor::Running() const {
    return running_.load(std::memory_order_acquire);
}

void Actor::OnStart() {}

void Actor::OnStop() {}

void Actor::OnError(std::exception_ptr) {}

void Actor::Run() {
    OnStart();
    while (!stopping_.load(std::memory_order_acquire)) {
        Message message;
        if (!mailbox_.WaitPop(message)) {
            continue;
        }
        try {
            message(*this);
        } catch (...) {
            OnError(std::current_exception());
        }
        boost::this_fiber::yield();
    }
    OnStop();
    running_.store(false, std::memory_order_release);
}

}  // namespace slg::coroutine
