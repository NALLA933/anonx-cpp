#ifndef SENPAI_SYSINFO_HPP
#define SENPAI_SYSINFO_HPP

#include <chrono>
#include <cstdint>
#include <string>

namespace senpai {

class SystemInfo {
public:
    SystemInfo();
    ~SystemInfo() = default;

    double cpuPercent();

    double       ramPercent();
    std::int64_t ramUsedMb();
    double       ramTotalGb();

    double diskPercent();
    double diskUsedGb();
    double diskTotalGb();

    int         cores();
    std::string platform();

    std::int64_t uptimeSeconds();

    std::string toolchainVersion();
    std::string telegramLibrary();
    std::string voiceLibrary();

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
