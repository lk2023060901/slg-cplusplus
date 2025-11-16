set(COROUTINE_PERF_TEST_SOURCES
    scheduler_performance_test.cpp
    fiber_tcp_session_performance_test.cpp
    performance_main.cpp
)

add_executable(coroutine_performance_tests ${COROUTINE_PERF_TEST_SOURCES})
target_include_directories(coroutine_performance_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(coroutine_performance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(coroutine_performance_tests
    PRIVATE
        slg_coroutine_static
        slg_tcp_static
        gtest
        Threads::Threads)

add_test(NAME coroutine_performance_tests COMMAND coroutine_performance_tests)
