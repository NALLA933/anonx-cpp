#include <anonx/core/config.hpp>
#include <anonx/core/logger.hpp>
#include <anonx/audio/ntgcalls_client.hpp>
#include <anonx/database/mongo_client.hpp>
#include <anonx/telegram/dispatcher.hpp>
#include <anonx/telegram/tdlib_client.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

extern "C" void signal_handler(int sig) {
    std::cout << "\n[Signal] Caught termination signal (" << sig << "). Initiating graceful shutdown...\n";
    g_running = false;
}

void setup_signal_handlers() {
#if !defined(_WIN32)
    // Ignore SIGPIPE so unexpected audio pipe closures don't kill the bot
    ::signal(SIGPIPE, SIG_IGN);
#endif

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

void print_banner() {
    std::cout << R"(
  █████╗ ███╗   ██╗ ██████╗ ███╗   ██╗██╗  ██╗       ██████╗██████╗ ██████╗ 
 ██╔══██╗████╗  ██║██╔═══██╗████╗  ██║╚██╗██╔╝      ██╔════╝██╔══██╗██╔══██╗
 ███████║██╔██╗ ██║██║   ██║██╔██╗ ██║ ╚███╔╝ █████╗██║     ██████╔╝██████╔╝
 ██╔══██║██║╚██╗██║██║   ██║██║╚██╗██║ ██╔██╗ ╚════╝██║     ██╔═══╝ ██╔═══╝ 
 ██║  ██║██║ ╚████║╚██████╔╝██║ ╚████║██╔╝ ██╗      ╚██████╗██║     ██║     
 ╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝       ╚═════╝╚═╝     ╚═╝     
        High-Performance C++20 Telegram Music Bot (Zero-VPS Build Engine)
    )" << std::endl;
}

void ensure_directories(const anonx::core::BotConfig& config) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& dir : {config.data_dir, config.downloads_dir, "sessions", "cache"}) {
        if (!fs::exists(dir, ec)) {
            fs::create_directories(dir, ec);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    print_banner();

    std::string config_file = (argc > 1) ? argv[1] : "config.json";

    // 1. Load Configuration
    anonx::core::BotConfig config = anonx::core::ConfigLoader::load(config_file);

    // 2. Initialize Logging
    anonx::core::Logger::instance().init(config.log_level, "bot.log");
    ANONX_LOG_INFO("Main", "Bootstrapping AnonX-CPP Engine...");

    if (!config.is_valid()) {
        ANONX_LOG_WARN("Main", "Incomplete Telegram credentials (BOT_TOKEN, API_ID, or API_HASH missing).");
        ANONX_LOG_WARN("Main", "Please provide credentials via config.json or environment variables.");
    }

    // 3. Ensure Runtime Directories
    ensure_directories(config);

    // 4. Setup Signal Handling
    setup_signal_handlers();

    // 5. Connect Database Layer (with auto-fallback)
    ANONX_LOG_INFO("Main", "Initializing MongoDB connection pool...");
    anonx::database::MongoClient::instance().connect(config.mongo_uri, config.db_name);

    // 6. Initialize Audio Bridge & Launch TDLib Client
    ANONX_LOG_INFO("Main", "Initializing WebRTC audio bridge...");
    anonx::audio::NTgCallsClient::instance().init();

    ANONX_LOG_INFO("Main", "Launching TDLib bot client...");
    auto bot_client = std::make_shared<anonx::telegram::TDLibClient>("bot", true);
    if (!bot_client->start(config.api_id, config.api_hash, config.bot_token, config.data_dir)) {
        ANONX_LOG_ERROR("Main", "Failed to start primary TDLib bot client.");
    }

    std::shared_ptr<anonx::telegram::TDLibClient> assistant_client = nullptr;
    if (!config.string_session.empty()) {
        ANONX_LOG_INFO("Main", "Assistant string session detected; launching userbot...");
        assistant_client = std::make_shared<anonx::telegram::TDLibClient>("assistant", false);
        assistant_client->start(config.api_id, config.api_hash, config.string_session, config.data_dir);
    }

    auto& dispatcher = anonx::telegram::CommandDispatcher::instance();
    dispatcher.init(config, bot_client);

    ANONX_LOG_INFO("Main", "AnonX-CPP Bot is now LIVE and listening for commands.");

    // 7. Main Event Loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // 8. Graceful Teardown
    ANONX_LOG_INFO("Main", "Shutting down AnonX-CPP subsystems...");
    if (bot_client) {
        bot_client->stop();
    }
    if (assistant_client) {
        assistant_client->stop();
    }
    anonx::audio::NTgCallsClient::instance().shutdown();
    anonx::database::MongoClient::instance().disconnect();

    ANONX_LOG_INFO("Main", "All subsystems halted cleanly. Exiting.");
    return 0;
}
