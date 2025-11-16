#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "coroutine/actor.h"
#include "coroutine/scheduler.h"

namespace {

class TestActor : public slg::coroutine::Actor {
public:
    explicit TestActor(slg::coroutine::CoroutineScheduler& scheduler)
        : slg::coroutine::Actor(scheduler, "test-actor") {}

    void ExpectMessage() {
        processed_promise_ = std::promise<void>();
        processed_future_ = processed_promise_.get_future();
    }

    std::future<void> ProcessedFuture() {
        return std::move(processed_future_);
    }

    bool ErrorReceived() const {
        return error_called_.load(std::memory_order_acquire);
    }

protected:
    void OnStop() override {
        stop_called_.store(true, std::memory_order_release);
    }

    void OnError(std::exception_ptr) override {
        error_called_.store(true, std::memory_order_release);
    }

public:
    bool StopCalled() const {
        return stop_called_.load(std::memory_order_acquire);
    }

    void ResolveProcessed() {
        processed_promise_.set_value();
    }

private:
    std::atomic<bool> stop_called_{false};
    std::atomic<bool> error_called_{false};
    std::promise<void> processed_promise_;
    std::future<void> processed_future_;
};

TEST(CoroutineActorTest, ProcessesMessagesAndStops) {
    slg::coroutine::CoroutineScheduler scheduler(1);
    auto actor = std::make_shared<TestActor>(scheduler);
    actor->ExpectMessage();
    actor->Start();

    auto processed_future = actor->ProcessedFuture();
    ASSERT_TRUE(actor->Post([](slg::coroutine::Actor& actor_interface) {
        auto* concrete = static_cast<TestActor*>(&actor_interface);
        concrete->ResolveProcessed();
    }));

    EXPECT_EQ(processed_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    actor->Stop();
    scheduler.Stop();
    EXPECT_TRUE(actor->StopCalled());
}

TEST(CoroutineActorTest, CapturesExceptionsFromMessages) {
    slg::coroutine::CoroutineScheduler scheduler(1);
    auto actor = std::make_shared<TestActor>(scheduler);
    actor->Start();

    ASSERT_TRUE(actor->Post([](slg::coroutine::Actor&) {
        throw std::runtime_error("intentional");
    }));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(actor->ErrorReceived());

    actor->Stop();
    scheduler.Stop();
}

}  // namespace
