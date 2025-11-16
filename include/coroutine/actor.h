#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "coroutine/coroutine_export.h"
#include "coroutine/mailbox.h"
#include "coroutine/scheduler.h"

namespace slg::coroutine {

class SLG_COROUTINE_API Actor : public std::enable_shared_from_this<Actor> {
public:
    using Message = std::function<void(Actor&)>;

    Actor(CoroutineScheduler& scheduler, std::string name = std::string(),
          std::size_t mailbox_capacity = 1024);
    virtual ~Actor();

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    void Start();
    void Stop();
    bool Post(Message message);
    bool Running() const;

    const std::string& Name() const {
        return name_;
    }

protected:
    virtual void OnStart();
    virtual void OnStop();
    virtual void OnError(std::exception_ptr error);

private:
    void Run();

    CoroutineScheduler& scheduler_;
    const std::string name_;
    Mailbox<Message> mailbox_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::mutex lifecycle_mutex_;
};

using ActorPtr = std::shared_ptr<Actor>;

}  // namespace slg::coroutine
