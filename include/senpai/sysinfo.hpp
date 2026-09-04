#ifndef SENPAI_SYSINFO_HPP
#define SENPAI_SYSINFO_HPP

#include <chrono>
#include <cstdint>
#include <string>

namespace senpai {

class SystemInfo {
public:
    SystemInfo();
    virtual ~SystemInfo() = default;

    virtual double cpuPercent();

    virtual double       ramPercent();
    virtual std::int64_t ramUsedMb();
    virtual double       ramTotalGb();

    virtual double diskPercent();
    virtual double diskUsedGb();
    virtual double diskTotalGb();

    virtual int         cores();
    virtual std::string platform();

    virtual std::int64_t uptimeSeconds();

    virtual std::string toolchainVersion();
    virtual std::string telegramLibrary();
    virtual std::string voiceLibrary();

    static std::string formatDuration(std::int64_t seconds);

    static std::string round1(double value);

protected:

    static bool readCpuJiffies(std::uint64_t& total, std::uint64_t& idle);

private:
    std::chrono::steady_clock::time_point start_;
    std::uint64_t lastTotal_ = 0;
    std::uint64_t lastIdle_  = 0;
    bool          sampled_   = false;
};

}

#endif
