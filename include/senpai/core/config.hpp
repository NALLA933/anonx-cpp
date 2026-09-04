#ifndef SENPAI_CONFIG_HPP
#define SENPAI_CONFIG_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace senpai {

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& msg) : std::runtime_error(msg) {}
};

class Config {
public:

    std::int64_t api_id = 0;
    std::string  api_hash;
    std::string  bot_token;
    std::int64_t logger_id = 0;
    std::int64_t owner_id = 0;
    std::string  session1;
    std::string  session2;
    std::string  session3;

    std::string  session_name = "assistant";
    std::string  data_dir     = "./data/tdlib_session";

    std::string  phone1;
    std::string  phone2;
    std::string  phone3;

    std::string db_path = "senpai.db";

    int duration_limit_seconds = 60 * 60;
    int queue_limit = 20;
    int playlist_limit = 20;

    std::string support_channel = "https://t.me/fallenx";
    std::string support_chat    = "https://t.me/DevilsHeavenMF";

    bool auto_leave = false;
    bool auto_end   = false;
    bool thumb_gen  = true;
    bool video_play = true;

    std::string lang_code = "en";

    std::vector<std::string> cookies_url;
    std::string default_thumb = "https://te.legra.ph/file/3e40a408286d4eda24191.jpg";
    std::string ping_img      = "https://files.catbox.moe/haagg2.png";
    std::string start_img     = "https://files.catbox.moe/zvziwk.jpg";

    static Config load(const std::string& envFile = ".env");

    void check() const;

    int assistantCount() const;

    std::vector<std::string> assistantPhones() const;

    std::string redactedSummary() const;
};

}

#endif
