#ifndef SENPAI_LOGGER_HPP
#define SENPAI_LOGGER_HPP

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

namespace senpai {

enum class LogLevel {
    Debug = 10,
    Info = 20,
    Warning = 30,
    Error = 40,
    Critical = 50,
};

class LogSink {
public:
    static LogSink& instance();

    void init(const std::string& filePath = "log.txt",
              std::size_t maxBytes = 10u * 1024u * 1024u,
              int backupCount = 5,
              LogLevel minLevel = LogLevel::Info);

    void setLevel(LogLevel lvl);
    LogLevel level() const;

    void write(LogLevel lvl, const std::string& name, const std::string& message);

    void close();

private:
    LogSink() = default;
    ~LogSink();
    LogSink(const LogSink&) = delete;
    LogSink& operator=(const LogSink&) = delete;

    void ensureOpenLocked();
    void rotateLocked();

    mutable std::mutex mtx_;
    std::string filePath_ = "log.txt";
    std::size_t maxBytes_ = 10u * 1024u * 1024u;
    int backupCount_ = 5;
    LogLevel minLevel_ = LogLevel::Info;
    std::FILE* file_ = nullptr;
    std::size_t curSize_ = 0;
};

class Logger {
public:
    explicit Logger(std::string name) : name_(std::move(name)) {}

    void debug(const std::string& msg) const    { LogSink::instance().write(LogLevel::Debug, name_, msg); }
    void info(const std::string& msg) const      { LogSink::instance().write(LogLevel::Info, name_, msg); }
    void warning(const std::string& msg) const   { LogSink::instance().write(LogLevel::Warning, name_, msg); }
    void error(const std::string& msg) const      { LogSink::instance().write(LogLevel::Error, name_, msg); }
    void critical(const std::string& msg) const   { LogSink::instance().write(LogLevel::Critical, name_, msg); }

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

}

#endif
