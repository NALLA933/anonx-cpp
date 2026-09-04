cmake_minimum_required(VERSION 3.14)
project(anonx_cpp LANGUAGES CXX)

# --- Modern C++ ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Dependencies ---
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)   # needs libsqlite3-dev; exposes SQLite::SQLite3

# nlohmann_json (header-only) — used only inside src/youtube.cpp.
# Prefer a system/vcpkg/conan package; otherwise fetch the release tarball.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found locally; fetching v3.11.3")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- Phase 1: data-layer library ---
add_library(anonx_data
  src/database.cpp
)
target_include_directories(anonx_data
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anonx_data
  PUBLIC SQLite::SQLite3 Threads::Threads)
target_compile_options(anonx_data PRIVATE -Wall -Wextra)

# --- Phase 2/3: core library (config, logging, app skeleton, YouTube) ---
add_library(anonx_core
  src/config.cpp
  src/logger.cpp
  src/app.cpp
  src/youtube.cpp
)
target_link_libraries(anonx_core
  PUBLIC anonx_data
  PRIVATE nlohmann_json::nlohmann_json)   # json used only in youtube.cpp
target_compile_options(anonx_core PRIVATE -Wall -Wextra)

# --- Real entrypoint ---
# Links the full Runtime when the Telegram layer is available (see the bottom of
# this file), and falls back to the data-layer skeleton otherwise.
add_executable(anonx src/main.cpp)
target_link_libraries(anonx PRIVATE anonx_core)
target_compile_options(anonx PRIVATE -Wall -Wextra)

# --- Demo / test executables ---
add_executable(anonx_db_demo examples/db_demo.cpp)
target_link_libraries(anonx_db_demo PRIVATE anonx_data)

add_executable(anonx_app_demo examples/app_demo.cpp)
target_link_libraries(anonx_app_demo PRIVATE anonx_core)

add_executable(anonx_youtube_demo examples/youtube_demo.cpp)
target_link_libraries(anonx_youtube_demo PRIVATE anonx_core)

# --- Phase 4 + integration: Telegram layer (TDLib JSON interface) ---
# The first four files are Phase 4 (transport, account, routing, assistants); the
# last three are the integration phase — the production BotApi, the MTProto half
# of joining a voice chat, and the object graph that ties every layer together.
set(ANONX_TG_SOURCES
  src/td_client.cpp
  src/telegram_client.cpp
  src/dispatcher.cpp
  src/userbot.cpp
  src/telegram_bot_api.cpp
  src/voice_signaling.cpp
  src/runtime.cpp
)

# Production build: compile the Telegram layer against the real TDLib JSON
# library. Off by default so the core project builds without TDLib installed;
# enable with -DANONX_WITH_TDLIB=ON (needs TDLib's CMake package / Td::TdJson).
option(ANONX_WITH_TDLIB "Build the Telegram layer against a real TDLib (Td::TdJson)" OFF)
if(ANONX_WITH_TDLIB)
  find_package(Td REQUIRED)   # provides the imported target Td::TdJson
  add_library(anonx_tg ${ANONX_TG_SOURCES})
  target_link_libraries(anonx_tg
    PUBLIC anonx_core Td::TdJson
    PRIVATE nlohmann_json::nlohmann_json)
  target_compile_options(anonx_tg PRIVATE -Wall -Wextra)
  message(STATUS "ANONX_WITH_TDLIB=ON — building anonx_tg against Td::TdJson")
endif()

# Hermetic test build: the same Telegram sources compiled against a scripted
# fake TDLib (test/fake_tdjson). Needs no TDLib install and no network, so this
# is what ctest runs. The fake header dir is placed AFTER include/ so only
# <td/telegram/td_json_client.h> resolves to the fake. The other fakes are for
# the Part 3 router checks below, which run the real command handlers.
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

