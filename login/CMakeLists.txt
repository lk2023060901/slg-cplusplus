add_executable(slg_login
    src/main.cpp
)

set_target_properties(slg_login PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(slg_login
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

find_package(Threads REQUIRED)

# Prefer the static variant of the application library; fall back to shared if needed
if(TARGET slg_application::static)
    set(_application_target slg_application::static)
elseif(TARGET slg_application::shared)
    set(_application_target slg_application::shared)
else()
    message(FATAL_ERROR "No slg_application target available for login service")
endif()

if(TARGET slg_logging::static)
    set(_logging_target slg_logging::static)
elseif(TARGET slg_logging::shared)
    set(_logging_target slg_logging::shared)
else()
    set(_logging_target "")
endif()

if(_logging_target STREQUAL "")
    target_link_libraries(slg_login PRIVATE ${_application_target} Threads::Threads)
else()
    target_link_libraries(slg_login PRIVATE ${_application_target} ${_logging_target} Threads::Threads)
endif()

set(LOGIN_GIT_HASH "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE LOGIN_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE LOGIN_GIT_RESULT)
    if(NOT LOGIN_GIT_RESULT EQUAL 0 OR LOGIN_GIT_HASH STREQUAL "")
        set(LOGIN_GIT_HASH "unknown")
    endif()
endif()

set(LOGIN_BUILD_GIT_HASH ${LOGIN_GIT_HASH})

string(TIMESTAMP LOGIN_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
set(LOGIN_BUILD_VERSION ${PROJECT_VERSION})
if(NOT LOGIN_BUILD_VERSION)
    set(LOGIN_BUILD_VERSION "unknown")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/build_info.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/login_build_info.h @ONLY)
