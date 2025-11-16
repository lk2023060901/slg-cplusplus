set(LOGIN_SOURCES
    src/main.cpp
    src/login_service.cpp
    src/player_login_handler.cpp
    src/internal_service_handler.cpp
    ${PROJECT_SOURCE_DIR}/src/algorithms/snowflake/snowflake_id.cpp
)

set(LOGIN_PROTO_FILES
    ${PROJECT_SOURCE_DIR}/protos/client/login/login.proto
    ${PROJECT_SOURCE_DIR}/protos/client/enums.proto
    ${PROJECT_SOURCE_DIR}/protos/common/error_code.proto
    ${PROJECT_SOURCE_DIR}/protos/common/common.proto
)

set(LOGIN_PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(LOGIN_PROTO_SOURCES "")

foreach(PROTO_FILE ${LOGIN_PROTO_FILES})
    get_filename_component(_proto_name ${PROTO_FILE} NAME_WE)
    file(RELATIVE_PATH _proto_rel_path ${PROJECT_SOURCE_DIR}/protos ${PROTO_FILE})
    get_filename_component(_proto_rel_dir ${_proto_rel_path} DIRECTORY)
    if(_proto_rel_dir STREQUAL "")
        set(_proto_rel_dir ".")
    endif()
    set(_proto_output_dir ${LOGIN_PROTO_GEN_DIR}/${_proto_rel_dir})
    set(_proto_src ${_proto_output_dir}/${_proto_name}.pb.cc)
    set(_proto_hdr ${_proto_output_dir}/${_proto_name}.pb.h)
    add_custom_command(
        OUTPUT ${_proto_src} ${_proto_hdr}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_proto_output_dir}
        COMMAND $<TARGET_FILE:protobuf::protoc>
                --cpp_out=${LOGIN_PROTO_GEN_DIR}
                --proto_path=${PROJECT_SOURCE_DIR}/protos
                ${PROTO_FILE}
        DEPENDS ${PROTO_FILE} protobuf::protoc
        VERBATIM
    )
    list(APPEND LOGIN_PROTO_SOURCES ${_proto_src})
    list(APPEND LOGIN_PROTO_SOURCES ${_proto_hdr})
endforeach()

add_executable(slg_login
    ${LOGIN_SOURCES}
    ${LOGIN_PROTO_SOURCES}
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
        ${LOGIN_PROTO_GEN_DIR}
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

if(TARGET slg_http_static)
    set(_http_target slg_http_static)
elseif(TARGET slg_http_shared)
    set(_http_target slg_http_shared)
else()
    message(FATAL_ERROR "No slg_http target available for login service")
endif()

if(TARGET slg_citus::static)
    set(_citus_target slg_citus::static)
elseif(TARGET slg_citus::shared)
    set(_citus_target slg_citus::shared)
else()
    message(FATAL_ERROR "No slg_citus target available for login service")
endif()

if(TARGET slg_coroutine::static)
    set(_coroutine_target slg_coroutine::static)
elseif(TARGET slg_coroutine::shared)
    set(_coroutine_target slg_coroutine::shared)
else()
    message(FATAL_ERROR "No slg_coroutine target available for login service")
endif()

set(_login_link_targets ${_application_target} ${_http_target} ${_citus_target} ${_coroutine_target} protobuf::libprotobuf Threads::Threads)
if(NOT _logging_target STREQUAL "")
    list(APPEND _login_link_targets ${_logging_target})
endif()

target_link_libraries(slg_login PRIVATE ${_login_link_targets})

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