# --- Phase 5: voice + queue layer ---
# The queue (include/anonx/queue.hpp) and the VoiceTransport interface
# (include/anonx/voice_transport.hpp) are header-only; only the CallManager
# orchestration needs compiling. This library is engine-agnostic — it talks to
# the abstract VoiceTransport, so it links no voice/WebRTC dependency, and it
# uses only the public headers (not even the SQLite data layer), keeping the
# voice logic and its test lightweight.
add_library(anonx_voice
  src/call_manager.cpp
)
target_include_directories(anonx_voice
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(anonx_voice PUBLIC Threads::Threads)
target_compile_options(anonx_voice PRIVATE -Wall -Wextra)

# Production build: the real NTgCalls-backed transport. Off by default so the
# project builds without NTgCalls/WebRTC installed; enable with
# -DANONX_WITH_NTGCALLS=ON. The NTgCalls headers are confined to
# ntgcalls_transport.cpp (PIMPL), so only this target pulls the dependency.
# (The CMake target name for NTgCalls varies by install method; adjust the
# find_package/target below to match your NTgCalls build if needed.)
option(ANONX_WITH_NTGCALLS "Build the real NTgCalls voice transport" OFF)
if(ANONX_WITH_NTGCALLS)
  find_package(ntgcalls REQUIRED)   # provides an imported NTgCalls target
  add_library(anonx_voice_ntgcalls src/ntgcalls_transport.cpp)
  target_compile_definitions(anonx_voice_ntgcalls PUBLIC ANONX_WITH_NTGCALLS)
  target_link_libraries(anonx_voice_ntgcalls
    PUBLIC anonx_voice ntgcalls)
  target_compile_options(anonx_voice_ntgcalls PRIVATE -Wall -Wextra)
  message(STATUS "ANONX_WITH_NTGCALLS=ON — building anonx_voice_ntgcalls against NTgCalls")
endif()

# Hermetic test build: CallManager + Queue driven through a scripted
# FakeVoiceTransport (test/fake_ntgcalls). No NTgCalls, no network — this is
# what ctest runs to verify queue/loop/auto-advance orchestration.
add_executable(anonx_voice_demo examples/voice_demo.cpp)
target_include_directories(anonx_voice_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls)
target_link_libraries(anonx_voice_demo PRIVATE anonx_voice)
target_compile_options(anonx_voice_demo PRIVATE -Wall -Wextra)

# --- Phase 6a/6b: command plugins ---
# The language tables, inline keyboards, permission guards, the playback command
# handlers (6a) and everything that is not playback — auth, blacklist,
# broadcast, sudoers, the inline menus and the host-metric cards (6b). Like the
# voice layer, this library is transport-agnostic: it talks to the abstract
# BotApi (include/anonx/bot_api.hpp) rather than to TDLib, so it links no
# Telegram dependency and its tests run fully offline.
add_library(anonx_plugins
  src/lang.cpp
  src/buttons.cpp
  src/guards.cpp
  src/plugins.cpp
  src/sysinfo.cpp
  src/admin_plugins.cpp
)
target_link_libraries(anonx_plugins
  PUBLIC anonx_core anonx_voice          # Database/Config/YouTube + CallManager
  PRIVATE nlohmann_json::nlohmann_json)  # json used only in lang.cpp
target_compile_options(anonx_plugins PRIVATE -Wall -Wextra)

# src/plugins_router.cpp is the ONLY Phase 6a/6b file that touches the
# dispatcher, so it is compiled into the Telegram targets instead of the library
# above — that is what keeps TDLib out of anonx_plugins and out of its tests.
if(ANONX_WITH_TDLIB)
  target_sources(anonx_tg PRIVATE src/plugins_router.cpp)
  target_link_libraries(anonx_tg PUBLIC anonx_plugins)

  # --- Integration: the real bot binary ---
  # ANONX_WITH_TDLIB switches src/main.cpp from the data-layer skeleton to the
  # full Runtime. Voice is a second, independent switch: with NTgCalls the bot
  # streams for real, without it NullVoiceTransport keeps every other command
  # working (see include/anonx/null_voice_transport.hpp).
  target_compile_definitions(anonx PRIVATE ANONX_WITH_TDLIB)
  target_link_libraries(anonx PRIVATE anonx_tg)
  if(ANONX_WITH_NTGCALLS)
    target_link_libraries(anonx PRIVATE anonx_voice_ntgcalls)
    message(STATUS "anonx: full build — TDLib + NTgCalls")
  else()
    message(STATUS "anonx: TDLib without NTgCalls — streaming disabled")
  endif()
endif()

# The hermetic Telegram demo builds the router too (against the fake TDLib), so
# the dispatcher wiring is compile- and link-checked without a TDLib install.
target_sources(anonx_telegram_demo PRIVATE src/plugins_router.cpp)
target_link_libraries(anonx_telegram_demo PRIVATE anonx_plugins)

# Hermetic test build: the real command handlers driven through a scripted
# FakeBotApi / FakeYouTube / FakeVoiceTransport over a real SQLite database.
# ANONX_LOCALES_DIR lets the test load the actual locales/*.json (so the checks
# verify real templates and argument order) from any working directory.
add_executable(anonx_plugins_demo examples/plugins_demo.cpp)
target_include_directories(anonx_plugins_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_bot_api
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_youtube
  ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_ntgcalls)
target_compile_definitions(anonx_plugins_demo PRIVATE
  ANONX_LOCALES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/locales")
target_link_libraries(anonx_plugins_demo PRIVATE anonx_plugins)
target_compile_options(anonx_plugins_demo PRIVATE -Wall -Wextra)

# The Phase 6b half: the same idea, plus a FakeSystemInfo so the /ping and /stats
# cards (which otherwise read this machine's CPU, RAM and uptime) can be compared
# byte for byte against the locale templates.
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
