#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "coroutine/mailbox.h"

namespace {

TEST(CoroutineMailboxTest, PushAndPop) {
    slg::coroutine::Mailbox<int> mailbox(4);
    std::vector<std::thread> producers;
    for (int i = 0; i < 3; ++i) {
        producers.emplace_back([&mailbox, i]() { EXPECT_TRUE(mailbox.Push(i)); });
    }
    for (auto& thread : producers) {
        thread.join();
    }

    for (int expected = 0; expected < 3; ++expected) {
        int value = -1;
        EXPECT_TRUE(mailbox.WaitPop(value));
    }
}

TEST(CoroutineMailboxTest, StopUnblocksWaiters) {
    slg::coroutine::Mailbox<int> mailbox(1);
    std::thread stopper([&mailbox]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mailbox.Stop();
    });

    int value = 0;
    EXPECT_FALSE(mailbox.WaitPop(value));
    stopper.join();
}

}  // namespace
