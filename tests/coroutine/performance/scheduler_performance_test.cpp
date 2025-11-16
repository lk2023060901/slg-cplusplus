#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "coroutine/scheduler.h"

namespace {

TEST(CoroutineSchedulerPerformanceTest, SchedulesLargeNumberOfTasksQuickly) {
    const std::size_t worker_count = std::max(2u, std::thread::hardware_concurrency());
    slg::coroutine::CoroutineScheduler scheduler(worker_count);
    constexpr std::size_t kTaskCount = 20000;
    std::vector<boost::fibers::future<void>> futures;
    futures.reserve(kTaskCount);

    auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kTaskCount; ++i) {
        futures.push_back(scheduler.Schedule([]() {}));
    }
    for (auto& future : futures) {
        future.get();
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Expect the workload to finish within a reasonable amount of time even under load.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 5);
    scheduler.Stop();
}

}  // namespace
