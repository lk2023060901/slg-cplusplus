option(SLG_APPLICATION_BUILD_STATIC "Build the static variant of slg_application" ON)
option(SLG_APPLICATION_BUILD_SHARED "Build the shared variant of slg_application" ON)

find_package(OpenSSL REQUIRED)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL is required for slg_application crypto support")
endif()

if(NOT SLG_APPLICATION_BUILD_STATIC AND NOT SLG_APPLICATION_BUILD_SHARED)
    message(FATAL_ERROR "Enable at least one of SLG_APPLICATION_BUILD_STATIC or SLG_APPLICATION_BUILD_SHARED")
endif()

file(GLOB ZSTD_COMMON_SOURCES ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib/common/*.c)
file(GLOB ZSTD_COMPRESS_SOURCES ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib/compress/*.c)
file(GLOB ZSTD_DECOMPRESS_SOURCES ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib/decompress/*.c)
file(GLOB ZSTD_DICT_SOURCES ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib/dictBuilder/*.c)

set(SLG_APPLICATION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/application.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_codec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tcp_protocol_router.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_context.cpp
    ${PROJECT_SOURCE_DIR}/src/crypto/aes_crypto_processor.cpp
    ${PROJECT_SOURCE_DIR}/src/compressor/compression_processors.cpp
    ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib/lz4.c
    ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib/lz4hc.c
    ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib/lz4frame.c
    ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib/xxhash.c
    ${ZSTD_COMMON_SOURCES}
    ${ZSTD_COMPRESS_SOURCES}
    ${ZSTD_DECOMPRESS_SOURCES}
    ${ZSTD_DICT_SOURCES}
)

set(SLG_APPLICATION_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/application/application.h
    ${PROJECT_SOURCE_DIR}/include/application/application_export.h
    ${PROJECT_SOURCE_DIR}/include/application/dependency_container.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/length_prefixed_reader.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/message_codec.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/packet_header.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/protocol_registry.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/tcp_protocol_router.h
    ${PROJECT_SOURCE_DIR}/include/crypto/aes_crypto_processor.h
    ${PROJECT_SOURCE_DIR}/include/crypto/crypto_processor.h
    ${PROJECT_SOURCE_DIR}/include/compressor/compression_processor.h
    ${PROJECT_SOURCE_DIR}/include/application/protocol/security_context.h
)

set(CXXOPTS_DIR ${PROJECT_SOURCE_DIR}/third_party/cxxopts-3.3.1/include)
set(NLOHMANN_DIR ${PROJECT_SOURCE_DIR}/third_party/nlohmann-json-3.12.0/include)

function(slg_application_define_library target_name lib_type)
    add_library(${target_name} ${lib_type} ${SLG_APPLICATION_SOURCES})
    target_sources(${target_name} PRIVATE ${SLG_APPLICATION_PUBLIC_HEADERS})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
            ${CXXOPTS_DIR}
            ${NLOHMANN_DIR}
            ${PROJECT_SOURCE_DIR}/third_party/protobuf-33.1/src
            ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib
            ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib
        PRIVATE
            ${PROJECT_SOURCE_DIR}/third_party/protobuf-33.1/src
            ${PROJECT_SOURCE_DIR}/third_party/lz4-1.10.0/lib
            ${PROJECT_SOURCE_DIR}/third_party/zstd-1.5.7/lib
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)
    target_link_libraries(${target_name} PUBLIC slg_tcp_static slg_json_static OpenSSL::Crypto)

    if(lib_type STREQUAL "SHARED")
        target_compile_definitions(${target_name}
            PRIVATE SLG_APPLICATION_BUILD_SHARED
            PUBLIC SLG_APPLICATION_SHARED
        )
    endif()
endfunction()

if(SLG_APPLICATION_BUILD_STATIC)
    slg_application_define_library(slg_application_static STATIC)
    set_target_properties(slg_application_static PROPERTIES OUTPUT_NAME slg_application_static)
    add_library(slg_application::static ALIAS slg_application_static)
endif()

if(SLG_APPLICATION_BUILD_SHARED)
    slg_application_define_library(slg_application_shared SHARED)
    set_target_properties(slg_application_shared PROPERTIES OUTPUT_NAME slg_application)
    add_library(slg_application::shared ALIAS slg_application_shared)
endif()
