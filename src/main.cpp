#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

#include <sys/stat.h>

#include "anonx/app.hpp"

#if defined(ANONX_WITH_TDLIB)
#include <chrono>
#include <memory>
#include <thread>

#include "anonx/config.hpp"
#include "anonx/logger.hpp"
#include "anonx/runtime.hpp"
#include "anonx/userbot.hpp"

#if defined(ANONX_WITH_NTGCALLS)
#include "anonx/voice_signaling.hpp"
#else
#include "anonx/null_voice_transport.hpp"
#endif
#endif

namespace {

#if defined(ANONX_WITH_TDLIB)

volatile std::sig_atomic_t g_stop = 0;

extern "C" void onSignal(int) { g_stop = 1; }

bool makeDir(const char* path) {
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool ensureDirs(const anonx::Logger& log) {
    for (const char* dir : {"cache", "downloads", "tdlib", "data", "data/tdlib_session"}) {
        if (!makeDir(dir)) {
            log.critical(std::string("cannot create directory '") + dir + "': " +
                         std::strerror(errno));
            return false;
        }
    }
    return true;
}

int runBot(const std::string& envFile) {
    anonx::LogSink::instance().init("log.txt");
    anonx::Logger log("anonx");
    log.info(std::string("AnonXMusic C++ ") + anonx::App::kVersion + " — initialising");

    anonx::Config config = anonx::Config::load(envFile);
    config.check();

    if (!ensureDirs(log)) return 1;

    std::unique_ptr<anonx::Runtime> runtime;

#if defined(ANONX_WITH_NTGCALLS)

    anonx::NtgCallsTransport transport(anonx::makeDeferredAssistantSignaling(
        [&runtime]() -> anonx::TelegramClient* {
            if (!runtime) return nullptr;
            for (const std::unique_ptr<anonx::TelegramClient>& c :
                 runtime->userbot().clients()) {
                if (c && c->authorized()) return c.get();
            }
            return nullptr;
        }));
#else
    anonx::NullVoiceTransport transport;
    log.warning("built without NTgCalls (-DANONX_WITH_NTGCALLS=ON) — every "
                "command works, but streaming will report a server error");
#endif

    runtime = std::make_unique<anonx::Runtime>(config, transport);
    if (!runtime->start()) {
        log.critical("startup failed — see the messages above");
        return 1;
    }

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    log.info("Running. Press Ctrl+C to stop.");
    while (g_stop == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    runtime->stop();
    runtime.reset();
    anonx::LogSink::instance().close();
    return 0;
}

#else

int runSkeleton(const std::string& envFile) {
    anonx::App app(envFile);
    app.log().warning("built without TDLib (-DANONX_WITH_TDLIB=ON) — running the "
                      "data-layer skeleton only; no Telegram connection");
    app.boot();
    app.run();
    return 0;
}

#endif

}

int main(int argc, char** argv) {
    const std::string envFile = (argc > 1) ? argv[1] : ".env";
    try {

        std::setvbuf(stdout, nullptr, _IOLBF, 0);
#if defined(ANONX_WITH_TDLIB)
        return runBot(envFile);
#else
        return runSkeleton(envFile);
#endif
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
}
