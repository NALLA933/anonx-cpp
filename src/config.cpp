#include "anonx/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace anonx {
namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    auto isws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (b < e && isws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isws(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2) {
        char f = s.front();
        char l = s.back();
        if ((f == '"' && l == '"') || (f == '\'' && l == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::unordered_map<std::string, std::string> parseEnvFile(const std::string& path) {
    std::unordered_map<std::string, std::string> out;
    if (path.empty()) return out;

    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t.rfind("export ", 0) == 0) {
            t = trim(t.substr(7));
        }

        std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(t.substr(0, eq));
        if (key.empty()) continue;

        std::string val = stripQuotes(trim(t.substr(eq + 1)));
        out[key] = val;
    }
    return out;
}

class EnvSource {
public:
    explicit EnvSource(std::unordered_map<std::string, std::string> dotenv)
        : dotenv_(std::move(dotenv)) {}

    std::string str(const char* key, const std::string& def = "") const {
        if (const char* e = std::getenv(key)) return std::string(e);
        auto it = dotenv_.find(key);
        if (it != dotenv_.end()) return it->second;
        return def;
    }

    std::int64_t integer(const char* key, std::int64_t def) const {
        std::string v = str(key);
        if (v.empty()) return def;
        try {
            std::size_t pos = 0;
            long long parsed = std::stoll(v, &pos);
            return static_cast<std::int64_t>(parsed);
        } catch (...) {
            return def;
        }
    }

    bool boolean(const char* key, bool def) const {
        std::string v = str(key);
        if (v.empty()) return def;
        return toLower(v) == "true";
    }

private:
    std::unordered_map<std::string, std::string> dotenv_;
};

}

Config Config::load(const std::string& envFile) {
    EnvSource env(parseEnvFile(envFile));
    Config c;

    c.api_id    = env.integer("API_ID", 0);
    c.api_hash  = env.str("API_HASH");
    c.bot_token = env.str("BOT_TOKEN");
    c.logger_id = env.integer("LOGGER_ID", 0);
    c.owner_id  = env.integer("OWNER_ID", 0);

    c.session1 = env.str("SESSION");
    c.session2 = env.str("SESSION2");
    c.session3 = env.str("SESSION3");

    c.session_name = env.str("SESSION_NAME");
    c.data_dir     = env.str("DATA_DIR", env.str("SESSION_DIR", ""));

    if (c.session1.empty()) {
        if (!c.session_name.empty()) {
            c.session1 = c.session_name;
        } else if (!c.data_dir.empty()) {
            c.session1 = c.data_dir;
        }
    }
    if (c.session_name.empty()) {
        c.session_name = !c.session1.empty() ? c.session1 : "assistant";
    }
    if (c.data_dir.empty()) {
        c.data_dir = "./data/tdlib_session";
    }

    c.phone1 = env.str("PHONE_NUMBER");
    c.phone2 = env.str("PHONE_NUMBER2");
    c.phone3 = env.str("PHONE_NUMBER3");

    c.db_path = env.str("DB_PATH", "anonx.db");

    c.duration_limit_seconds = static_cast<int>(env.integer("DURATION_LIMIT", 60)) * 60;
    c.queue_limit    = static_cast<int>(env.integer("QUEUE_LIMIT", 20));
    c.playlist_limit = static_cast<int>(env.integer("PLAYLIST_LIMIT", 20));

    c.support_channel = env.str("SUPPORT_CHANNEL", "https://t.me/fallenx");
    c.support_chat    = env.str("SUPPORT_CHAT", "https://t.me/DevilsHeavenMF");

    c.auto_leave = env.boolean("AUTO_LEAVE", false);
    c.auto_end   = env.boolean("AUTO_END", false);
    c.thumb_gen  = env.boolean("THUMB_GEN", true);
    c.video_play = env.boolean("VIDEO_PLAY", true);

    c.lang_code = env.str("LANG_CODE", "en");

    {
        std::string raw = env.str("COOKIES_URL");
        std::istringstream iss(raw);
        std::string tok;
        while (iss >> tok) {
            if (tok.find("batbin.me") != std::string::npos) {
                c.cookies_url.push_back(tok);
            }
        }
    }

    c.default_thumb = env.str("DEFAULT_THUMB", c.default_thumb);
    c.ping_img      = env.str("PING_IMG", c.ping_img);
    c.ping_img      = env.str("PING_IMG_URL", c.ping_img);
    c.start_img     = env.str("START_IMG", c.start_img);
    c.start_img     = env.str("START_IMG_URL", c.start_img);

    return c;
}

void Config::check() const {
    std::vector<std::string> missing;
    if (api_id == 0)        missing.push_back("API_ID");
    if (api_hash.empty())   missing.push_back("API_HASH");
    if (bot_token.empty())  missing.push_back("BOT_TOKEN");
    if (logger_id == 0)     missing.push_back("LOGGER_ID");
    if (owner_id == 0)      missing.push_back("OWNER_ID");

    if (!missing.empty()) {
        std::string list;
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i) list += ", ";
            list += missing[i];
        }
        throw ConfigError("Missing required environment variables: " + list);
    }
}

int Config::assistantCount() const {
    int n = 0;
    if (!session1.empty() || !phone1.empty()) ++n;
    if (!session2.empty() || !phone2.empty()) ++n;
    if (!session3.empty() || !phone3.empty()) ++n;
    return n > 0 ? n : 1;
}

std::vector<std::string> Config::assistantPhones() const {
    std::vector<std::string> out;

    for (const std::string* p : {&phone1, &phone2, &phone3}) {
        if (!p->empty()) out.push_back(*p);
    }
    return out;
}

std::string Config::redactedSummary() const {
    auto yn = [](bool b) { return b ? "yes" : "no"; };
    std::ostringstream os;
    os << "config: owner_id=" << owner_id
       << " logger_id=" << logger_id
       << " lang=" << lang_code
       << " assistants=" << assistantCount()
       << " db=" << db_path
       << " duration_limit=" << (duration_limit_seconds / 60) << "min"
       << " queue_limit=" << queue_limit
       << " playlist_limit=" << playlist_limit
       << " auto_leave=" << yn(auto_leave)
       << " auto_end=" << yn(auto_end)
       << " thumb_gen=" << yn(thumb_gen)
       << " video_play=" << yn(video_play)
       << " cookies=" << cookies_url.size()
       << " | session=" << (!session1.empty() ? session1 : (!session_name.empty() ? session_name : "none"))
       << " data_dir=" << data_dir
       << " | secrets set: api_hash=" << yn(!api_hash.empty())
       << " bot_token=" << yn(!bot_token.empty())

       << " phones=" << assistantPhones().size();
    return os.str();
}

}
