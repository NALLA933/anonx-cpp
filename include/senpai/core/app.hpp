#ifndef SENPAI_APP_HPP
#define SENPAI_APP_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "senpai/config.hpp"
#include "senpai/logger.hpp"

#include "senpai/version.hpp"

namespace senpai {

class Database;
class CacheManager;

class App {
public:
    static constexpr const char* kVersion = senpai::kVersion;

    explicit App(const std::string& envFile = ".env");
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void boot();

    void run();

    void stop();

    double uptimeSeconds() const;

    void requestStop();

    Config&        config()       { return config_; }
    const Config&  config() const { return config_; }
    const Logger&  log() const    { return logger_; }
    Database&      db();
    CacheManager&  cache();

private:
    void ensureDirs();
    void checkMediaTools();

    Config config_;
    Logger logger_;
    std::unique_ptr<Database>     db_;
    std::unique_ptr<CacheManager> cache_;

    std::chrono::steady_clock::time_point bootTime_{};
    std::atomic<bool> stopRequested_{false};
    bool booted_ = false;
    bool stopped_ = false;
};

}

#endif
