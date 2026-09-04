#ifndef SENPAI_NULL_VOICE_TRANSPORT_HPP
#define SENPAI_NULL_VOICE_TRANSPORT_HPP

#include <cstdint>
#include <utility>

#include "senpai/voice_transport.hpp"

namespace senpai {

class NullVoiceTransport : public VoiceTransport {
public:
    PlayResult play(std::int64_t, const MediaSource&) override {
        return PlayResult::ServerError;
    }

    bool pause(std::int64_t) override { return false; }
    bool resume(std::int64_t) override { return false; }

    void stop(std::int64_t) override {}
    double ping() const override { return 0.0; }

    void setStreamEndHandler(StreamEndHandler handler) override {
        onStreamEnd_ = std::move(handler);
    }
    void setCallClosedHandler(CallClosedHandler handler) override {
        onCallClosed_ = std::move(handler);
    }

private:
    StreamEndHandler  onStreamEnd_;
    CallClosedHandler onCallClosed_;
};

}

#endif
