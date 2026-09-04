cmake_minimum_required(VERSION 3.14)
project(senpai_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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

set(SENPAI_SOURCES
  src/core/config.cpp
  src/core/logger.cpp
  src/core/runtime.cpp
  src/database/database.cpp
  src/plugins/admin_plugins.cpp
  src/plugins/buttons.cpp
  src/plugins/guards.cpp
  src/plugins/lang.cpp
  src/plugins/plugins.cpp
  src/plugins/plugins_router.cpp
  src/telegram/dispatcher.cpp
  src/telegram/td_client.cpp
  src/telegram/telegram_client.cpp
  src/telegram/userbot.cpp
  src/utils/sysinfo.cpp
  src/utils/youtube.cpp
  src/voice/call_manager.cpp
  src/voice/ntgcalls_transport.cpp
  src/voice/voice_signaling.cpp
)

# Unified bot executable
add_executable(senpai_bot src/main.cpp ${SENPAI_SOURCES})
set_target_properties(senpai_bot PROPERTIES OUTPUT_NAME "senpai")
add_custom_target(senpai DEPENDS senpai_bot)

target_include_directories(senpai_bot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(senpai_bot PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(senpai_bot PRIVATE
  SQLite::SQLite3
  Threads::Threads
  nlohmann_json::nlohmann_json
  ${CMAKE_DL_LIBS}
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
  target_compile_definitions(senpai_bot PRIVATE SENPAI_WITH_TDLIB)
  target_link_libraries(senpai_bot PRIVATE Td::TdJson)
  set_target_properties(senpai_bot PROPERTIES
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
    INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
  )
endif()

option(SENPAI_WITH_NTGCALLS "Build the real NTgCalls voice transport" OFF)
if(SENPAI_WITH_NTGCALLS)
  find_package(ntgcalls REQUIRED)
  target_compile_definitions(senpai_bot PRIVATE SENPAI_WITH_NTGCALLS)
  target_link_libraries(senpai_bot PRIVATE ntgcalls)
  message(STATUS "SENPAI_WITH_NTGCALLS=ON — building against NTgCalls")
endif()

# Offline Unit Tests
option(BUILD_TESTING "Build offline unit test suite" OFF)
if(BUILD_TESTING)
  enable_testing()
  add_executable(senpai_tests
    tests/test_main.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/database/database.cpp
    src/plugins/lang.cpp
    src/utils/sysinfo.cpp
    src/utils/youtube.cpp
  )
  target_include_directories(senpai_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(senpai_tests PRIVATE -Wall -Wextra -pedantic)
  target_link_libraries(senpai_tests PRIVATE
    SQLite::SQLite3
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}
  )
  add_test(NAME senpai_unit_tests COMMAND senpai_tests)
endif()
