set(NETWORK_HTTP_TEST_SOURCES
    http_client_test.cpp
)

add_executable(network_http_unit_tests ${NETWORK_HTTP_TEST_SOURCES})
target_include_directories(network_http_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)

if(TARGET slg_http_static)
    set(_http_target slg_http_static)
elseif(TARGET slg_http_shared)
    set(_http_target slg_http_shared)
else()
    message(FATAL_ERROR "No slg_http target available for HTTP unit tests")
endif()

target_link_libraries(network_http_unit_tests
    PRIVATE
        ${_http_target}
        gtest
        gtest_main
        Threads::Threads
)

if(TARGET slg_coroutine_static)
    target_link_libraries(network_http_unit_tests PRIVATE slg_coroutine_static)
elseif(TARGET slg_coroutine_shared)
    target_link_libraries(network_http_unit_tests PRIVATE slg_coroutine_shared)
endif()

target_compile_definitions(network_http_unit_tests PRIVATE SLG_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")

add_test(NAME network_http_unit_tests COMMAND network_http_unit_tests)

set(NETWORK_HTTP_PERFORMANCE_SOURCES
    performance/performance_main.cpp
    performance/http_load_test.cpp
)

add_executable(network_http_performance_tests ${NETWORK_HTTP_PERFORMANCE_SOURCES})
target_include_directories(network_http_performance_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(network_http_performance_tests
    PRIVATE
        ${_http_target}
        Threads::Threads
        gtest
        gtest_main
)
if(TARGET slg_coroutine_static)
    target_link_libraries(network_http_performance_tests PRIVATE slg_coroutine_static)
elseif(TARGET slg_coroutine_shared)
    target_link_libraries(network_http_performance_tests PRIVATE slg_coroutine_shared)
endif()
add_test(NAME network_http_performance_tests COMMAND network_http_performance_tests)
