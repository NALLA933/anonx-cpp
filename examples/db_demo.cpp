// AnonXMusic C++ port — Phase 1 data layer
// db_demo.cpp — exercises every Database + CacheManager method and asserts the
// expected behaviour, including persistence across a reopen and a small
// multi-threaded smoke test. Build target: `anonx_db_demo`.

#include "anonx/cache_manager.hpp"
#include "anonx/database.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

using namespace anonx;

// Always-on check (unlike assert, unaffected by -DNDEBUG in Release builds).
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cerr << "CHECK FAILED: " #cond " (" << __FILE__ << ":" \
                      << __LINE__ << ")\n";                             \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {
const char* kDbPath = "anonx_demo.db";

void section(const char* name) { std::cout << "  [ok] " << name << "\n"; }
}  // namespace

int main() {
    // Start from a clean file so the run is deterministic.
    std::remove(kDbPath);
    std::remove("anonx_demo.db-wal");
    std::remove("anonx_demo.db-shm");

    std::cout << "== Phase 1 data layer: functional tests ==\n";

    // ---- pass 1: populate + verify ----
    {
        Database db(kDbPath);
        db.setDefaultLang("en");
        db.setAssistantCount(3);

        // chats
        CHECK(!db.isChat(-100123));
        CHECK(db.addChat(-100123));
        CHECK(db.isChat(-100123));
        CHECK(db.addChat(-100123));  // idempotent
        CHECK(db.chatCount() == 1);
        section("chats: add / is / idempotent");

        // users
        CHECK(db.addUser(555));
        CHECK(db.isUser(555));
        CHECK(!db.isUser(999));
        section("users: add / is");

        // auth
        CHECK(!db.isAuth(-100123, 777));
        CHECK(db.addAuth(-100123, 777));
        CHECK(db.addAuth(-100123, 778));
        CHECK(db.isAuth(-100123, 777));
        CHECK(db.getAuthUsers(-100123).size() == 2);
        CHECK(db.removeAuth(-100123, 777));
        CHECK(!db.isAuth(-100123, 777));
        CHECK(db.getAuthUsers(-100123).size() == 1);
        section("auth: add / is / remove / list");

        // language
        CHECK(db.getLang(-100123) == "en");  // default
        CHECK(db.setLang(-100123, "hi"));
        CHECK(db.getLang(-100123) == "hi");
        section("lang: default / set / get");

        // assistant
        int a = db.getAssistant(-100123);
        CHECK(a >= 1 && a <= 3);
        CHECK(db.getAssistant(-100123) == a);  // stable across calls
        int b = db.setAssistant(-100123);       // forced reassign
        CHECK(b >= 1 && b <= 3);
        section("assistant: assign / stable / reassign");

        // blacklist (routing by sign)
        CHECK(db.addBlacklist(-100999));  // chat (negative)
        CHECK(db.isBlacklistedChat(-100999));
        CHECK(!db.isBlacklistedUser(-100999));
        CHECK(db.addBlacklist(888));      // user (positive)
        CHECK(db.isBlacklistedUser(888));
        CHECK(db.removeBlacklist(-100999));
        CHECK(!db.isBlacklistedChat(-100999));
        section("blacklist: chat/user routing / add / remove");

        // sudoers
        CHECK(db.addSudo(1));
        CHECK(db.addSudo(2));
        CHECK(db.isSudo(1));
        CHECK(!db.isSudo(3));
        CHECK(db.getSudoers().size() == 2);
        CHECK(db.removeSudo(2));
        CHECK(!db.isSudo(2));
        section("sudoers: add / is / list / remove");

        // per-chat flags
        CHECK(!db.getCmdDelete(-100123));
        CHECK(db.setCmdDelete(-100123, true));
        CHECK(db.getCmdDelete(-100123));
        CHECK(!db.getPlayMode(-100123));
        CHECK(db.setPlayMode(-100123, true));
        CHECK(db.getPlayMode(-100123));
        section("flags: cmd_delete / admin_play set + get");

        // global settings (key/value) + the logger switch built on top of it
        CHECK(db.getSetting("nope").empty());               // unknown key
        CHECK(db.getSetting("other", "dflt") == "dflt");    // unknown + fallback
        // A miss is cached too (negative caching), so a hot key like "logger"
        // costs one SELECT and never touches SQLite again. The price is that the
        // FIRST fallback wins for a given key — fine here, because each key has
        // exactly one caller and therefore one canonical default.
        CHECK(db.getSetting("other", "second") == "dflt");
        CHECK(db.setSetting("greeting", "hello"));
        CHECK(db.getSetting("greeting") == "hello");
        CHECK(db.getSetting("greeting", "dflt") == "hello");   // fallback ignored
        CHECK(db.setSetting("greeting", "bye"));                // overwrite
        CHECK(db.getSetting("greeting") == "bye");
        CHECK(db.setSetting("empty", ""));                       // an empty VALUE
        CHECK(db.getSetting("empty", "dflt").empty());           // is not "missing"
        section("settings: get / set / overwrite / fallback");

        CHECK(db.getLoggerEnabled());                 // logging is on by default
        CHECK(db.setLoggerEnabled(false));
        CHECK(!db.getLoggerEnabled());
        CHECK(db.getSetting("logger") == "0");        // stored as the flag "0"
        CHECK(db.setLoggerEnabled(true));
        CHECK(db.getLoggerEnabled());
        CHECK(db.setLoggerEnabled(false));            // leave it off for pass 2
        section("logger switch: default on, toggles, stored as a setting");
    }

    // ---- pass 2: reopen -> confirm persistence (fresh caches) ----
    {
        Database db(kDbPath);
        db.setDefaultLang("en");
        db.setAssistantCount(3);

        CHECK(db.isChat(-100123));                    // chat persisted
        CHECK(db.isUser(555));                        // user persisted
        CHECK(db.isAuth(-100123, 778));               // auth persisted
        CHECK(!db.isAuth(-100123, 777));              // removal persisted
        CHECK(db.getLang(-100123) == "hi");           // lang persisted
        CHECK(db.isBlacklistedUser(888));             // blacklist persisted
        CHECK(!db.isBlacklistedChat(-100999));        // blacklist removal persisted
        CHECK(db.isSudo(1) && !db.isSudo(2));         // sudo add/remove persisted
        CHECK(db.getCmdDelete(-100123));              // cmd_delete persisted
        CHECK(db.getPlayMode(-100123));               // admin_play PERSISTED (key check)
        CHECK(db.getAssistant(-100123) >= 1);         // assistant persisted
        CHECK(db.getSetting("greeting") == "bye");    // setting persisted
        CHECK(db.getSetting("empty", "dflt").empty()); // an empty value persisted
        CHECK(!db.getLoggerEnabled());                // logger switch persisted OFF
        section("persistence across reopen (incl. admin_play)");
    }

    // ---- pass 3: multi-threaded smoke test (thread-safety) ----
    {
        Database db(kDbPath);
        db.setAssistantCount(3);

        constexpr int kThreads = 8;
        constexpr int kPerThread = 500;
        std::vector<std::thread> pool;
        for (int t = 0; t < kThreads; ++t) {
            pool.emplace_back([&db, t] {
                for (int i = 0; i < kPerThread; ++i) {
                    std::int64_t chat = -1000000 - (t * kPerThread + i);
                    db.addChat(chat);
                    db.addAuth(chat, 1000 + i);
                    db.setLang(chat, (i % 2) ? "hi" : "en");
                    db.getAssistant(chat);
                    (void)db.isChat(chat);
                }
            });
        }
        for (auto& th : pool) th.join();

        // 1 pre-existing chat + all the freshly added unique chats.
        std::size_t expected = 1 + static_cast<std::size_t>(kThreads * kPerThread);
        CHECK(db.chatCount() == expected);
        section("multi-threaded: 8 threads x 500 ops, no corruption");
    }

    // ---- pass 4: CacheManager (RAM-only) ----
    {
        CacheManager cache;
        CHECK(!cache.isActiveCall(-100123));
        cache.addCall(-100123);
        CHECK(cache.isActiveCall(-100123));
        CHECK(cache.isPlaying(-100123));
        CHECK(!cache.setPaused(-100123, true));   // now paused
        CHECK(cache.isActiveCall(-100123));       // still in call
        CHECK(!cache.isPlaying(-100123));         // but not playing
        CHECK(cache.setPaused(-100123, false));   // resumed
        CHECK(cache.isPlaying(-100123));
        CHECK(cache.getLoop(-100123) == 0);
        cache.setLoop(-100123, 5);
        CHECK(cache.getLoop(-100123) == 5);
        cache.setLoop(-100123, 0);                // clears
        CHECK(cache.getLoop(-100123) == 0);
        CHECK(cache.activeCallCount() == 1);
        // activeChats() is what /activevc lists, so its order is part of the
        // contract: ascending chat id, whatever order the calls started in.
        cache.addCall(-100999);
        cache.addCall(-100001);
        CHECK(cache.activeCallCount() == 3);
        std::vector<std::int64_t> active = cache.activeChats();
        CHECK(active == std::vector<std::int64_t>({-100999, -100123, -100001}));
        cache.removeCall(-100999);
        cache.removeCall(-100001);
        CHECK(cache.activeChats() == std::vector<std::int64_t>({-100123}));
        section("CacheManager: activeChats() lists every call, ascending");

        cache.clearChat(-100123);
        CHECK(!cache.isActiveCall(-100123));
        CHECK(cache.activeCallCount() == 0);
        CHECK(cache.activeChats().empty());
        section("CacheManager: active-call + loop lifecycle (RAM-only)");
    }

    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}
