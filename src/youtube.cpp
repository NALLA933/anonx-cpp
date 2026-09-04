// AnonXMusic C++ port — Phase 3
// youtube.cpp — implementation of the YouTube service (yt-dlp subprocess).

#include "anonx/youtube.hpp"

#include "anonx/logger.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>     // opendir / readdir
#include <sys/wait.h>   // WIFEXITED / WEXITSTATUS
#include <unistd.h>     // access

namespace anonx {
namespace {

using nlohmann::json;

// ---- small JSON accessors (guarded, never throw) ----
std::string jstr(const json& j, const char* key) {
    if (j.contains(key)) {
        const json& v = j[key];
        if (v.is_string()) return v.get<std::string>();
    }
    return "";
}

double jnum(const json& j, const char* key) {
    if (j.contains(key)) {
        const json& v = j[key];
        if (v.is_number()) return v.get<double>();
    }
    return 0.0;
}

// ---- string / time helpers ----
std::string formatSeconds(int s) {
    if (s < 0) s = 0;
    const int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, sec);
    else       std::snprintf(buf, sizeof(buf), "%d:%02d", m, sec);
    return std::string(buf);
}

// Parse "H:M:S" / "M:S" / "S" -> seconds (mirrors utils.to_seconds).
int toSeconds(const std::string& t) {
    std::vector<int> parts;
    std::stringstream ss(t);
    std::string p;
    while (std::getline(ss, p, ':')) {
        try {
            parts.push_back(std::stoi(p));
        } catch (...) {
            return 0;
        }
    }
    int total = 0, mul = 1;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        total += *it * mul;
        mul *= 60;
    }
    return total;
}

// Truncate to at most `maxCp` UTF-8 code points without splitting a character.
std::string utf8Truncate(const std::string& s, std::size_t maxCp) {
    std::size_t cp = 0, i = 0;
    while (i < s.size() && cp < maxCp) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        i += len;
        ++cp;
    }
    if (i > s.size()) i = s.size();
    return s.substr(0, i);
}

std::string stripQuery(std::string u) {
    const auto p = u.find('?');
    if (p != std::string::npos) u = u.substr(0, p);
    return u;
}

// Wrap an argument in single quotes for /bin/sh, escaping embedded quotes.
std::string shellQuote(const std::string& s) {
    std::string r = "'";
    for (const char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    r += "'";
    return r;
}

bool fileExists(const std::string& path) {
    return ::access(path.c_str(), F_OK) == 0;
}

// Run `cmd`, capture stdout, and (optionally) report the process exit code.
std::string runCapture(const std::string& cmd, int* exitCode) {
    std::string out;
    std::FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        if (exitCode) *exitCode = -1;
        return out;
    }
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
        out.append(buf, n);
    }
    const int status = ::pclose(pipe);
    if (exitCode) {
        if (status == -1) *exitCode = -1;
        else if (WIFEXITED(status)) *exitCode = WEXITSTATUS(status);
        else *exitCode = -1;
    }
    return out;
}

}  // namespace

YouTube::YouTube() : rng_(std::random_device{}()) {
    // Ported from the Python regexes. Compilation is guarded so a bad build of
    // std::regex can never crash the process — valid()/invalid() just degrade.
    try {
        regex_ = std::regex(
            R"((https?://)?(www\.|m\.|music\.)?(youtube\.com/(watch\?v=|shorts/|playlist\?list=)|youtu\.be/)([A-Za-z0-9_-]{11}|PL[A-Za-z0-9_-]+)([&?][^\s]*)?)");
        iregex_ = std::regex(
            R"(https?://(?:www\.|m\.|music\.)?(?:youtube\.com|youtu\.be)(?!/(watch\?v=[A-Za-z0-9_-]{11}|shorts/[A-Za-z0-9_-]{11}|playlist\?list=PL[A-Za-z0-9_-]+|[A-Za-z0-9_-]{11}))\S*)");
        regexOk_ = true;
    } catch (const std::regex_error&) {
        regexOk_ = false;
    }
}

std::string YouTube::runCommand(const std::string& cmd) {
    return runCapture(cmd, nullptr);
}

