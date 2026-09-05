#include <anonx/core/logger.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

#if ANONX_HAS_SPDLOG
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#endif

namespace anonx::core {

namespace {

#if !ANONX_HAS_SPDLOG
std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &in_time_t);
#else
    localtime_r(&in_time_t, &tm_buf);
#endif
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO ";
        case LogLevel::Warn:     return "WARN ";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "FATAL";
    }
    return "INFO ";
}

const char* level_to_color(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "\033[90m"; // Gray
        case LogLevel::Debug:    return "\033[36m"; // Cyan
        case LogLevel::Info:     return "\033[32m"; // Green
        case LogLevel::Warn:     return "\033[33m"; // Yellow
        case LogLevel::Error:    return "\033[31m"; // Red
        case LogLevel::Critical: return "\033[1;31m"; // Bold Red
    }
    return "\033[0m";
}
#endif

} // namespace

Logger::Logger() = default;

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(std::string_view level_name, std::string_view log_file) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return;

    if (level_name == "trace") current_level_ = LogLevel::Trace;
    else if (level_name == "debug") current_level_ = LogLevel::Debug;
    else if (level_name == "warn" || level_name == "warning") current_level_ = LogLevel::Warn;
    else if (level_name == "error") current_level_ = LogLevel::Error;
    else if (level_name == "critical" || level_name == "fatal") current_level_ = LogLevel::Critical;
    else current_level_ = LogLevel::Info;

#if ANONX_HAS_SPDLOG
    try {
        std::vector<spdlog::sink_ptr> sinks;
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        sinks.push_back(console_sink);

        if (!log_file.empty()) {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(std::string(log_file), true);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
            sinks.push_back(file_sink);
        }

        auto combined_logger = std::make_shared<spdlog::logger>("anonx", sinks.begin(), sinks.end());
        spdlog::set_default_logger(combined_logger);

        switch (current_level_) {
            case LogLevel::Trace:    spdlog::set_level(spdlog::level::trace); break;
            case LogLevel::Debug:    spdlog::set_level(spdlog::level::debug); break;
            case LogLevel::Info:     spdlog::set_level(spdlog::level::info); break;
            case LogLevel::Warn:     spdlog::set_level(spdlog::level::warn); break;
            case LogLevel::Error:    spdlog::set_level(spdlog::level::err); break;
            case LogLevel::Critical: spdlog::set_level(spdlog::level::critical); break;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Failed to initialize spdlog sinks: " << ex.what() << std::endl;
    }
#endif

    initialized_ = true;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_level_ = level;
#if ANONX_HAS_SPDLOG
    switch (level) {
        case LogLevel::Trace:    spdlog::set_level(spdlog::level::trace); break;
        case LogLevel::Debug:    spdlog::set_level(spdlog::level::debug); break;
        case LogLevel::Info:     spdlog::set_level(spdlog::level::info); break;
        case LogLevel::Warn:     spdlog::set_level(spdlog::level::warn); break;
        case LogLevel::Error:    spdlog::set_level(spdlog::level::err); break;
        case LogLevel::Critical: spdlog::set_level(spdlog::level::critical); break;
    }
#endif
}

LogLevel Logger::level() const noexcept {
    return current_level_;
}

void Logger::log(LogLevel level, std::string_view tag, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(current_level_)) {
        return;
    }

#if ANONX_HAS_SPDLOG
    std::string formatted = fmt::format("[{}] {}", tag, message);
    switch (level) {
        case LogLevel::Trace:    spdlog::trace(formatted); break;
        case LogLevel::Debug:    spdlog::debug(formatted); break;
        case LogLevel::Info:     spdlog::info(formatted); break;
        case LogLevel::Warn:     spdlog::warn(formatted); break;
        case LogLevel::Error:    spdlog::error(formatted); break;
        case LogLevel::Critical: spdlog::critical(formatted); break;
    }
#else
    std::lock_guard<std::mutex> lock(mutex_);
    const char* color = level_to_color(level);
    std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;

    out << color << "[" << current_timestamp() << "] "
        << "[" << level_to_string(level) << "] "
        << "[" << tag << "] "
        << message << "\033[0m\n";
    out.flush();
#endif
}

} // namespace anonx::core
