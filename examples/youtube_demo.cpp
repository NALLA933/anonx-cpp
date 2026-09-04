// AnonXMusic C++ port — Phase 3
// youtube_demo.cpp — automated, offline-safe checks for the YouTube service.
// Build target: `anonx_youtube_demo`.
//
// The deterministic checks (JSON parsing, URL classification, download
// cache-hit, runCommand) never touch the network or the yt-dlp binary. The
// graceful-degradation + warn-once checks only run when yt-dlp is NOT installed
// (i.e. in CI / this sandbox); on a machine that has yt-dlp they are skipped so
// the demo never makes a live network call.

#include "anonx/logger.hpp"
#include "anonx/youtube.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <sys/stat.h>   // mkdir

using namespace anonx;

// Always-on check (unaffected by -DNDEBUG in Release builds).
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr << "CHECK FAILED: " #cond " (" << __FILE__ << ":" \
                      << __LINE__ << ")\n";                             \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {

void section(const char* name) { std::cout << "  [ok] " << name << "\n"; }

void writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    f << data;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int n = 0;
    for (std::size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size())) {
        ++n;
    }
    return n;
}

}  // namespace

int main() {
    std::cout << "== Phase 3 YouTube service: functional tests ==\n";

    // ---- parseTrackJson: full search-result object ----
    {
        const std::string js = R"J({"id":"dQw4w9WgXcQ","title":"Rick Astley - Never Gonna Give You Up (Official Video)","duration":212,"duration_string":"3:32","channel":"Rick Astley","uploader":"RickAstleyVEVO","thumbnail":"https://i.ytimg.com/vi/dQw4w9WgXcQ/maxresdefault.jpg?sqp=abc123","view_count":1600000000,"webpage_url":"https://www.youtube.com/watch?v=dQw4w9WgXcQ"})J";
        auto t = YouTube::parseTrackJson(js, /*video=*/true);
        CHECK(t.has_value());
        CHECK(t->id == "dQw4w9WgXcQ");
        CHECK(t->title == "Rick Astley - Never Gonna");                 // 25 code points
        CHECK(t->duration_sec == 212);
        CHECK(t->duration == "3:32");                                    // duration_string preferred
        CHECK(t->channel_name == "Rick Astley");                         // "channel" preferred over "uploader"
        CHECK(t->thumbnail == "https://i.ytimg.com/vi/dQw4w9WgXcQ/maxresdefault.jpg");  // query stripped
        CHECK(t->view_count == "1600000000");
        CHECK(t->url == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
        CHECK(t->video == true);
        section("parseTrackJson: full object (title trunc, dur, channel, thumb, views)");
    }

    // ---- parseTrackJson: fallbacks (uploader, duration_string->sec, derived thumb) ----
    {
        const std::string js = R"J({"id":"abcdef12345","title":"Short Title","duration_string":"1:05","uploader":"Some Channel"})J";
        auto t = YouTube::parseTrackJson(js, /*video=*/false);
        CHECK(t.has_value());
        CHECK(t->channel_name == "Some Channel");                        // uploader fallback
        CHECK(t->duration_sec == 65);                                    // "1:05" -> 65
        CHECK(t->duration == "1:05");
        CHECK(t->thumbnail == "https://i.ytimg.com/vi/abcdef12345/hqdefault.jpg");  // derived
        CHECK(t->view_count.empty());                                    // absent
        CHECK(t->title == "Short Title");                                // under 25, unchanged
        CHECK(t->video == false);
        section("parseTrackJson: fallbacks (uploader, duration_string, derived thumbnail)");
    }

    // ---- parseTrackJson: numeric duration -> H:MM:SS formatting ----
    {
        const std::string js = R"J({"id":"vid12345678","title":"X","duration":3725})J";
        auto t = YouTube::parseTrackJson(js, false);
        CHECK(t.has_value());
        CHECK(t->duration_sec == 3725);
        CHECK(t->duration == "1:02:05");                                 // 1h 2m 5s
        section("parseTrackJson: numeric duration formatted as H:MM:SS");
    }

    // ---- parseTrackJson: unusable inputs -> nullopt (never throws) ----
    {
        CHECK(!YouTube::parseTrackJson("not even json", false).has_value());
        CHECK(!YouTube::parseTrackJson("[1,2,3]", false).has_value());          // array, not object
        CHECK(!YouTube::parseTrackJson(R"J({"title":"no id here"})J", false).has_value());
        CHECK(!YouTube::parseTrackJson("", false).has_value());
        section("parseTrackJson: garbage / arrays / missing id -> nullopt");
    }

    // ---- URL classification ----
    {
        YouTube yt;
        CHECK(yt.valid("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
        CHECK(yt.valid("https://youtu.be/dQw4w9WgXcQ"));
        CHECK(yt.valid("https://www.youtube.com/playlist?list=PLabcdef123"));
        CHECK(!yt.valid("https://example.com/watch?v=dQw4w9WgXcQ"));       // not youtube
        CHECK(!yt.invalid("https://www.youtube.com/watch?v=dQw4w9WgXcQ")); // a handled link
        CHECK(yt.invalid("https://www.youtube.com/channel/UCabcdef"));     // youtube, unhandled
        section("valid()/invalid(): watch/shorts/playlist vs. unhandled links");
    }

    // ---- runCommand: captures stdout, never throws ----
    {
        std::string out = YouTube::runCommand("echo hello_from_shell");
        CHECK(out.find("hello_from_shell") != std::string::npos);
        section("runCommand: captures subprocess stdout");
    }

    // ---- download(): cache hit returns immediately, no subprocess ----
    {
        ::mkdir(YouTube::kDownloadsDir, 0755);  // ignore EEXIST
        writeFile(std::string(YouTube::kDownloadsDir) + "/CACHEDID001.webm", "audio-bytes");
        writeFile(std::string(YouTube::kDownloadsDir) + "/CACHEDID002.mp4", "video-bytes");

        YouTube yt;
        auto a = yt.download("CACHEDID001", /*video=*/false);
        CHECK(a.has_value());
        CHECK(*a == "downloads/CACHEDID001.webm");

        auto v = yt.download("CACHEDID002", /*video=*/true);
        CHECK(v.has_value());
        CHECK(*v == "downloads/CACHEDID002.mp4");
        section("download(): cache-hit returns existing file (no re-download)");
    }

    // ---- graceful degradation + warn-once (only when yt-dlp is absent) ----
    {
        const bool ytdlp =
            !YouTube::runCommand("command -v yt-dlp 2>/dev/null").empty();
        if (!ytdlp) {
            const char* kLog = "yt_demo_log.txt";
            std::remove(kLog);
            LogSink::instance().init(kLog, 10u * 1024u * 1024u, 5, LogLevel::Info);

            YouTube yt;
            // No cache file + no yt-dlp -> nullopt, and pickCookie warns once.
            CHECK(!yt.download("nonexistentA", false).has_value());
            CHECK(!yt.download("nonexistentB", false).has_value());
            // search / playlist degrade to empty results.
            CHECK(!yt.search("some query that goes nowhere").has_value());
            CHECK(yt.playlist("https://www.youtube.com/playlist?list=PLnope", 5).empty());

            LogSink::instance().close();
            const std::string log = readFile(kLog);
            CHECK(countOccurrences(log, "Cookies are missing") == 1);   // warned exactly once
            std::remove(kLog);
            section("graceful: no yt-dlp -> empty/nullopt, cookie warning emitted once");
        } else {
            std::cout << "  [skip] live yt-dlp detected; skipping network calls\n";
        }
    }

    // ---- cleanup ----
    std::remove((std::string(YouTube::kDownloadsDir) + "/CACHEDID001.webm").c_str());
    std::remove((std::string(YouTube::kDownloadsDir) + "/CACHEDID002.mp4").c_str());

    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}
