// AnonXMusic C++ port — Phase 3
// youtube.hpp — YouTube service.
//
// A thin launcher/parser around the `yt-dlp` binary, invoked as a subprocess.
// It never links or reimplements yt-dlp. If yt-dlp is not installed on PATH,
// every operation fails gracefully (search/playlist return empty, download
// returns nullopt) — the program still compiles and boots.
//
// Ported from anony/core/youtube.py, adapted from the py_yt/yt_dlp Python
// libraries to the yt-dlp command-line interface (--dump-json).

#ifndef ANONX_YOUTUBE_HPP
#define ANONX_YOUTUBE_HPP

#include <cstdint>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <vector>

namespace anonx {

// Track metadata, mirroring the Python dataclass `Track`.
struct Track {
    std::string  id;
    std::string  channel_name;
    std::string  duration = "00:00";   // human-readable "M:SS" / "H:MM:SS"
    int          duration_sec = 0;
    std::string  title;
    std::string  url;
    std::string  file_path;            // set once downloaded
    std::int64_t message_id = 0;
    std::int64_t time = 0;
    std::string  thumbnail;
    std::string  user;
    std::string  view_count;
    bool         video = false;
};

class YouTube {
public:
    YouTube();

    // The three subprocess-backed operations are virtual for the same reason
    // VoiceTransport and BotApi are abstract: a test build substitutes scripted
    // results and the whole command layer runs with no yt-dlp and no network.
    // (valid()/invalid() stay non-virtual — they are pure regex work, so tests
    // always exercise the real URL classification.)
    virtual ~YouTube() = default;

    // yt-dlp "ytsearch1:QUERY" --dump-json --no-download  ->  first result.
    // Returns nullopt if nothing was found or yt-dlp is unavailable.
    virtual std::optional<Track> search(const std::string& query,
                                        std::int64_t messageId = 0,
                                        bool video = false);

    // yt-dlp URL --flat-playlist --dump-json --no-download  ->  up to `limit`
    // tracks. Returns an empty vector on any failure.
    virtual std::vector<Track> playlist(const std::string& url, int limit,
                                        const std::string& user = "", bool video = false);

    // Download by video id. Returns the local file path, or nullopt on failure.
    // CRITICAL: if downloads/<id>.<ext> already exists it is returned
    // immediately, with no subprocess launched (no re-download).
    virtual std::optional<std::string> download(const std::string& videoId,
                                                bool video = false);

    // URL classification, ported from the Python regexes.
    bool valid(const std::string& url) const;    // a watch/shorts/playlist link
    bool invalid(const std::string& url) const;  // a YouTube link we can't handle

    // Parse ONE yt-dlp --dump-json object (a single JSON line) into a Track.
    // Pure and side-effect-free, so it can be unit-tested without the binary.
    // Returns nullopt if the text is not a usable object (e.g. missing id).
    static std::optional<Track> parseTrackJson(const std::string& jsonText, bool video);

    // Run a shell command and return its stdout (POSIX popen). Never throws;
    // returns "" if the command cannot be launched. Named per the project spec.
    static std::string runCommand(const std::string& cmd);

    static constexpr const char* kDownloadsDir = "downloads";

private:
    // Choose a random cookie file, or "" if none exist. The cookie directories
    // are scanned exactly once (result cached); a missing-cookies warning is
    // emitted at most once.
    std::string pickCookie();

    std::string  base_ = "https://www.youtube.com/watch?v=";
    std::vector<std::string> cookies_;
    bool         cookiesScanned_ = false;
    bool         warnedNoCookies_ = false;
    std::mt19937 rng_;

    std::regex   regex_;      // "valid" pattern
    std::regex   iregex_;     // "invalid" pattern
    bool         regexOk_ = false;
};

}  // namespace anonx

#endif  // ANONX_YOUTUBE_HPP
