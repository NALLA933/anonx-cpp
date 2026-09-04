cmake_minimum_required(VERSION 3.14)
project(senpai_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found locally; fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# Database component
add_library(senpai_data
  src/database/database.cpp
)
target_include_directories(senpai_data
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai_data
  PUBLIC SQLite::SQLite3 Threads::Threads)
target_compile_options(senpai_data PRIVATE -Wall -Wextra)

# Utilities component
add_library(senpai_utils
  src/utils/youtube.cpp
  src/utils/sysinfo.cpp
)
target_include_directories(senpai_utils
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai_utils
  PUBLIC senpai_data
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(senpai_utils PRIVATE -Wall -Wextra)

# Core component
add_library(senpai_core
  src/core/config.cpp
  src/core/logger.cpp
  src/core/app.cpp
)
target_include_directories(senpai_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai_core
  PUBLIC senpai_data senpai_utils
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(senpai_core PRIVATE -Wall -Wextra)

# Voice component
add_library(senpai_voice
  src/voice/call_manager.cpp
)
target_include_directories(senpai_voice
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai_voice PUBLIC Threads::Threads senpai_utils)
target_compile_options(senpai_voice PRIVATE -Wall -Wextra)

option(SENPAI_WITH_NTGCALLS "Build the real NTgCalls voice transport" OFF)
if(SENPAI_WITH_NTGCALLS)
  find_package(ntgcalls REQUIRED)
  add_library(senpai_voice_ntgcalls src/voice/ntgcalls_transport.cpp)
  target_compile_definitions(senpai_voice_ntgcalls PUBLIC SENPAI_WITH_NTGCALLS)
  target_link_libraries(senpai_voice_ntgcalls
    PUBLIC senpai_voice ntgcalls)
  target_compile_options(senpai_voice_ntgcalls PRIVATE -Wall -Wextra)
  message(STATUS "SENPAI_WITH_NTGCALLS=ON — building senpai_voice_ntgcalls against NTgCalls")
endif()

# Plugins component
add_library(senpai_plugins
  src/plugins/lang.cpp
  src/plugins/buttons.cpp
  src/plugins/guards.cpp
  src/plugins/plugins.cpp
  src/plugins/admin_plugins.cpp
)
target_include_directories(senpai_plugins
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai_plugins
  PUBLIC senpai_core senpai_voice
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(senpai_plugins PRIVATE -Wall -Wextra)

# Main bot executable
add_executable(senpai src/main.cpp)
target_include_directories(senpai
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(senpai PRIVATE senpai_core)
target_compile_options(senpai PRIVATE -Wall -Wextra)

# Telegram and Runtime layer
set(SENPAI_TG_SOURCES
  src/telegram/td_client.cpp
  src/telegram/telegram_client.cpp
  src/telegram/dispatcher.cpp
  src/telegram/userbot.cpp
  src/telegram/telegram_bot_api.cpp
  src/voice/voice_signaling.cpp
  src/core/runtime.cpp
)

option(SENPAI_WITH_TDLIB "Build the Telegram layer against a real TDLib (Td::TdJson)" OFF)
if(SENPAI_WITH_TDLIB OR ANONX_WITH_TDLIB)
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
    else()
      message(FATAL_ERROR "SENPAI_WITH_TDLIB=ON but neither Td CMake package nor libtdjson was found.")
    endif()
  endif()
  add_library(senpai_tg ${SENPAI_TG_SOURCES})
  target_include_directories(senpai_tg
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(senpai_tg
    PUBLIC senpai_core Td::TdJson
    PRIVATE nlohmann_json::nlohmann_json)
  target_compile_options(senpai_tg PRIVATE -Wall -Wextra)
  message(STATUS "SENPAI_WITH_TDLIB=ON — building senpai_tg against Td::TdJson")

  target_sources(senpai_tg PRIVATE src/plugins/plugins_router.cpp)
  target_link_libraries(senpai_tg PUBLIC senpai_plugins)

  target_compile_definitions(senpai PRIVATE SENPAI_WITH_TDLIB)
  target_link_libraries(senpai PRIVATE senpai_tg)
  set_target_properties(senpai PROPERTIES
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
    INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
  )
  if(SENPAI_WITH_NTGCALLS)
    target_link_libraries(senpai PRIVATE senpai_voice_ntgcalls)
    message(STATUS "senpai: full build — TDLib + NTgCalls")
  else()
    message(STATUS "senpai: TDLib without NTgCalls — streaming disabled")
  endif()
endif()

# Offline Unit Tests
option(BUILD_TESTING "Build offline unit test suite" ON)
if(BUILD_TESTING)
  enable_testing()
  add_executable(senpai_tests tests/test_main.cpp)
  target_include_directories(senpai_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(senpai_tests
    PRIVATE senpai_core senpai_plugins)
  target_compile_options(senpai_tests PRIVATE -Wall -Wextra)
  add_test(NAME senpai_unit_tests COMMAND senpai_tests)
endif()
