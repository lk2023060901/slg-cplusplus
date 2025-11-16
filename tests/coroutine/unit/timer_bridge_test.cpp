#include <gtest/gtest.h>

#include <chrono>

#include "coroutine/scheduler.h"
#include "coroutine/timer_bridge.h"
#include "timer/scheduler.h"

namespace {

TEST(CoroutineTimerBridgeTest, SleepForCompletesInFiber) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::timer::Scheduler timer;
    timer.Start();
    slg::coroutine::CoroutineTimerBridge bridge(scheduler, timer);

    auto scheduled = bridge.SleepFor(std::chrono::milliseconds(50));
    EXPECT_EQ(scheduled.future.wait_for(std::chrono::seconds(1)),
              boost::fibers::future_status::ready);
    scheduled.future.get();
    timer.Stop();
    scheduler.Stop();
}

TEST(CoroutineTimerBridgeTest, CancelSleepNotifiesFuture) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::timer::Scheduler timer;
    timer.Start();
    slg::coroutine::CoroutineTimerBridge bridge(scheduler, timer);

    auto scheduled = bridge.SleepFor(std::chrono::milliseconds(500));
    EXPECT_TRUE(bridge.Cancel(scheduled.id));
    EXPECT_EQ(scheduled.future.wait_for(std::chrono::seconds(1)),
              boost::fibers::future_status::ready);
    EXPECT_THROW({ scheduled.future.get(); }, std::runtime_error);
    timer.Stop();
    scheduler.Stop();
}

TEST(CoroutineTimerBridgeTest, SleepUntilPastTimeResolvesImmediately) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::timer::Scheduler timer;
    timer.Start();
    slg::coroutine::CoroutineTimerBridge bridge(scheduler, timer);

    auto scheduled = bridge.SleepUntil(std::chrono::steady_clock::now());
    EXPECT_EQ(scheduled.future.wait_for(std::chrono::seconds(1)),
              boost::fibers::future_status::ready);
    scheduled.future.get();

    timer.Stop();
    scheduler.Stop();
}

TEST(CoroutineTimerBridgeTest, CancelAfterCompletionReturnsFalse) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    slg::timer::Scheduler timer;
    timer.Start();
    slg::coroutine::CoroutineTimerBridge bridge(scheduler, timer);

    auto scheduled = bridge.SleepFor(std::chrono::milliseconds(50));
    EXPECT_EQ(scheduled.future.wait_for(std::chrono::seconds(1)),
              boost::fibers::future_status::ready);
    scheduled.future.get();
    EXPECT_FALSE(bridge.Cancel(scheduled.id));

    timer.Stop();
    scheduler.Stop();
}

}  // namespace
