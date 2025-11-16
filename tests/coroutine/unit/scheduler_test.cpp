#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <vector>

#include "coroutine/scheduler.h"

namespace {

TEST(CoroutineSchedulerTest, ExecutesScheduledTasks) {
    slg::coroutine::CoroutineScheduler scheduler(2);
    auto future_a = scheduler.Schedule([] { return 42; });
    auto future_b = scheduler.Schedule([] { return 18; });

    EXPECT_EQ(42, future_a.get());
    EXPECT_EQ(18, future_b.get());
    scheduler.Stop();
}

TEST(CoroutineSchedulerTest, PropagatesExceptionsViaFuture) {
    slg::coroutine::CoroutineScheduler scheduler(1);
    auto future = scheduler.Schedule([]() -> int {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(future.get(), std::runtime_error);
    scheduler.Stop();
}

TEST(CoroutineSchedulerTest, HandlesManyTasks) {
    slg::coroutine::CoroutineScheduler scheduler(3);
    constexpr std::size_t kTaskCount = 1000;
    std::vector<boost::fibers::future<int>> futures;
    futures.reserve(kTaskCount);
    for (std::size_t i = 0; i < kTaskCount; ++i) {
        futures.push_back(scheduler.Schedule([i]() { return static_cast<int>(i); }));
    }
    for (std::size_t i = 0; i < kTaskCount; ++i) {
        EXPECT_EQ(static_cast<int>(i), futures[i].get());
    }
    scheduler.Stop();
}

}  // namespace
