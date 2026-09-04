#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <sstream>

#if __has_include(<spdlog/spdlog.h>)
#include <spdlog/spdlog.h>
#define ANONX_HAS_SPDLOG 1
#else
#define ANONX_HAS_SPDLOG 0
#endif

namespace anonx::core {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

class Logger {
public:
    static Logger& instance();

    void init(std::string_view level_name = "info", std::string_view log_file = "");
    void set_level(LogLevel level);
    [[nodiscard]] LogLevel level() const noexcept;

    void log(LogLevel level, std::string_view tag, std::string_view message);

    template <typename... Args>
    void info(std::string_view tag, Args&&... args) {
        log(LogLevel::Info, tag, format_args(std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warn(std::string_view tag, Args&&... args) {
        log(LogLevel::Warn, tag, format_args(std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error(std::string_view tag, Args&&... args) {
        log(LogLevel::Error, tag, format_args(std::forward<Args>(args)...));
    }

    template <typename... Args>
    void debug(std::string_view tag, Args&&... args) {
        log(LogLevel::Debug, tag, format_args(std::forward<Args>(args)...));
    }

private:
    Logger();
    ~Logger() = default;

    template <typename... Args>
    static std::string format_args(Args&&... args) {
        std::ostringstream oss;
        ((oss << std::forward<Args>(args)), ...);
        return oss.str();
    }

    mutable std::mutex mutex_;
    LogLevel current_level_{LogLevel::Info};
    bool initialized_{false};
};

} // namespace anonx::core

// Convenience Logging Macros
#define ANONX_LOG_TRACE(tag, ...) anonx::core::Logger::instance().log(anonx::core::LogLevel::Trace, tag, __VA_ARGS__)
#define ANONX_LOG_DEBUG(tag, ...) anonx::core::Logger::instance().debug(tag, __VA_ARGS__)
#define ANONX_LOG_INFO(tag, ...)  anonx::core::Logger::instance().info(tag, __VA_ARGS__)
#define ANONX_LOG_WARN(tag, ...)  anonx::core::Logger::instance().warn(tag, __VA_ARGS__)
#define ANONX_LOG_ERROR(tag, ...) anonx::core::Logger::instance().error(tag, __VA_ARGS__)
