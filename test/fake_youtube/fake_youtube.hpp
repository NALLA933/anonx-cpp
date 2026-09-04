// AnonXMusic C++ port — Phase 6a (command plugins)
// test/fake_youtube/fake_youtube.hpp
//
// A scripted YouTube service for offline testing. The real one shells out to
// yt-dlp, which a hermetic test cannot rely on, so the three subprocess-backed
// methods are overridden with canned results. valid()/invalid() are deliberately
// NOT overridden — those are pure regex, so the tests still exercise the real URL
// classification that the /play preflight depends on.

#ifndef ANONX_TEST_FAKE_YOUTUBE_HPP
#define ANONX_TEST_FAKE_YOUTUBE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "anonx/youtube.hpp"

namespace anonx {

class FakeYouTube : public YouTube {
public:
    // --- dials ---
    std::optional<Track> nextSearch;      // result of the next search() (nullopt = miss)
    std::vector<Track>   nextPlaylist;    // result of the next playlist()
    bool                 downloadFails = false;

    // --- recorded state ---
    int         searchCalls = 0;
    int         playlistCalls = 0;
    int         downloadCalls = 0;
    std::string lastQuery;
    std::int64_t lastMessageId = 0;
    bool        lastVideo = false;
    int         lastLimit = 0;
    std::vector<std::string> downloaded;

    std::optional<Track> search(const std::string& query, std::int64_t messageId,
                                bool video) override {
        ++searchCalls;
        lastQuery = query;
        lastMessageId = messageId;
        lastVideo = video;
        return nextSearch;
    }

    std::vector<Track> playlist(const std::string& url, int limit,
                                const std::string& user, bool video) override {
        ++playlistCalls;
        lastQuery = url;
        lastLimit = limit;
        lastVideo = video;
        std::vector<Track> out = nextPlaylist;
        if (limit > 0 && static_cast<int>(out.size()) > limit)
            out.resize(static_cast<std::size_t>(limit));
        for (Track& t : out) {   // the real playlist() stamps these too
            t.user  = user;
            t.video = video;
        }
        return out;
    }

    std::optional<std::string> download(const std::string& videoId, bool video) override {
        ++downloadCalls;
        downloaded.push_back(videoId);
        if (downloadFails)
            return std::nullopt;
        return std::string("downloads/") + videoId + (video ? ".mp4" : ".webm");
    }
};

}  // namespace anonx

#endif  // ANONX_TEST_FAKE_YOUTUBE_HPP
