// AnonXMusic C++ port — Phase 5 (voice + queue)
// test/fake_ntgcalls/fake_voice_transport.hpp
//
// A scripted, in-memory VoiceTransport for offline testing. Because
// VoiceTransport is a plain abstract C++ class (unlike TDLib's C ABI, which
// needed a fake shared library in Phase 4), the fake is simply a subclass — no
// native stubbing required.
//
// It records every call, lets a test dictate the PlayResult and pause/resume
// outcomes, and can fire the two engine callbacks (stream-ended, call-closed)
// on demand so the auto-advance / loop / stop-on-close logic in CallManager can
// be exercised deterministically without any network or NTgCalls dependency.

#ifndef ANONX_TEST_FAKE_VOICE_TRANSPORT_HPP
#define ANONX_TEST_FAKE_VOICE_TRANSPORT_HPP

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "anonx/voice_transport.hpp"

namespace anonx {

class FakeVoiceTransport : public VoiceTransport {
public:
    struct ChatState {
        int         playCount   = 0;
        int         stopCount   = 0;
        int         pauseCount  = 0;
        int         resumeCount = 0;
        bool        joined      = false;
        MediaSource lastSource;
    };

    // --- dials a test can set ---
    PlayResult playResult = PlayResult::Ok;  // returned by every play()
    bool       pauseOk    = true;            // return value of pause()
    bool       resumeOk   = true;            // return value of resume()
    double     pingValue  = 42.0;            // returned by ping()

    // --- aggregate counters ---
    int totalPlays  = 0;
    int totalStops  = 0;

    // --- VoiceTransport interface ---
    PlayResult play(std::int64_t chatId, const MediaSource& src) override {
        auto& st = chats[chatId];
        st.playCount++;
        st.lastSource = src;
        ++totalPlays;
        if (playResult == PlayResult::Ok)
            st.joined = true;
        return playResult;
    }

    bool pause(std::int64_t chatId) override {
        chats[chatId].pauseCount++;
        return pauseOk;
    }

    bool resume(std::int64_t chatId) override {
        chats[chatId].resumeCount++;
        return resumeOk;
    }

    void stop(std::int64_t chatId) override {
        auto& st = chats[chatId];
        st.stopCount++;
        st.joined = false;
        ++totalStops;
    }

    double ping() const override { return pingValue; }

    void setStreamEndHandler(StreamEndHandler handler) override {
        streamEnd_ = std::move(handler);
    }
    void setCallClosedHandler(CallClosedHandler handler) override {
        callClosed_ = std::move(handler);
    }

    // --- test-side event injection ---
    void fireStreamEnd(std::int64_t chatId, StreamKind kind = StreamKind::Audio) {
        if (streamEnd_)
            streamEnd_(chatId, kind);
    }
    void fireCallClosed(std::int64_t chatId) {
        if (callClosed_)
            callClosed_(chatId);
    }

    const ChatState& state(std::int64_t chatId) { return chats[chatId]; }

private:
    std::unordered_map<std::int64_t, ChatState> chats;
    StreamEndHandler  streamEnd_;
    CallClosedHandler callClosed_;
};

}  // namespace anonx

#endif  // ANONX_TEST_FAKE_VOICE_TRANSPORT_HPP
