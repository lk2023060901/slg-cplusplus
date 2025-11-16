#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "tests/coroutine/performance/performance_config.h"

namespace perf = slg::tests::coroutine::performance;

std::size_t perf::g_fiber_session_client_count = 32;
std::size_t perf::g_fiber_session_messages_per_client = 8;

int main(int argc, char** argv) {
    std::vector<char*> gtest_argv;
    gtest_argv.reserve(argc);
    gtest_argv.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.rfind("--fiber-clients=", 0) == 0) {
            perf::g_fiber_session_client_count = static_cast<std::size_t>(std::stoul(arg.substr(16)));
            continue;
        }
        if (arg.rfind("--fiber-messages=", 0) == 0) {
            perf::g_fiber_session_messages_per_client =
                static_cast<std::size_t>(std::stoul(arg.substr(17)));
            continue;
        }
        gtest_argv.push_back(argv[i]);
    }

    int new_argc = static_cast<int>(gtest_argv.size());
    ::testing::InitGoogleTest(&new_argc, gtest_argv.data());
    return RUN_ALL_TESTS();
}
