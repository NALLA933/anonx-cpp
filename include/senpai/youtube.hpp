#ifndef SENPAI_YOUTUBE_HPP
#define SENPAI_YOUTUBE_HPP

#include <cstdint>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <vector>

namespace senpai {

struct Track {
    std::string  id;
    std::string  channel_name;
    std::string  duration = "00:00";
    int          duration_sec = 0;
    std::string  title;
    std::string  url;
    std::string  file_path;
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

    virtual ~YouTube() = default;

    virtual std::optional<Track> search(const std::string& query,
                                        std::int64_t messageId = 0,
                                        bool video = false);

    virtual std::vector<Track> playlist(const std::string& url, int limit,
                                        const std::string& user = "", bool video = false);

    virtual std::optional<std::string> download(const std::string& videoId,
                                                bool video = false);

    bool valid(const std::string& url) const;
    bool invalid(const std::string& url) const;

    static std::optional<Track> parseTrackJson(const std::string& jsonText, bool video);

    static std::string runCommand(const std::string& cmd);

    static constexpr const char* kDownloadsDir = "downloads";

private:

    std::string pickCookie();

    std::string  base_ = "https://www.youtube.com/watch?v=";
    std::vector<std::string> cookies_;
    bool         cookiesScanned_ = false;
    bool         warnedNoCookies_ = false;
    std::mt19937 rng_;

    std::regex   regex_;
    std::regex   iregex_;
    bool         regexOk_ = false;
};

}

#endif
