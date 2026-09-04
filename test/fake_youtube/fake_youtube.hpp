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

    std::optional<Track> nextSearch;
    std::vector<Track>   nextPlaylist;
    bool                 downloadFails = false;

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
        for (Track& t : out) {
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

}

#endif
