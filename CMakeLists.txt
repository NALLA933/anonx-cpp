cmake_minimum_required(VERSION 3.25)

project(
    anonx_cpp
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "Production-Grade High-Performance C++20 Telegram Music Bot"
)

# ------------------------------------------------------------------------------
# Language Standards & Compile Commands
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------------------
# CMake Modules & Linker Acceleration
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(MoldLinker)

# Compiler Cache (ccache) Setup
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: Enabling compiler launcher (${CCACHE_PROGRAM})")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# ------------------------------------------------------------------------------
# Dependencies Discovery (via vcpkg or system)
# ------------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(fmt CONFIG QUIET)
find_package(spdlog CONFIG QUIET)
find_package(nlohmann_json 3.10 CONFIG QUIET)
find_package(OpenSSL QUIET)

# Fallback for nlohmann_json if not found via vcpkg
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via find_package; attempting FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# MongoDB C++ Driver
find_package(mongocxx CONFIG QUIET)
find_package(bsoncxx CONFIG QUIET)

# TDLib (libtdjson) Discovery
find_package(Td QUIET)
if(NOT TARGET Td::TdJson)
    find_library(TDJSON_LIB NAMES tdjson libtdjson
        PATHS
            /usr/local/lib
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            ${CMAKE_CURRENT_SOURCE_DIR}/lib
            ${CMAKE_CURRENT_BINARY_DIR}
    )
    if(TDJSON_LIB)
        message(STATUS "Found libtdjson: ${TDJSON_LIB}")
        add_library(Td::TdJson UNKNOWN IMPORTED)
        set_target_properties(Td::TdJson PROPERTIES
            IMPORTED_LOCATION "${TDJSON_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
    endif()
endif()

# NTgCalls Discovery
find_package(ntgcalls QUIET)

# ------------------------------------------------------------------------------
# Core Library: anonx::core
# ------------------------------------------------------------------------------
add_library(anonx_core STATIC
    src/core/config.cpp
    src/core/logger.cpp
)
add_library(anonx::core ALIAS anonx_core)
target_include_directories(anonx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(anonx_core PUBLIC
    Threads::Threads
    nlohmann_json::nlohmann_json
    $<$<TARGET_EXISTS:fmt::fmt>:fmt::fmt>
    $<$<TARGET_EXISTS:spdlog::spdlog>:spdlog::spdlog>
)
set_project_warnings(anonx_core)

# ------------------------------------------------------------------------------
# Database Library: anonx::database
# ------------------------------------------------------------------------------
add_library(anonx_database STATIC
    src/database/mongo_client.cpp
    src/database/models.cpp
)
add_library(anonx::database ALIAS anonx_database)
target_include_directories(anonx_database PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(anonx_database PUBLIC
    anonx::core
    $<$<TARGET_EXISTS:mongo::mongocxx_shared>:mongo::mongocxx_shared>
    $<$<TARGET_EXISTS:mongo::bsoncxx_shared>:mongo::bsoncxx_shared>
)
set_project_warnings(anonx_database)

# ------------------------------------------------------------------------------
# Audio Pipeline Library: anonx::audio
# ------------------------------------------------------------------------------
add_library(anonx_audio STATIC
    src/audio/ffmpeg_pipeline.cpp
    src/audio/audio_streamer.cpp
    src/audio/ntgcalls_client.cpp
)
add_library(anonx::audio ALIAS anonx_audio)
target_include_directories(anonx_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(anonx_audio PUBLIC
    anonx::core
    $<$<TARGET_EXISTS:OpenSSL::SSL>:OpenSSL::SSL>
    $<$<TARGET_EXISTS:OpenSSL::Crypto>:OpenSSL::Crypto>
    $<$<TARGET_EXISTS:ntgcalls>:ntgcalls>
    ${CMAKE_DL_LIBS}
)
set_project_warnings(anonx_audio)

# ------------------------------------------------------------------------------
# Telegram Layer: anonx::telegram
# ------------------------------------------------------------------------------
add_library(anonx_telegram STATIC
    src/telegram/tdlib_client.cpp
    src/telegram/dispatcher.cpp
)
add_library(anonx::telegram ALIAS anonx_telegram)
target_include_directories(anonx_telegram PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(anonx_telegram PUBLIC
    anonx::core
    anonx::audio
    anonx::database
    $<$<TARGET_EXISTS:Td::TdJson>:Td::TdJson>
    ${CMAKE_DL_LIBS}
)
set_project_warnings(anonx_telegram)

# ------------------------------------------------------------------------------
# Bot Executable: anonx_bot
# ------------------------------------------------------------------------------
add_executable(anonx_bot src/main.cpp)
target_link_libraries(anonx_bot PRIVATE
    anonx::core
    anonx::database
    anonx::audio
    anonx::telegram
)
set_project_warnings(anonx_bot)

set_target_properties(anonx_bot PROPERTIES
    OUTPUT_NAME "anonx_bot"
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
    INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
)

# Strip symbols for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
    add_custom_command(TARGET anonx_bot POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:anonx_bot>
        COMMENT "Stripping debug symbols from release executable..."
    )
endif()
