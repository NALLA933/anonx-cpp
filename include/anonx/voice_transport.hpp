#ifndef ANONX_VOICE_TRANSPORT_HPP
#define ANONX_VOICE_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace anonx {

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

class VoiceTransport {
public:
    using StreamEndHandler  = std::function<void(std::int64_t chatId, StreamKind kind)>;
    using CallClosedHandler = std::function<void(std::int64_t chatId)>;

    virtual ~VoiceTransport() = default;

    virtual PlayResult play(std::int64_t chatId, const MediaSource& src) = 0;

    virtual bool pause(std::int64_t chatId)  = 0;
    virtual bool resume(std::int64_t chatId) = 0;

    virtual void stop(std::int64_t chatId) = 0;

    virtual double ping() const = 0;

    virtual void setStreamEndHandler(StreamEndHandler handler) = 0;

    virtual void setCallClosedHandler(CallClosedHandler handler) = 0;
};

}

#endif