std::optional<Track> YouTube::parseTrackJson(const std::string& jsonText, bool video) {
    json j = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;

    const std::string id = jstr(j, "id");
    if (id.empty()) return std::nullopt;

    Track t;
    t.id = id;
    t.video = video;

    // channel name: prefer "channel", fall back to "uploader"
    t.channel_name = jstr(j, "channel");
    if (t.channel_name.empty()) t.channel_name = jstr(j, "uploader");

    // duration: yt-dlp emits seconds as a number; "duration_string" is "M:SS"
    int secs = 0;
    if (j.contains("duration") && j["duration"].is_number()) {
        secs = static_cast<int>(jnum(j, "duration"));
    }
    const std::string durStr = jstr(j, "duration_string");
    if (secs == 0 && !durStr.empty()) secs = toSeconds(durStr);
    t.duration_sec = secs;
    t.duration = !durStr.empty() ? durStr : formatSeconds(secs);

    // title — faithfully truncated to 25 code points, as in the original
    t.title = utf8Truncate(jstr(j, "title"), 25);

    // canonical single-video URL
    t.url = "https://www.youtube.com/watch?v=" + id;

    // thumbnail: scalar "thumbnail" (query stripped) if present, else derived
    const std::string thumb = jstr(j, "thumbnail");
    t.thumbnail = !thumb.empty()
                      ? stripQuery(thumb)
                      : ("https://i.ytimg.com/vi/" + id + "/hqdefault.jpg");

    // view_count: yt-dlp gives a raw number; store it as a string ("" if absent)
    if (j.contains("view_count") && j["view_count"].is_number()) {
        t.view_count = std::to_string(static_cast<long long>(jnum(j, "view_count")));
    }

    return t;
}

std::optional<Track> YouTube::search(const std::string& query,
                                     std::int64_t messageId, bool video) {
    const std::string cmd = "yt-dlp " + shellQuote("ytsearch1:" + query) +
                            " --dump-json --no-download --no-warnings 2>/dev/null";
    const std::string out = runCommand(cmd);

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        auto t = parseTrackJson(line, video);
        if (t) {
            t->message_id = messageId;
            return t;
        }
        break;  // first non-empty line was not usable
    }
    return std::nullopt;
}

std::vector<Track> YouTube::playlist(const std::string& url, int limit,
                                     const std::string& user, bool video) {
    std::vector<Track> tracks;
    if (limit <= 0) return tracks;

    const std::string cmd = "yt-dlp " + shellQuote(url) +
                            " --flat-playlist --dump-json --no-download --no-warnings 2>/dev/null";
    const std::string out = runCommand(cmd);

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line) && static_cast<int>(tracks.size()) < limit) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        auto t = parseTrackJson(line, video);
        if (t) {
            t->user = user;
            tracks.push_back(*t);
        }
    }
    return tracks;
}

std::optional<std::string> YouTube::download(const std::string& videoId, bool video) {
    const std::string ext = video ? "mp4" : "webm";
    const std::string filename = std::string(kDownloadsDir) + "/" + videoId + "." + ext;

    // CRITICAL: never re-download something we already have.
    if (fileExists(filename)) return filename;

    const std::string url = base_ + videoId;
    const std::string selector =
        video ? "(bestvideo[height<=?720][width<=?1280][ext=mp4])+(bestaudio)"
              : "bestaudio[ext=webm][acodec=opus]";

    std::string cmd = "yt-dlp " + shellQuote(url) +
                      " --no-playlist --geo-bypass --no-warnings --no-check-certificate";
    cmd += " -f " + shellQuote(selector);
    if (video) cmd += " --merge-output-format mp4";
    cmd += " -o " + shellQuote(std::string(kDownloadsDir) + "/%(id)s.%(ext)s");

    const std::string cookie = pickCookie();
    if (!cookie.empty()) cmd += " --cookies " + shellQuote(cookie);
    cmd += " 2>/dev/null";

    int code = -1;
    runCapture(cmd, &code);
    if (code == 0 && fileExists(filename)) return filename;
    return std::nullopt;
}

std::string YouTube::pickCookie() {
    if (!cookiesScanned_) {
        const char* dirs[] = {"cookies", "anony/cookies"};
        for (const char* dir : dirs) {
            DIR* d = ::opendir(dir);
            if (!d) continue;
            struct dirent* e;
            while ((e = ::readdir(d)) != nullptr) {
                const std::string name = e->d_name;
                if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0) {
                    cookies_.push_back(std::string(dir) + "/" + name);
                }
            }
            ::closedir(d);
        }
        cookiesScanned_ = true;
    }

    if (cookies_.empty()) {
        if (!warnedNoCookies_) {
            warnedNoCookies_ = true;
            Logger("anonx.youtube").warning("Cookies are missing; downloads might fail.");
        }
        return "";
    }

    std::uniform_int_distribution<std::size_t> dist(0, cookies_.size() - 1);
    return cookies_[dist(rng_)];
}

bool YouTube::valid(const std::string& url) const {
    if (!regexOk_) return false;
    return std::regex_search(url, regex_, std::regex_constants::match_continuous);
}

bool YouTube::invalid(const std::string& url) const {
    if (!regexOk_) return false;
    return std::regex_search(url, iregex_, std::regex_constants::match_continuous);
}

}  // namespace anonx
