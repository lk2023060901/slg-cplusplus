#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "network/http/performance/performance_config.h"

namespace perf = slg::tests::network::http::performance;

std::size_t perf::g_http_concurrency = 64;
std::size_t perf::g_http_requests_per_client = 16;

int main(int argc, char** argv) {
    std::vector<char*> gtest_argv;
    gtest_argv.reserve(argc);
    gtest_argv.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg.rfind("--http-clients=", 0) == 0) {
            perf::g_http_concurrency = static_cast<std::size_t>(std::stoul(arg.substr(15)));
            continue;
        }
        if (arg.rfind("--http-requests=", 0) == 0) {
            perf::g_http_requests_per_client = static_cast<std::size_t>(std::stoul(arg.substr(16)));
            continue;
        }
        gtest_argv.push_back(argv[i]);
    }

    int new_argc = static_cast<int>(gtest_argv.size());
    ::testing::InitGoogleTest(&new_argc, gtest_argv.data());
    return RUN_ALL_TESTS();
}
