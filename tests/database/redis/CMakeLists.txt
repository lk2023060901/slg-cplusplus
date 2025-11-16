set(REDIS_TEST_SOURCES
    redis_client_test.cpp
    redis_cluster_test.cpp
)

add_executable(redis_client_tests ${REDIS_TEST_SOURCES})
target_include_directories(redis_client_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(redis_client_tests
    PRIVATE
        slg_redis_static
        gtest
        gtest_main
    )

add_test(NAME redis_client_tests COMMAND redis_client_tests)
