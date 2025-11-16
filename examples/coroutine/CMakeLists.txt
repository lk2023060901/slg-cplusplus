add_executable(fiber_echo_server
    fiber_echo_server/main.cpp
)

target_include_directories(fiber_echo_server PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(fiber_echo_server
    PRIVATE
        slg_coroutine_static
        slg_tcp_static
        Threads::Threads)
