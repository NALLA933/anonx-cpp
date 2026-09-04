#include "anonx/ntgcalls_transport.hpp"

#include <mutex>
#include <string>

#if defined(ANONX_WITH_NTGCALLS)

#include <ntgcalls/ntgcalls.hpp>

namespace anonx {
namespace {

void audioParamsFor(AudioQuality q, int& sampleRate, int& bits, int& channels) {
    bits     = 16;
    channels = 2;
    switch (q) {
        case AudioQuality::Low:    sampleRate = 24000; break;
        case AudioQuality::Medium: sampleRate = 36000; break;
        case AudioQuality::High:   sampleRate = 48000; break;
    }
}

void videoParamsFor(VideoQuality q, int& w, int& h, int& fps) {
    fps = 30;
    switch (q) {
        case VideoQuality::SD_360p:   w = 640;  h = 360;  break;
        case VideoQuality::SD_480p:   w = 854;  h = 480;  break;
        case VideoQuality::HD_720p:   w = 1280; h = 720;  break;
        case VideoQuality::FHD_1080p: w = 1920; h = 1080; break;
    }
}

std::string shellEscape(const std::string& s) {
    std::string r = "'";
    for (const char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    r += "'";
    return r;
}

ntgcalls::MediaDescription buildMedia(const MediaSource& s) {
    const std::string seek =
        s.seekSeconds > 1 ? ("-ss " + std::to_string(s.seekSeconds) + " ") : "";

    ntgcalls::MediaDescription desc;

    int aRate, aBits, aChans;
    audioParamsFor(s.audio, aRate, aBits, aChans);
    ntgcalls::AudioDescription audio;
    audio.mediaSource   = ntgcalls::BaseMediaDescription::MediaSource::Shell;
    audio.sampleRate    = static_cast<uint32_t>(aRate);
    audio.bitsPerSample = static_cast<uint8_t>(aBits);
    audio.channelCount  = static_cast<uint8_t>(aChans);
    audio.input = "ffmpeg -nostdin " + seek + "-i " + shellEscape(s.path) +
                  " -f s16le -ac " + std::to_string(aChans) +
                  " -ar " + std::to_string(aRate) + " -loglevel quiet pipe:1";
    desc.audio = audio;

    if (s.video) {
        int vw, vh, vfps;
        videoParamsFor(s.videoQuality, vw, vh, vfps);
        ntgcalls::VideoDescription video;
        video.mediaSource = ntgcalls::BaseMediaDescription::MediaSource::Shell;
        video.width       = static_cast<uint16_t>(vw);
        video.height      = static_cast<uint16_t>(vh);
        video.fps         = static_cast<uint8_t>(vfps);
        video.input = "ffmpeg -nostdin " + seek + "-i " + shellEscape(s.path) +
                      " -f rawvideo -pix_fmt yuv420p -vf scale=" +
                      std::to_string(vw) + ":" + std::to_string(vh) +
                      " -r " + std::to_string(vfps) + " -loglevel quiet pipe:1";
        desc.video = video;
    }
    return desc;
}

}

struct NtgCallsTransport::Impl {
    explicit Impl(Signaling sig) : signaling(std::move(sig)) {

        instance.onStreamEnd(
            [this](std::int64_t chatId, ntgcalls::StreamManager::Type type) {
                StreamKind kind = (type == ntgcalls::StreamManager::Type::Playback)
                                      ? StreamKind::Audio
                                      : StreamKind::Video;
                StreamEndHandler h;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    h = onStreamEnd;
                }
                if (h && kind == StreamKind::Audio)
                    h(chatId, kind);
            });

        instance.onConnectionChange(
            [this](std::int64_t chatId, ntgcalls::NetworkInfo state) {
                if (state.state == ntgcalls::ConnectionState::Closed ||
                    state.state == ntgcalls::ConnectionState::Failed ||
                    state.state == ntgcalls::ConnectionState::Timeout) {
                    CallClosedHandler h;
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        h = onCallClosed;
                    }
                    if (h)
                        h(chatId);
                }
            });
    }

    ntgcalls::NTgCalls              instance;
    Signaling                       signaling;
    mutable std::mutex              mtx;
    StreamEndHandler                onStreamEnd;
    CallClosedHandler               onCallClosed;
};

NtgCallsTransport::NtgCallsTransport(Signaling signaling)
    : impl_(std::make_unique<Impl>(std::move(signaling))) {}

NtgCallsTransport::~NtgCallsTransport() = default;

PlayResult NtgCallsTransport::play(std::int64_t chatId, const MediaSource& src) {
    try {
        auto media = buildMedia(src);

        const std::string localParams = impl_->instance.createCall(chatId, std::move(media));

        const std::string remoteParams =
            impl_->signaling.joinGroupCall
                ? impl_->signaling.joinGroupCall(chatId, localParams)
                : throw VoiceError(PlayResult::ServerError, "no join signaling wired");

        impl_->instance.connect(chatId, remoteParams);
        return PlayResult::Ok;
    }

    catch (const VoiceError& e) {
        return e.category;
    }

    catch (const ntgcalls::RTMPNeeded&)         { return PlayResult::RtmpUnsupported; }
    catch (const ntgcalls::FileError&)          { return PlayResult::FileNotFound; }
    catch (const ntgcalls::TelegramServerError&){ return PlayResult::ServerError; }
    catch (const ntgcalls::ConnectionError&)    { return PlayResult::ServerError; }
    catch (const std::exception&)               { return PlayResult::ServerError; }
}

bool NtgCallsTransport::pause(std::int64_t chatId) {
    try {
        return impl_->instance.pause(chatId);
    } catch (const std::exception&) {
        return false;
    }
}

bool NtgCallsTransport::resume(std::int64_t chatId) {
    try {
        return impl_->instance.resume(chatId);
    } catch (const std::exception&) {
        return false;
    }
}

void NtgCallsTransport::stop(std::int64_t chatId) {

    try {
        if (impl_->signaling.leaveGroupCall)
            impl_->signaling.leaveGroupCall(chatId);
    } catch (const std::exception&) {
    }
    try {
        impl_->instance.stop(chatId);
    } catch (const std::exception&) {
    }
}

double NtgCallsTransport::ping() const {

    return 0.0;
}

void NtgCallsTransport::setStreamEndHandler(StreamEndHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->onStreamEnd = std::move(handler);
}

void NtgCallsTransport::setCallClosedHandler(CallClosedHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->onCallClosed = std::move(handler);
}

}

#else

namespace anonx {

struct NtgCallsTransport::Impl {
    explicit Impl(Signaling sig) : signaling(std::move(sig)) {}
    Signaling         signaling;
    StreamEndHandler  onStreamEnd;
    CallClosedHandler onCallClosed;
};

NtgCallsTransport::NtgCallsTransport(Signaling signaling)
    : impl_(std::make_unique<Impl>(std::move(signaling))) {}
NtgCallsTransport::~NtgCallsTransport() = default;

PlayResult NtgCallsTransport::play(std::int64_t, const MediaSource&) {

    return PlayResult::ServerError;
}
bool   NtgCallsTransport::pause(std::int64_t)  { return false; }
bool   NtgCallsTransport::resume(std::int64_t) { return false; }
void   NtgCallsTransport::stop(std::int64_t)   {}
double NtgCallsTransport::ping() const         { return 0.0; }
void   NtgCallsTransport::setStreamEndHandler(StreamEndHandler h)  { impl_->onStreamEnd = std::move(h); }
void   NtgCallsTransport::setCallClosedHandler(CallClosedHandler h){ impl_->onCallClosed = std::move(h); }

}

#endif
