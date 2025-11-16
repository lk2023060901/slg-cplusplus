if(NOT SLG_BUILD_TESTS)
    return()
endif()

find_package(Threads REQUIRED)

add_subdirectory(coroutine)
add_subdirectory(network)
add_subdirectory(database)
