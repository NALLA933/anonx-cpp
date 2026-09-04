#ifndef SENPAI_NTGCALLS_TRANSPORT_HPP
#define SENPAI_NTGCALLS_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "senpai/voice_transport.hpp"

namespace senpai {

class NtgCallsTransport : public VoiceTransport {
public:

    struct Signaling {

        std::function<std::string(std::int64_t chatId, const std::string& localParams)>
            joinGroupCall;

        std::function<void(std::int64_t chatId)> leaveGroupCall;
    };

    explicit NtgCallsTransport(Signaling signaling);
    ~NtgCallsTransport() override;

    NtgCallsTransport(const NtgCallsTransport&)            = delete;
    NtgCallsTransport& operator=(const NtgCallsTransport&) = delete;

    PlayResult play(std::int64_t chatId, const MediaSource& src) override;
    bool       pause(std::int64_t chatId) override;
    bool       resume(std::int64_t chatId) override;
    void       stop(std::int64_t chatId) override;
    double     ping() const override;
    void       setStreamEndHandler(StreamEndHandler handler) override;
    void       setCallClosedHandler(CallClosedHandler handler) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
