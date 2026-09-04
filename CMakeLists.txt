cmake_minimum_required(VERSION 3.14)
project(anonx_cpp LANGUAGES CXX)

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

add_library(anonx_data
  src/database.cpp
)
target_include_directories(anonx_data
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anonx_data
  PUBLIC SQLite::SQLite3 Threads::Threads)
target_compile_options(anonx_data PRIVATE -Wall -Wextra)

add_library(anonx_core
  src/config.cpp
  src/logger.cpp
  src/app.cpp
  src/youtube.cpp
)
target_link_libraries(anonx_core
  PUBLIC anonx_data
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(anonx_core PRIVATE -Wall -Wextra)

add_executable(anonx src/main.cpp)
target_link_libraries(anonx PRIVATE anonx_core)
target_compile_options(anonx PRIVATE -Wall -Wextra)

add_executable(anonx_db_demo examples/db_demo.cpp)
target_link_libraries(anonx_db_demo PRIVATE anonx_data)

add_executable(anonx_app_demo examples/app_demo.cpp)
target_link_libraries(anonx_app_demo PRIVATE anonx_core)

add_executable(anonx_youtube_demo examples/youtube_demo.cpp)
target_link_libraries(anonx_youtube_demo PRIVATE anonx_core)

set(ANONX_TG_SOURCES
  src/td_client.cpp
  src/telegram_client.cpp
  src/dispatcher.cpp
  src/userbot.cpp
  src/telegram_bot_api.cpp
  src/voice_signaling.cpp
  src/runtime.cpp
)

option(ANONX_WITH_TDLIB "Build the Telegram layer against a real TDLib (Td::TdJson)" OFF)
if(ANONX_WITH_TDLIB)
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
      message(FATAL_ERROR "ANONX_WITH_TDLIB=ON but neither Td CMake package nor libtdjson.so was found.")
    endif()
  endif()
  add_library(anonx_tg ${ANONX_TG_SOURCES})
  target_link_libraries(anonx_tg
    PUBLIC anonx_core Td::TdJson
    PRIVATE nlohmann_json::nlohmann_json)
  target_compile_options(anonx_tg PRIVATE -Wall -Wextra)
  message(STATUS "ANONX_WITH_TDLIB=ON — building anonx_tg against Td::TdJson")
endif()

add_executable(anonx_telegram_demo
  examples/telegram_demo.cpp
  ${ANONX_TG_SOURCES}
  test/fake_tdjson/fake_tdjson.cpp)
target_include_directories(anonx_telegram_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_tdjson
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_bot_api
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_youtube
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_sysinfo)
target_compile_definitions(anonx_telegram_demo PRIVATE
  ANONX_LOCALES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/locales")
target_link_libraries(anonx_telegram_demo
  PRIVATE anonx_core nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(anonx_telegram_demo PRIVATE -Wall -Wextra)

add_library(anonx_voice
  src/call_manager.cpp
)
target_include_directories(anonx_voice
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anonx_voice PUBLIC Threads::Threads)
target_compile_options(anonx_voice PRIVATE -Wall -Wextra)

option(ANONX_WITH_NTGCALLS "Build the real NTgCalls voice transport" OFF)
if(ANONX_WITH_NTGCALLS)
  find_package(ntgcalls REQUIRED)
  add_library(anonx_voice_ntgcalls src/ntgcalls_transport.cpp)
  target_compile_definitions(anonx_voice_ntgcalls PUBLIC ANONX_WITH_NTGCALLS)
  target_link_libraries(anonx_voice_ntgcalls
    PUBLIC anonx_voice ntgcalls)
  target_compile_options(anonx_voice_ntgcalls PRIVATE -Wall -Wextra)
  message(STATUS "ANONX_WITH_NTGCALLS=ON — building anonx_voice_ntgcalls against NTgCalls")
endif()

add_executable(anonx_voice_demo examples/voice_demo.cpp)
target_include_directories(anonx_voice_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls)
target_link_libraries(anonx_voice_demo PRIVATE anonx_voice)
target_compile_options(anonx_voice_demo PRIVATE -Wall -Wextra)

add_library(anonx_plugins
  src/lang.cpp
  src/buttons.cpp
  src/guards.cpp
  src/plugins.cpp
  src/sysinfo.cpp
  src/admin_plugins.cpp
)
target_link_libraries(anonx_plugins
  PUBLIC anonx_core anonx_voice
  PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(anonx_plugins PRIVATE -Wall -Wextra)

if(ANONX_WITH_TDLIB)
  target_sources(anonx_tg PRIVATE src/plugins_router.cpp)
  target_link_libraries(anonx_tg PUBLIC anonx_plugins)

  target_compile_definitions(anonx PRIVATE ANONX_WITH_TDLIB)
  target_link_libraries(anonx PRIVATE anonx_tg)
  set_target_properties(anonx PROPERTIES
    BUILD_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
    INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib:/usr/local/lib:/usr/lib"
  )
  if(ANONX_WITH_NTGCALLS)
    target_link_libraries(anonx PRIVATE anonx_voice_ntgcalls)
    message(STATUS "anonx: full build — TDLib + NTgCalls")
  else()
    message(STATUS "anonx: TDLib without NTgCalls — streaming disabled")
  endif()
endif()

target_sources(anonx_telegram_demo PRIVATE src/plugins_router.cpp)
target_link_libraries(anonx_telegram_demo PRIVATE anonx_plugins)

add_executable(anonx_plugins_demo examples/plugins_demo.cpp)
target_include_directories(anonx_plugins_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_bot_api
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_youtube
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls)
target_compile_definitions(anonx_plugins_demo PRIVATE
  ANONX_LOCALES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/locales")
target_link_libraries(anonx_plugins_demo PRIVATE anonx_plugins)
target_compile_options(anonx_plugins_demo PRIVATE -Wall -Wextra)

add_executable(anonx_admin_demo examples/admin_demo.cpp)
target_include_directories(anonx_admin_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_bot_api
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_sysinfo)
target_compile_definitions(anonx_admin_demo PRIVATE
  ANONX_LOCALES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/locales")
target_link_libraries(anonx_admin_demo PRIVATE anonx_plugins)
target_compile_options(anonx_admin_demo PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME db_demo      COMMAND anonx_db_demo)
add_test(NAME app_demo     COMMAND anonx_app_demo)
add_test(NAME youtube_demo COMMAND anonx_youtube_demo)
add_test(NAME telegram_demo COMMAND anonx_telegram_demo)
add_test(NAME voice_demo   COMMAND anonx_voice_demo)
add_test(NAME plugins_demo COMMAND anonx_plugins_demo)
add_test(NAME admin_demo   COMMAND anonx_admin_demo)
