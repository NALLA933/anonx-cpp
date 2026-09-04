#include "anonx/app.hpp"
#include "anonx/cache_manager.hpp"
#include "anonx/config.hpp"
#include "anonx/database.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace anonx;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr << "CHECK FAILED: " #cond " (" << __FILE__ << ":" \
                      << __LINE__ << ")\n";                             \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {
const char* kEnvPath = "anonx_demo.env";
const char* kDbPath  = "anonx_demo_app.db";

void section(const char* name) { std::cout << "  [ok] " << name << "\n"; }

void writeDemoEnv() {

    std::ofstream f(kEnvPath, std::ios::binary);
    f << "# demo env\n";
    f << "API_ID=12345\n";
    f << "API_HASH=\"abcdef\"\n";
    f << "BOT_TOKEN=111:xyz\r\n";
    f << "LOGGER_ID=-100999\n";
    f << "OWNER_ID=42\n";
    f << "SESSION=AoJ1c2VyPT09\n";
    f << "SESSION2=BbK2\n";
    f << "  export DURATION_LIMIT=30 \n";
    f << "AUTO_LEAVE=true\n";
    f << "LANG_CODE=hi\n";
    f << "COOKIES_URL=https://batbin.me/aaa https://example.com/bbb\n";
    f << "DB_PATH=" << kDbPath << "\n";
}

void cleanup() {
    std::remove(kEnvPath);
    std::remove(kDbPath);
    std::remove((std::string(kDbPath) + "-wal").c_str());
    std::remove((std::string(kDbPath) + "-shm").c_str());
}
}

int main() {
    cleanup();
    writeDemoEnv();

    std::cout << "== Phase 2 config + skeleton: functional tests ==\n";

    {
        Config c = Config::load(kEnvPath);
        CHECK(c.api_id == 12345);
        CHECK(c.api_hash == "abcdef");
        CHECK(c.bot_token == "111:xyz");
        CHECK(c.logger_id == -100999);
        CHECK(c.owner_id == 42);
        CHECK(c.session1 == "AoJ1c2VyPT09");
        CHECK(c.session2 == "BbK2");
        CHECK(c.session3.empty());
        CHECK(c.assistantCount() == 2);
        CHECK(c.duration_limit_seconds == 30 * 60);
        CHECK(c.auto_leave == true);
        CHECK(c.thumb_gen == true);
        CHECK(c.video_play == true);
        CHECK(c.lang_code == "hi");
        CHECK(c.cookies_url.size() == 1);
        CHECK(c.cookies_url[0].find("batbin.me") != std::string::npos);
        CHECK(c.db_path == kDbPath);
        section("config: .env parsing (quotes, CRLF, '=' padding, cookie filter)");
    }

    {
        Config good;
        good.api_id = 1;
        good.api_hash = "h";
        good.bot_token = "t";
        good.logger_id = 2;
        good.owner_id = 3;
        good.session1 = "s";
        good.check();

        bool threw = false;
        Config bad = good;
        bad.bot_token.clear();
        try {
            bad.check();
        } catch (const ConfigError&) {
            threw = true;
        }
        CHECK(threw);

        threw = false;
        Config bad2 = good;
        bad2.api_id = 0;
        try {
            bad2.check();
        } catch (const ConfigError&) {
            threw = true;
        }
        CHECK(threw);
        section("config: check() passes when complete, throws when required missing");
    }

    {
        App app(kEnvPath);
        app.boot();

        CHECK(app.uptimeSeconds() >= 0.0);
        CHECK(app.config().assistantCount() == 2);

        CHECK(app.db().addChat(-100500));
        CHECK(app.db().isChat(-100500));
        int a = app.db().getAssistant(-100500);
        CHECK(a >= 1 && a <= 2);
        CHECK(app.db().getLang(-100501) == "hi");

        app.cache().addCall(-100500);
        CHECK(app.cache().isActiveCall(-100500));

        app.stop();
        app.stop();
        section("app: boot -> db()/cache() usable -> stop() (idempotent)");
    }

    {
        std::ifstream log("log.txt");
        CHECK(log.good());
        std::string content((std::istreambuf_iterator<char>(log)),
                            std::istreambuf_iterator<char>());
        CHECK(content.find("anonx:") != std::string::npos);
        CHECK(content.find("Ready.") != std::string::npos);
        section("logger: log.txt written with expected format");
    }

    cleanup();
    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}
