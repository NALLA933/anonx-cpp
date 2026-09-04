#ifndef SENPAI_NTGCALLS_TRANSPORT_HPP
#define SENPAI_NTGCALLS_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

namespace senpai {

enum class AudioQuality { Low, Medium, High };

enum class VideoQuality { SD_360p, SD_480p, HD_720p, FHD_1080p };

struct MediaSource {
    std::string  path;
    bool         video = false;
    int          seekSeconds = 0;
    AudioQuality audio = AudioQuality::High;
    VideoQuality videoQuality = VideoQuality::HD_720p;
};

enum class StreamKind { Audio, Video };

enum class PlayResult {
    Ok,
    FileNotFound,
    NoActiveGroupCall,
    NoAudioSource,
    ServerError,
    RtmpUnsupported,
};

struct VoiceError : std::runtime_error {
    PlayResult category;
    explicit VoiceError(PlayResult cat, const std::string& what = "voice error")
        : std::runtime_error(what), category(cat) {}
};

class NtgCallsTransport {
public:
    using StreamEndHandler  = std::function<void(std::int64_t chatId, StreamKind kind)>;
    using CallClosedHandler = std::function<void(std::int64_t chatId)>;

    struct Signaling {
        std::function<std::string(std::int64_t chatId, const std::string& localParams)>
            joinGroupCall;
        std::function<void(std::int64_t chatId)> leaveGroupCall;
    };

    explicit NtgCallsTransport(Signaling signaling);
    ~NtgCallsTransport();

    NtgCallsTransport(const NtgCallsTransport&)            = delete;
    NtgCallsTransport& operator=(const NtgCallsTransport&) = delete;

    PlayResult play(std::int64_t chatId, const MediaSource& src);
    bool       pause(std::int64_t chatId);
    bool       resume(std::int64_t chatId);
    void       stop(std::int64_t chatId);
    double     ping() const;
    void       setStreamEndHandler(StreamEndHandler handler);
    void       setCallClosedHandler(CallClosedHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}

#endif
