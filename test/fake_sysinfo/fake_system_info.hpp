#ifndef ANONX_TEST_FAKE_SYSTEM_INFO_HPP
#define ANONX_TEST_FAKE_SYSTEM_INFO_HPP

#include <cstdint>
#include <string>

#include "anonx/sysinfo.hpp"

namespace anonx {

class FakeSystemInfo : public SystemInfo {
public:

    double       cpu        = 12.5;
    double       ramPct     = 43.2;
    std::int64_t ramMb      = 512;
    double       ramGb      = 7.8;
    double       diskPct    = 60.0;
    double       diskUsed   = 30.5;
    double       diskTotal  = 50.0;
    int          coreCount  = 4;
    std::string  platformId = "Linux 6.1.0 x86_64";
    std::int64_t uptime     = 3725;
    std::string  toolchain  = "C++17 (g++ 13.2.0)";
    std::string  telegram   = "TDLib (JSON interface)";
    std::string  voice      = "NTgCalls";

    int cpuCalls = 0;

    double       cpuPercent() override { ++cpuCalls; return cpu; }
    double       ramPercent() override { return ramPct; }
    std::int64_t ramUsedMb() override { return ramMb; }
    double       ramTotalGb() override { return ramGb; }
    double       diskPercent() override { return diskPct; }
    double       diskUsedGb() override { return diskUsed; }
    double       diskTotalGb() override { return diskTotal; }
    int          cores() override { return coreCount; }
    std::string  platform() override { return platformId; }
    std::int64_t uptimeSeconds() override { return uptime; }
    std::string  toolchainVersion() override { return toolchain; }
    std::string  telegramLibrary() override { return telegram; }
    std::string  voiceLibrary() override { return voice; }
};

}

#endif
