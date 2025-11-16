#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "coroutine/actor.h"
#include "coroutine/actor_manager.h"
#include "coroutine/scheduler.h"

namespace {

class KeyedActor : public slg::coroutine::Actor {
public:
    KeyedActor(slg::coroutine::CoroutineScheduler& scheduler, int key)
        : slg::coroutine::Actor(scheduler, "keyed-actor"), key_(key) {}

    int Key() const { return key_; }

    void Increment() {
        Post([](slg::coroutine::Actor& actor) {
            auto* self = static_cast<KeyedActor*>(&actor);
            ++self->counter_;
        });
    }

    int Counter() const { return counter_; }

private:
    int key_{0};
    std::atomic<int> counter_{0};
};

TEST(CoroutineActorManagerTest, RegisterFindRemove) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::coroutine::ActorManager<int> manager;

    auto actor = std::make_shared<KeyedActor>(scheduler, 1);
    actor->Start();
    EXPECT_TRUE(manager.Register(actor->Key(), actor));
    auto found = manager.Find(actor->Key());
    ASSERT_NE(found, nullptr);
    auto derived = std::dynamic_pointer_cast<KeyedActor>(found);
    ASSERT_NE(derived, nullptr);
    derived->Increment();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(actor->Counter(), 1);

    EXPECT_TRUE(manager.Remove(actor->Key()));
    actor->Stop();
    scheduler.Stop();
}

TEST(CoroutineActorManagerTest, RegisterDuplicateFails) {
    slg::coroutine::CoroutineScheduler scheduler(1);
    slg::coroutine::ActorManager<int> manager;
    auto actor1 = std::make_shared<KeyedActor>(scheduler, 1);
    auto actor2 = std::make_shared<KeyedActor>(scheduler, 1);
    actor1->Start();
    actor2->Start();
    EXPECT_TRUE(manager.Register(actor1->Key(), actor1));
    EXPECT_FALSE(manager.Register(actor2->Key(), actor2));
    actor1->Stop();
    actor2->Stop();
    scheduler.Stop();
}

TEST(CoroutineActorManagerTest, ForEachVisitsAllActors) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::coroutine::ActorManager<int> manager;
    constexpr int kCount = 10;
    for (int i = 0; i < kCount; ++i) {
        auto actor = std::make_shared<KeyedActor>(scheduler, i);
        actor->Start();
        EXPECT_TRUE(manager.Register(actor->Key(), actor));
    }
    int visited = 0;
    manager.ForEach([&visited](const int&, const std::shared_ptr<slg::coroutine::Actor>&) {
        ++visited;
    });
    EXPECT_EQ(visited, kCount);
    manager.ForEach([](const int&, const std::shared_ptr<slg::coroutine::Actor>& actor) {
        actor->Stop();
    });
    scheduler.Stop();
}

}  // namespace
