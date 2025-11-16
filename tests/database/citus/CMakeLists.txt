set(CITUS_TEST_SOURCES
    citus_connection_test.cpp
)

add_executable(citus_connection_tests ${CITUS_TEST_SOURCES})
target_include_directories(citus_connection_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(citus_connection_tests
    PRIVATE
        slg_citus_static
        gtest
        gtest_main
)

add_test(NAME citus_connection_tests COMMAND citus_connection_tests)
