set(COROUTINE_UNIT_TEST_SOURCES
    scheduler_test.cpp
    mailbox_test.cpp
    actor_test.cpp
    actor_manager_test.cpp
    timer_bridge_test.cpp
    asio_bridge_test.cpp
    fiber_tcp_session_error_test.cpp
)

add_executable(coroutine_unit_tests ${COROUTINE_UNIT_TEST_SOURCES})
target_include_directories(coroutine_unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(coroutine_unit_tests
    PRIVATE
        slg_coroutine_static
        gtest
        gtest_main
        Threads::Threads)

if(TARGET slg_timer::static)
    target_link_libraries(coroutine_unit_tests PRIVATE slg_timer::static)
elseif(TARGET slg_timer::shared)
    target_link_libraries(coroutine_unit_tests PRIVATE slg_timer::shared)
endif()

if(TARGET slg_tcp_static)
    target_link_libraries(coroutine_unit_tests PRIVATE slg_tcp_static)
elseif(TARGET slg_tcp_shared)
    target_link_libraries(coroutine_unit_tests PRIVATE slg_tcp_shared)
endif()

add_test(NAME coroutine_unit_tests COMMAND coroutine_unit_tests)
