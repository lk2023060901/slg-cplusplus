option(SLG_REDIS_BUILD_STATIC "Build the static variant of slg_redis" ON)
option(SLG_REDIS_BUILD_SHARED "Build the shared variant of slg_redis" ON)

if(NOT SLG_REDIS_BUILD_STATIC AND NOT SLG_REDIS_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_REDIS_BUILD_STATIC or SLG_REDIS_BUILD_SHARED")
endif()

set(HIREDIS_ROOT ${PROJECT_SOURCE_DIR}/third_party/hiredis-1.3.0)
if(NOT EXISTS ${HIREDIS_ROOT}/hiredis.h)
    message(WARNING "hiredis sources not found at ${HIREDIS_ROOT}. Redis wrapper will be skipped.")
    return()
endif()

set(HIREDIS_SOURCES
    ${HIREDIS_ROOT}/alloc.c
    ${HIREDIS_ROOT}/async.c
    ${HIREDIS_ROOT}/hiredis.c
    ${HIREDIS_ROOT}/net.c
    ${HIREDIS_ROOT}/read.c
    ${HIREDIS_ROOT}/sds.c
    ${HIREDIS_ROOT}/sockcompat.c
)

add_library(thirdparty_hiredis STATIC ${HIREDIS_SOURCES})
set(HIREDIS_GENERATED_INCLUDE ${CMAKE_BINARY_DIR}/generated/hiredis)
file(MAKE_DIRECTORY ${HIREDIS_GENERATED_INCLUDE}/hiredis)
file(MAKE_DIRECTORY ${HIREDIS_GENERATED_INCLUDE}/hiredis/adapters)
foreach(header hiredis.h read.h sds.h async.h alloc.h sockcompat.h)
    configure_file(${HIREDIS_ROOT}/${header} ${HIREDIS_GENERATED_INCLUDE}/hiredis/${header} COPYONLY)
endforeach()
configure_file(${HIREDIS_ROOT}/adapters/libuv.h
               ${HIREDIS_GENERATED_INCLUDE}/hiredis/adapters/libuv.h COPYONLY)
target_include_directories(thirdparty_hiredis PUBLIC ${HIREDIS_ROOT} ${HIREDIS_GENERATED_INCLUDE})
if(APPLE)
    target_link_libraries(thirdparty_hiredis PUBLIC iconv)
endif()

set(REDIS_PLUS_PLUS_ROOT ${PROJECT_SOURCE_DIR}/third_party/redis-plus-plus-1.3.15/src)
set(REDIS_PLUS_PLUS_INCLUDE ${REDIS_PLUS_PLUS_ROOT})
set(REDIS_PLUS_PLUS_CXX17 ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/cxx17)
set(REDIS_PLUS_PLUS_TLS ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/no_tls)
set(REDIS_PLUS_PLUS_FUTURE_STD ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/future/std)
set(REDIS_PLUS_PLUS_GENERATED ${CMAKE_BINARY_DIR}/generated/redis_plus_plus)
file(MAKE_DIRECTORY ${REDIS_PLUS_PLUS_GENERATED}/sw/redis++)
configure_file(${PROJECT_SOURCE_DIR}/third_party/redis-plus-plus-1.3.15/hiredis_features.h.in
               ${REDIS_PLUS_PLUS_GENERATED}/sw/redis++/hiredis_features.h @ONLY)

set(LIBUV_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/libuv-1.51.0/include)
if(NOT TARGET uv_a)
    message(FATAL_ERROR "libuv target uv_a not found. Ensure third_party/libuv-1.51.0 is added to the build.")
endif()
set(LIBUV_LIBRARY_TARGET uv_a)

set(SLG_REDIS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/redis_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/redis_config.cpp
)

set(REDIS_PLUS_PLUS_SOURCES
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/command.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/command_options.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/connection.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/connection_pool.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/crc16.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/errors.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/pipeline.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/redis.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/redis_cluster.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/redis_uri.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/reply.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/sentinel.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/shards.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/shards_pool.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/subscriber.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/transaction.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/event_loop.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_connection.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_connection_pool.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_redis.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_redis_cluster.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_sentinel.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_shards_pool.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_subscriber.cpp
    ${REDIS_PLUS_PLUS_ROOT}/sw/redis++/async_subscriber_impl.cpp
)

list(APPEND SLG_REDIS_SOURCES ${REDIS_PLUS_PLUS_SOURCES})

set(SLG_REDIS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/database/redis/redis_client.h
    ${PROJECT_SOURCE_DIR}/include/database/redis/redis_config.h
    ${PROJECT_SOURCE_DIR}/include/database/redis/redis_export.h
)

function(slg_redis_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_REDIS_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_REDIS_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${REDIS_PLUS_PLUS_INCLUDE}
            ${REDIS_PLUS_PLUS_CXX17}
            ${REDIS_PLUS_PLUS_TLS}
            ${REDIS_PLUS_PLUS_FUTURE_STD}
            ${REDIS_PLUS_PLUS_GENERATED}
            ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json-3.12.0/include
            ${HIREDIS_ROOT}
            ${HIREDIS_GENERATED_INCLUDE}
            ${LIBUV_INCLUDE_DIR}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC thirdparty_hiredis slg_json_static ${LIBUV_LIBRARY_TARGET})

    if(TARGET slg_coroutine_static)
        target_link_libraries(${target_name} PUBLIC slg_coroutine_static)
    elseif(TARGET slg_coroutine_shared)
        target_link_libraries(${target_name} PUBLIC slg_coroutine_shared)
    endif()

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_REDIS_BUILD_SHARED
            PUBLIC SLG_REDIS_SHARED
        )
    endif()
endfunction()

if(SLG_REDIS_BUILD_STATIC)
    slg_redis_define_library(slg_redis_static STATIC)
    set_target_properties(slg_redis_static PROPERTIES OUTPUT_NAME slg_redis_static)
    add_library(slg_redis::static ALIAS slg_redis_static)
endif()

if(SLG_REDIS_BUILD_SHARED)
    slg_redis_define_library(slg_redis_shared SHARED)
    set_target_properties(slg_redis_shared PROPERTIES OUTPUT_NAME slg_redis)
    add_library(slg_redis::shared ALIAS slg_redis_shared)
endif()
