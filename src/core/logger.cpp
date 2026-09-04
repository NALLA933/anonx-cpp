#include "senpai/core/logger.hpp"

#include <cstdio>
#include <ctime>
#include <string>

namespace senpai {
namespace {

const char* levelName(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARNING";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
    }
    return "INFO";
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%d-%b-%y %H:%M:%S", &tmv) == 0) {
        return "??";
    }
    return std::string(buf);
}

}

LogSink& LogSink::instance() {
    static LogSink sink;
    return sink;
}

void LogSink::init(const std::string& /*filePath*/, std::size_t /*maxBytes*/,
                   int /*backupCount*/, LogLevel minLevel) {
    std::lock_guard<std::mutex> lk(mtx_);
    minLevel_ = minLevel;
}

void LogSink::setLevel(LogLevel lvl) {
    std::lock_guard<std::mutex> lk(mtx_);
    minLevel_ = lvl;
}

LogLevel LogSink::level() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return minLevel_;
}

void LogSink::write(LogLevel lvl, const std::string& name, const std::string& message) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (static_cast<int>(lvl) < static_cast<int>(minLevel_)) return;

    std::string line = "[" + timestamp() + " - " + levelName(lvl) + "] - " +
                       name + ": " + message + "\n";

    FILE* out = (lvl >= LogLevel::Error) ? stderr : stdout;
    std::fputs(line.c_str(), out);
    std::fflush(out);
}

void LogSink::close() {
    std::fflush(stdout);
    std::fflush(stderr);
}

}
