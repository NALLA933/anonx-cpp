#include "anonx/config.hpp"
#include "anonx/dispatcher.hpp"
#include "anonx/logger.hpp"
#include "anonx/plugins_router.hpp"
#include "anonx/runtime.hpp"
#include "anonx/telegram_client.hpp"
#include "anonx/td_client.hpp"
#include "anonx/userbot.hpp"

#include "fake_bot_api.hpp"
#include "fake_system_info.hpp"
#include "fake_tdjson_hook.h"
#include "fake_voice_transport.hpp"
#include "fake_youtube.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace anonx;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

static void testFiltersAndParsing() {
    Dispatcher d;
    d.setBotUsername("AnonXMusicBot");

    auto c1 = d.parseCommand("/play never gonna");
    CHECK(c1.size() == 3 && c1[0] == "play" && c1[1] == "never" && c1[2] == "gonna",
          "parseCommand basic + args");

    auto c2 = d.parseCommand("/play@AnonXMusicBot song");
    CHECK(c2.size() == 2 && c2[0] == "play" && c2[1] == "song", "parseCommand @thisbot accepted");

    auto c3 = d.parseCommand("/play@SomeOtherBot song");
    CHECK(c3.empty(), "parseCommand @otherbot ignored");

    CHECK(d.parseCommand("hello world").empty(), "parseCommand non-command");
    CHECK(d.parseCommand("").empty(), "parseCommand empty");

    auto c5 = d.parseCommand("/PLAY x");
    CHECK(c5.size() == 2 && c5[0] == "PLAY", "parseCommand preserves name case");

    Dispatcher d2;
    d2.setPrefixes({'/', '!', '.'});
    CHECK(d2.parseCommand("!ping").size() == 1 && d2.parseCommand("!ping")[0] == "ping",
          "custom prefix '!'");
    CHECK(d2.parseCommand(".stats")[0] == "stats", "custom prefix '.'");
    CHECK(d2.parseCommand("/skip")[0] == "skip", "custom prefix keeps '/'");

    MessageContext g;
    g.chatId = -100123;
    g.chatType = ChatType::Group;
    g.fromUserId = 42;
    g.command = {"play", "x"};
    g.text = "/play x";

    MessageContext p;
    p.chatId = 555;
    p.chatType = ChatType::Private;
    p.fromUserId = 7;
    p.command = {"help"};
    p.text = "/help";

    CHECK(filters::command({"play"})(g), "command filter match");
    CHECK(!filters::command({"skip"})(g), "command filter no-match");
    CHECK(filters::command({"PLAY"})(g), "command filter case-insensitive");
    CHECK(filters::groupChat()(g), "groupChat filter");
    CHECK(!filters::groupChat()(p), "groupChat filter negative");
    CHECK(filters::privateChat()(p), "privateChat filter");
    CHECK(!filters::privateChat()(g), "privateChat filter negative");
    CHECK(filters::user({42, 99})(g), "user filter match");
    CHECK(!filters::user({1, 2})(g), "user filter no-match");
    CHECK(filters::textMessage()(g), "text filter");

    auto isBlacklisted = [](std::int64_t id) { return id == 42; };
    CHECK(filters::userWhere(isBlacklisted)(g), "userWhere match");
    CHECK(!filters::userWhere(isBlacklisted)(p), "userWhere no-match");

    auto f = filters::command({"play"}) && filters::groupChat() &&
             !filters::userWhere(isBlacklisted);
    CHECK(!f(g), "combinator excludes blacklisted user");
    MessageContext g2 = g;
    g2.fromUserId = 99;
    CHECK(f(g2), "combinator allows normal user");

    auto orf = filters::command({"skip"}) || filters::command({"play"});
    CHECK(orf(g), "OR filter");

    Filter matchAll;
    CHECK(matchAll(g) && matchAll(p), "default filter matches all");

    std::printf("  [ok] filters + command parsing\n");
}

static void testDispatchRouting() {
    Dispatcher d;
    std::string hit;
    d.onMessage(filters::command({"ping"}), [&](MessageContext&) { hit = "ping"; });
    d.onMessage(filters::command({"play"}), [&](MessageContext&) { hit = "play"; });
    d.onMessage(Filter(), [&](MessageContext&) { hit = "catchall"; });

    MessageContext m;
    m.command = {"play"};
    m.text = "/play";
    CHECK(d.dispatchMessage(m) && hit == "play", "routes to first matching handler");

    hit.clear();
    MessageContext m2;
    m2.text = "just chatting";
    CHECK(d.dispatchMessage(m2) && hit == "catchall", "catch-all handles non-command");

    hit.clear();
    Dispatcher empty;
    MessageContext m3;
    m3.command = {"play"};
    CHECK(!empty.dispatchMessage(m3), "no handlers -> not handled");

    std::printf("  [ok] message routing (first-match wins)\n");
}

static void testCallbackRouting() {
    Dispatcher d;
    std::string result;
    d.onCallback(filters::callbackDataPrefix("vol"),
                 [&](CallbackContext& c) { result = "prefix:" + c.data; });
    d.onCallback(filters::callbackData("play_1"),
                 [&](CallbackContext& c) { result = "exact:" + c.data; });

    d.onUpdate(
        R"({"@type":"updateNewCallbackQuery","id":999,"sender_user_id":7,"chat_id":-100,)"
        R"("message_id":3,"payload":{"@type":"callbackQueryPayloadData","data":"dm9sX3Vw"}})");
    CHECK(result == "prefix:vol_up", "callback prefix + base64 decode");

    d.onUpdate(
        R"({"@type":"updateNewCallbackQuery","id":1000,"sender_user_id":7,"chat_id":-100,)"
        R"("message_id":3,"payload":{"@type":"callbackQueryPayloadData","data":"cGxheV8x"}})");
    CHECK(result == "exact:play_1", "callback exact + base64 decode");

    std::printf("  [ok] callback routing + base64 payloads\n");
}

static void testBotBootAndOps() {
    Dispatcher disp;

    TelegramClient::Options opts;
    opts.apiId = 12345;
    opts.apiHash = "hash";
    opts.databaseDirectory = "tdlib/bot";
    opts.botToken = "123456:ABCDEF";
    opts.name = "anony";

    TelegramClient bot(std::move(opts));
    CHECK(bot.boot(5000), "bot authorizes via bot token");
    CHECK(bot.authorized(), "bot authorized() true");

    const int cid = bot.raw().clientId();
    CHECK(bot.me().id == 100000 + cid, "getMe id parsed");
    CHECK(bot.me().username == std::string("anon_bot_") + std::to_string(cid),
          "getMe username parsed (active_usernames)");
    CHECK(bot.me().mention.find("tg://user?id=") != std::string::npos, "mention is an HTML link");

    const std::string status = bot.getChatMemberStatus(-1001122334455LL, bot.me().id);
    CHECK(status == "chatMemberStatusAdministrator", "getChatMemberStatus parsed");

    const std::int64_t mid = bot.sendMessage(-1001122334455LL, "<b>Bot Started</b>");
    CHECK(mid != 0, "sendMessage returns a message id");

    disp.attach(bot);

    std::mutex mtx;
    std::condition_variable cv;
    bool pinged = false;
    std::string gotCmd;
    std::int64_t gotUser = 0;
    ChatType gotType = ChatType::Private;

    disp.onMessage(filters::command({"ping"}) && filters::groupChat(),
                   [&](MessageContext& m) {
                       std::lock_guard<std::mutex> lk(mtx);
                       pinged = true;
                       gotCmd = m.command.empty() ? "" : m.command[0];
                       gotUser = m.fromUserId;
                       gotType = m.chatType;
                       cv.notify_all();
                   });

    fake_td_inject(
        cid,
        R"({"@type":"updateNewMessage","message":{"@type":"message","id":42,)"
        R"("chat_id":-100999,"sender_id":{"@type":"messageSenderUser","user_id":777},)"
        R"("content":{"@type":"messageText","text":{"@type":"formattedText","text":"/ping"}}}})");

    {
        std::unique_lock<std::mutex> lk(mtx);
        CHECK(cv.wait_for(lk, std::chrono::seconds(3), [&] { return pinged; }),
              "injected /ping reached the handler");
    }
    CHECK(gotCmd == "ping", "dispatched command name");
    CHECK(gotUser == 777, "dispatched sender id");
    CHECK(gotType == ChatType::Group, "dispatched chat classified as group");

    std::mutex cmtx;
    std::condition_variable ccv;
    bool cbHit = false;
    std::string cbData;
    disp.onCallback(filters::callbackDataPrefix("vol"), [&](CallbackContext& c) {
        std::lock_guard<std::mutex> lk(cmtx);
        cbHit = true;
        cbData = c.data;
        ccv.notify_all();
        c.answer("done");
    });

    fake_td_inject(
        cid,
        R"({"@type":"updateNewCallbackQuery","id":555,"sender_user_id":777,)"
        R"("chat_id":-100999,"message_id":42,)"
        R"("payload":{"@type":"callbackQueryPayloadData","data":"dm9sX3Vw"}})");

    {
        std::unique_lock<std::mutex> lk(cmtx);
        CHECK(ccv.wait_for(lk, std::chrono::seconds(3), [&] { return cbHit; }),
              "injected callback reached the handler");
    }
    CHECK(cbData == "vol_up", "callback payload decoded end-to-end");

    bot.exit();
    std::printf("  [ok] bot boot, getMe, admin check, sendMessage, live dispatch\n");
}

static void testUserbotManager() {
    Userbot ub(12345, "hash");
    ub.setInteractiveLogin(false);

    Userbot::AssistantSpec a1;
    a1.name = "AnonyUB1";
    a1.phoneNumber = "+10000000001";
    a1.sessionDirectory = "tdlib/assistant1";
    a1.codeProvider = [] { return std::string("11111"); };
    ub.addAssistant(a1);

    Userbot::AssistantSpec a2;
    a2.name = "AnonyUB2";
    a2.phoneNumber = "+10000000002";
    a2.sessionDirectory = "tdlib/assistant2";
    a2.codeProvider = [] { return std::string("2fa"); };
    a2.passwordProvider = [] { return std::string("secret"); };
    ub.addAssistant(a2);

    ub.setLoggerChatId(-100999);
    ub.setSupportChat("fallenx");

    CHECK(ub.count() == 2, "assistant count");
    CHECK(ub.bootAll(5000), "all assistants boot (incl. 2FA path)");
    CHECK(ub.at(0) && ub.at(0)->authorized(), "assistant 1 authorized");
    CHECK(ub.at(1) && ub.at(1)->authorized(), "assistant 2 authorized via 2FA");
    CHECK(ub.at(2) == nullptr, "out-of-range accessor is null");

    ub.exitAll();
    std::printf("  [ok] userbot manager (bot-token + phone + 2FA logins)\n");
}

namespace router {

constexpr std::int64_t kChat    = -1001234567890LL;
constexpr std::int64_t kLogger  = -1009999999999LL;
constexpr std::int64_t kBoss    = 7;
constexpr std::int64_t kBanned  = 8;
constexpr std::int64_t kBadChat = -1005555555555LL;

int g_routed = 0;

std::string freshDbPath() {
    static int counter = 0;
    const std::string path = "anonx_router_" + std::to_string(++counter) + ".db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    return path;
}

void loadLocales(Language& lang) {
    std::vector<std::string> dirs;
#ifdef ANONX_LOCALES_DIR
    dirs.push_back(ANONX_LOCALES_DIR);
#endif
    dirs.push_back("locales");
    dirs.push_back("../locales");
    dirs.push_back("../../locales");
    for (const std::string& dir : dirs) {
        if (lang.loadDir(dir) > 0 && lang.loaded("en"))
            break;
    }
    lang.setDefault("en");
}

struct Bot {
    std::string        dbPath;
    FakeBotApi         api;
    Database           db;
    CacheManager       cache;
    Queue              queue;
    FakeYouTube        yt;
    FakeVoiceTransport tp;
    CallManager        calls;
    FakeSystemInfo     sys;
    Config             config;
    Plugins            plugins;
    AdminPlugins       admin;
    Dispatcher         disp;

    explicit Bot(const Language& lang)
        : dbPath(freshDbPath()), db(dbPath), calls(tp, queue, cache),
          plugins(Plugins::Deps{api, db, cache, queue, yt, calls, lang, config}),
          admin(AdminPlugins::Deps{api, db, cache, calls, sys, lang, config}) {
        config.owner_id        = kBoss;
        config.logger_id       = kLogger;
        config.support_chat    = "https://t.me/support";
        config.support_channel = "https://t.me/channel";
        db.setDefaultLang("en");
        db.setAssistantCount(1);
        api.makeAdmin(kChat, kBoss);
        api.titles[kChat] = "Router Group";

        cache.addCall(kChat);
        for (int i = 1; i <= 2; ++i) {
            MediaItem track;
            track.id           = "t" + std::to_string(i);
            track.title        = "Track " + std::to_string(i);
            track.duration     = "3:00";
            track.duration_sec = 180;
            track.url          = "https://youtu.be/t" + std::to_string(i);
            track.file_path    = "/tmp/t" + std::to_string(i) + ".webm";
            track.user         = "@u7";
            queue.add(kChat, track);
        }
    }

    ~Bot() {
        std::remove(dbPath.c_str());
        std::remove((dbPath + "-wal").c_str());
        std::remove((dbPath + "-shm").c_str());
    }

    Bot(const Bot&)            = delete;
    Bot& operator=(const Bot&) = delete;
};

std::string trace(const FakeBotApi& api) {
    std::string out;
    for (const FakeBotApi::Record& r : api.log) {
        out += r.op + " chat=" + std::to_string(r.chatId) + " id=" +
               std::to_string(r.messageId) + (r.alert ? " alert" : "") + " | " + r.text +
               "\n";
        for (const auto& row : r.kb) {
            out += "      row:";
            for (const InlineButton& b : row)
                out += " [" + b.text + "=" + b.data + b.url + b.copy + "]";
            out += "\n";
        }
    }
    return out;
}

std::string noDigits(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c < '0' || c > '9')
            out += c;
    }
    return out;
}

MessageContext msg(const std::vector<std::string>& tokens, bool priv,
                   std::int64_t user = kBoss, std::int64_t chat = 0) {
    MessageContext m;
    m.chatId     = chat != 0 ? chat : (priv ? user : kChat);
    m.chatType   = priv ? ChatType::Private : ChatType::Group;
    m.messageId  = 500;
    m.fromUserId = user;
    m.text       = tokens.empty() ? "hello there" : "/" + tokens[0];
    m.command    = tokens;
    return m;
}

CallbackContext press(const std::string& payload) {
    CallbackContext c;
    c.chatId     = kChat;
    c.messageId  = 700;
    c.fromUserId = kBoss;
    c.queryId    = 991;
    c.data       = payload;
    return c;
}

using Direct       = std::function<void(Bot&, const CommandEvent&)>;
using DirectButton = std::function<void(Bot&, const ButtonEvent&)>;

void compare(const std::string& label, const std::string& routed,
             const std::string& expected, bool ignoreDigits) {
    std::string a = routed;
    std::string b = expected;
    CHECK(!a.empty(), (label + ": the handler left a trace at all").c_str());
    if (ignoreDigits) {
        a = noDigits(a);
        b = noDigits(b);
    }
    if (a != b) {
        std::fprintf(stderr, "--- %s routed ---\n%s--- expected ---\n%s", label.c_str(),
                     a.c_str(), b.c_str());
    }
    CHECK(a == b, (label + ": reached its own handler").c_str());
    ++g_routed;
}

void expectRoutes(const Language& lang, const std::string& label,
                  const std::vector<std::string>& tokens, bool priv,
                  const Direct& direct, bool ignoreDigits = false) {
    Bot routed(lang);
    installPlugins(routed.disp, routed.plugins, routed.admin, routed.db);
    MessageContext ctx = msg(tokens, priv);
    CHECK(routed.disp.dispatchMessage(ctx), (label + ": is routed").c_str());

    Bot mirror(lang);
    mirror.plugins.attachCallbacks();
    const CommandEvent ev = toCommandEvent(ctx);
    mirror.admin.onSeen(ev);
    direct(mirror, ev);

    compare(label, trace(routed.api), trace(mirror.api), ignoreDigits);
}

void expectIgnored(const Language& lang, const std::string& label,
                   const std::vector<std::string>& tokens, bool priv) {
    Bot bot(lang);
    installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
    MessageContext ctx = msg(tokens, priv);
    CHECK(!bot.disp.dispatchMessage(ctx), (label + ": no handler claims it").c_str());
}

void expectCallbackRoutes(const Language& lang, const std::string& payload,
                          const DirectButton& direct) {
    Bot routed(lang);
    installPlugins(routed.disp, routed.plugins, routed.admin, routed.db);
    CallbackContext ctx = press(payload);
    CHECK(routed.disp.dispatchCallback(ctx), ("\"" + payload + "\": is routed").c_str());

    Bot mirror(lang);
    mirror.plugins.attachCallbacks();
    direct(mirror, toButtonEvent(ctx));

    compare("\"" + payload + "\"", trace(routed.api), trace(mirror.api), false);
}

void testAdapters() {
    MessageContext raw = msg({"SeekBack", "30"}, false);
    raw.replyToMessageId = 404;
    const CommandEvent ev = toCommandEvent(raw);
    CHECK(ev.command.size() == 2 && ev.command[0] == "seekback",
          "toCommandEvent lowercases the command name");
    CHECK(ev.command[1] == "30", "toCommandEvent keeps the arguments verbatim");
    CHECK(ev.chatId == kChat && ev.messageId == 500 && ev.fromUserId == kBoss,
          "toCommandEvent copies chat, message and sender");
    CHECK(!ev.isPrivate, "a group message is not private");
    CHECK(ev.replyToMessageId == 404, "toCommandEvent carries the reply-to id");
    CHECK(toCommandEvent(msg({"start"}, true)).isPrivate, "a private chat sets isPrivate");
    CHECK(toCommandEvent(msg({}, false)).command.empty(), "a plain message has no command");

    const ButtonEvent be = toButtonEvent(press("help 3"));
    CHECK(be.data == "help 3", "toButtonEvent keeps the payload as sent (no lowercasing)");
    CHECK(be.chatId == kChat && be.messageId == 700 && be.fromUserId == kBoss &&
              be.queryId == 991,
          "toButtonEvent copies chat, message, sender and query id");

    std::printf("  [ok] router adapters (MessageContext/CallbackContext -> events)\n");
}

void testCommandRouting(const Language& lang) {

    struct PlaybackGroup {
        std::vector<std::string> names;
        std::vector<std::string> args;
        Direct                   handler;
    };
    const std::vector<PlaybackGroup> playback = {
        {Plugins::playCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onPlay(e); }},
        {Plugins::skipCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onSkip(e); }},
        {Plugins::pauseCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onPause(e); }},
        {Plugins::resumeCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onResume(e); }},
        {Plugins::stopCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onStop(e); }},
        {Plugins::loopCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onLoop(e); }},
        {Plugins::queueCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onQueue(e); }},
        {Plugins::seekCommands(), {}, [](Bot& b, const CommandEvent& e) { b.plugins.onSeek(e); }},
    };
    for (const PlaybackGroup& g : playback) {
        for (const std::string& name : g.names) {
            std::vector<std::string> tokens{name};
            tokens.insert(tokens.end(), g.args.begin(), g.args.end());
            expectRoutes(lang, "/" + name, tokens, false, g.handler);
            expectIgnored(lang, "/" + name + " in private", tokens, true);
        }
    }
    std::printf("  [ok] playback commands: every alias, all group-only\n");

    struct AdminGroup {
        std::vector<std::string> names;
        std::vector<std::string> args;
        bool                     groupOnly;
        bool                     ignoreDigits;
        Direct                   handler;
    };
    const std::vector<AdminGroup> groups = {
        {AdminPlugins::authCommands(), {"12345"}, true, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onAuth(e); }},
        {AdminPlugins::authListCommands(), {}, true, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onAuthList(e); }},
        {AdminPlugins::settingsCommands(), {}, true, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onSettings(e); }},
        {AdminPlugins::blacklistCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onBlacklist(e); }},
        {AdminPlugins::gcastCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onGcast(e); }},
        {AdminPlugins::sudoCommands(), {"12345"}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onSudo(e); }},
        {AdminPlugins::sudoListCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onSudoList(e); }},
        {AdminPlugins::langCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onLang(e); }},
        {AdminPlugins::pingCommands(), {}, false, true,
         [](Bot& b, const CommandEvent& e) { b.admin.onPing(e); }},
        {AdminPlugins::statsCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onStats(e); }},
        {AdminPlugins::activeVcCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onActiveVc(e); }},
        {AdminPlugins::startCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onStart(e); }},
        {AdminPlugins::helpCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onHelp(e); }},
        {AdminPlugins::loggerCommands(), {}, false, false,
         [](Bot& b, const CommandEvent& e) { b.admin.onLogger(e); }},
    };
    CHECK(groups.size() == AdminPlugins::allCommandGroups().size(),
          "every admin command group is covered here");

    for (const AdminGroup& g : groups) {
        for (const std::string& name : g.names) {
            std::vector<std::string> tokens{name};
            tokens.insert(tokens.end(), g.args.begin(), g.args.end());
            expectRoutes(lang, "/" + name, tokens, false, g.handler, g.ignoreDigits);
        }

        std::vector<std::string> first{g.names.front()};
        first.insert(first.end(), g.args.begin(), g.args.end());
        if (g.groupOnly)
            expectIgnored(lang, "/" + g.names.front() + " in private", first, true);
        else
            expectRoutes(lang, "/" + g.names.front() + " in private", first, true,
                         g.handler, g.ignoreDigits);
    }
    std::printf("  [ok] admin commands: %d groups, group-only vs private honoured\n",
                static_cast<int>(groups.size()));

    expectRoutes(lang, "/PING", {"PING"}, false,
                 [](Bot& b, const CommandEvent& e) { b.admin.onPing(e); }, true);
    expectRoutes(lang, "/Stats", {"Stats"}, false,
                 [](Bot& b, const CommandEvent& e) { b.admin.onStats(e); });

    expectIgnored(lang, "an unknown command", {"definitelynotacommand"}, false);
    expectIgnored(lang, "a plain message", {}, false);
    std::printf("  [ok] case-insensitive commands, unknown commands ignored\n");
}

void testBlacklistFilters(const Language& lang) {
    {
        Bot bot(lang);
        installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
        bot.db.addBlacklist(kBanned);

        MessageContext banned = msg({"ping"}, false, kBanned);
        CHECK(!bot.disp.dispatchMessage(banned), "a blacklisted user gets no handler");
        CHECK(bot.api.log.empty(), "and the watcher stays silent for them too");

        MessageContext fine = msg({"ping"}, false);
        CHECK(bot.disp.dispatchMessage(fine), "another user in the same chat is answered");

        bot.db.addBlacklist(kBoss);
        MessageContext now = msg({"ping"}, false);
        CHECK(!bot.disp.dispatchMessage(now), "blacklisting applies to the next message");
    }
    {
        Bot bot(lang);
        installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
        bot.db.addBlacklist(kBadChat);

        MessageContext ctx = msg({"ping"}, false, kBoss, kBadChat);
        CHECK(!bot.disp.dispatchMessage(ctx), "a blacklisted chat gets no handler");
        CHECK(bot.api.log.empty(), "and no notice is sent for it");
        CHECK(!bot.db.isChat(kBadChat), "nor is it counted as a served chat");
    }
    std::printf("  [ok] blacklisted users and chats filtered, re-read per message\n");
}

void testWatcherAndMenus(const Language& lang) {

    {
        Bot bot(lang);
        installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
        MessageContext chatter = msg({}, false);
        CHECK(!bot.disp.dispatchMessage(chatter), "plain text matches no command");
        CHECK(bot.db.isChat(kChat), "but the watcher registered the chat");
        const FakeBotApi::Record* note = bot.api.last("send");
        CHECK(note != nullptr && note->chatId == kLogger, "and told the log group");
    }

    {
        Bot bot(lang);
        installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
        MessageContext ctx = msg({"help"}, false);
        CHECK(bot.disp.dispatchMessage(ctx), "/help is handled");
        CHECK(bot.db.isChat(kChat), "the watcher ran for the same message");
        bool toLog = false, toChat = false;
        for (const FakeBotApi::Record& r : bot.api.log) {
            if (r.chatId == kLogger)
                toLog = true;
            if (r.chatId == kChat)
                toChat = true;
        }
        CHECK(toLog && toChat, "both the log notice and the help card were sent");
    }
    std::printf("  [ok] chat watcher: every message, alongside the command handlers\n");

    expectCallbackRoutes(lang, "controls " + std::to_string(kChat) + " pause",
                         [](Bot& b, const ButtonEvent& e) { b.plugins.onControls(e); });
    expectCallbackRoutes(lang, "controls nonsense",
                         [](Bot& b, const ButtonEvent& e) { b.plugins.onControls(e); });

    for (const char* payload : {"help menu", "help 3", "lang menu", "settings menu",
                                "settings toggle cmd_delete", "settings toggle play_mode",
                                "start menu", "close"}) {
        expectCallbackRoutes(lang, payload,
                             [](Bot& b, const ButtonEvent& e) { b.admin.onMenu(e); });
    }
    {
        Bot bot(lang);
        installPlugins(bot.disp, bot.plugins, bot.admin, bot.db);
        CallbackContext ctx = press("not a payload we know");
        CHECK(!bot.disp.dispatchCallback(ctx), "an unknown payload matches no callback");
    }
    std::printf("  [ok] callbacks: the controls prefix and all five menu prefixes\n");
}

void run() {
    Language lang;
    loadLocales(lang);

    testAdapters();
    testCommandRouting(lang);
    testBlacklistFilters(lang);
    testWatcherAndMenus(lang);

    CHECK(g_routed >= 40, "the router comparisons really ran");
    std::printf("  [ok] plugin router: %d command/payload traces matched\n", g_routed);
}

}

namespace integration {

using nlohmann::json;

constexpr std::int64_t kChat   = -1001777888999LL;
constexpr std::int64_t kLogger = -1009999999999LL;
constexpr std::int64_t kOwner  = 7;
constexpr std::int64_t kCardId = 4242;
constexpr std::int64_t kQueryId = 8181;

const char* kDbPath = "anonx_integration.db";

void removeDb() {
    std::remove(kDbPath);
    std::remove((std::string(kDbPath) + "-wal").c_str());
    std::remove((std::string(kDbPath) + "-shm").c_str());
}

Config makeConfig() {
    Config c;
    c.api_id          = 12345;
    c.api_hash        = "0123456789abcdef0123456789abcdef";
    c.bot_token       = "123456:INTEGRATION-TOKEN";
    c.logger_id       = kLogger;
    c.owner_id        = kOwner;
    c.db_path         = kDbPath;
    c.lang_code       = "en";
    c.support_chat    = "https://t.me/support_chat";
    c.support_channel = "https://t.me/support_channel";
    return c;
}

std::string localesDir() {
    std::vector<std::string> dirs;
#ifdef ANONX_LOCALES_DIR
    dirs.push_back(ANONX_LOCALES_DIR);
#endif
    dirs.push_back("locales");
    dirs.push_back("../locales");
    dirs.push_back("../../locales");
    for (const std::string& dir : dirs) {
        Language probe;
        if (probe.loadDir(dir) > 0 && probe.loaded("en"))
            return dir;
    }
    return "locales";
}

Runtime::Options makeOptions() {
    Runtime::Options o;
    o.localesDir                 = localesDir();
    o.botSessionDir              = "tdlib/integration";
    o.interactiveAssistantLogin  = false;
    o.bootAssistants             = false;
    return o;
}

json parse(const std::string& requestJson) {
    json j = json::parse(requestJson, nullptr, false);
    return (j.is_discarded() || !j.is_object()) ? json::object() : j;
}

std::string sentText(const std::string& requestJson) {
    const json j = parse(requestJson);
    if (!j.contains("input_message_content")) return std::string();
    const json& content = j["input_message_content"];
    if (!content.is_object() || !content.contains("text") ||
        !content["text"].is_object()) {
        return std::string();
    }
    const json& text = content["text"];
    return (text.contains("text") && text["text"].is_string())
               ? text["text"].get<std::string>()
               : std::string();
}

std::int64_t int64Of(const std::string& requestJson, const char* key) {
    const json j = parse(requestJson);
    return (j.contains(key) && j[key].is_number()) ? j[key].get<std::int64_t>() : 0;
}

bool has(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool waitFor(const std::function<bool()>& pred) {
    for (int i = 0; i < 500; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

std::string base64(const std::string& in) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out += kTable[(v >> 18) & 0x3F];
        out += kTable[(v >> 12) & 0x3F];
        out += kTable[(v >> 6) & 0x3F];
        out += kTable[v & 0x3F];
        i += 3;
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += kTable[(v >> 18) & 0x3F];
        out += kTable[(v >> 12) & 0x3F];
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kTable[(v >> 18) & 0x3F];
        out += kTable[(v >> 12) & 0x3F];
        out += kTable[(v >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string messageUpdate(std::int64_t chatId, std::int64_t userId,
                          std::int64_t messageId, const std::string& text) {
    json formatted;
    formatted["@type"]    = "formattedText";
    formatted["text"]     = text;
    formatted["entities"] = json::array();
    json content;
    content["@type"] = "messageText";
    content["text"]  = formatted;
    json sender;
    sender["@type"]   = "messageSenderUser";
    sender["user_id"] = userId;
    json message;
    message["@type"]     = "message";
    message["id"]        = messageId;
    message["chat_id"]   = chatId;
    message["sender_id"] = sender;
    message["content"]   = content;
    json update;
    update["@type"]   = "updateNewMessage";
    update["message"] = message;
    return update.dump();
}

std::string callbackUpdate(std::int64_t chatId, std::int64_t userId,
                           std::int64_t messageId, std::int64_t queryId,
                           const std::string& payload) {
    json data;
    data["@type"] = "callbackQueryPayloadData";
    data["data"]  = base64(payload);
    json update;
    update["@type"]          = "updateNewCallbackQuery";
    update["id"]             = queryId;
    update["sender_user_id"] = userId;
    update["chat_id"]        = chatId;
    update["message_id"]     = messageId;
    update["payload"]        = data;
    return update.dump();
}

std::string requestTo(const char* type, std::int64_t chatId) {
    for (std::size_t i = 0;; ++i) {
        const std::string r = fake_td_request_of_type(type, i);
        if (r.empty()) return std::string();
        if (int64Of(r, "chat_id") == chatId) return r;
    }
}

void checkBoot(const Config& config, Runtime& rt) {
    CHECK(rt.bot().authorized(), "the bot account authorized through the real client");
    CHECK(rt.lang().loaded("en"), "start() loaded the locale tables");
    CHECK(rt.assistantsUp() == 0, "no assistant was booted (bootAssistants = false)");
    CHECK(rt.db().chatCount() == 0, "the database starts empty");

    const std::string card = fake_td_request_of_type("sendMessage");
    CHECK(!card.empty(), "start() announced itself");
    CHECK(int64Of(card, "chat_id") == kLogger, "the startup card goes to LOGGER_ID");

    const std::string text = sentText(card);
    CHECK(has(text, "<b>Bot Started</b>"), "... and says the bot started");

    CHECK(has(text, "<b>Assistants:</b> 0/" +
                        std::to_string(config.assistantCount())),
          "... with the assistant count");
    CHECK(has(text, "<b>Languages:</b> "), "... and the locale count");
    CHECK(!has(text, config.bot_token) && !has(text, config.api_hash),
          "... and leaks no token or api hash");

    std::printf("  [ok] Runtime::start(): bot authorized, locales loaded, startup card\n");
}

void checkCommand(const Config& config, Runtime& rt, int clientId) {
    fake_td_clear_requests();
    fake_td_inject(clientId, messageUpdate(kChat, kOwner, 900, "/ping").c_str());
    CHECK(waitFor([] { return fake_td_count_of_type("editMessageText") >= 1; }),
          "/ping crossed the pump, reached AdminPlugins::onPing and was answered");

    const std::string pinging = requestTo("sendMessage", kChat);
    CHECK(!pinging.empty(), "the status message was sent to the group");
    CHECK(!sentText(pinging).empty(), "... and carries the 'pinging' notice");

    const std::string pong = fake_td_last_request_of_type("editMessageText");
    CHECK(int64Of(pong, "chat_id") == kChat, "the card is edited in the same chat");
    CHECK(int64Of(pong, "message_id") != 0, "... over a real message id");
    CHECK(!sentText(pong).empty() && sentText(pong) != sentText(pinging),
          "... replacing the notice with the ping card");
    CHECK(has(pong, "replyMarkupInlineKeyboard") &&
              has(pong, "inlineKeyboardButtonTypeUrl") && has(pong, config.support_chat),
          "... and the support button is a URL button on an inline keyboard");

    CHECK(rt.db().isChat(kChat), "the watcher registered the group as served");
    CHECK(!requestTo("sendMessage", kLogger).empty(), "and told the log group about it");

    std::printf("  [ok] /ping: injected update -> real handler -> TDLib requests\n");
}

void checkButton(Runtime& rt, FakeVoiceTransport& transport, int clientId) {

    rt.cache().addCall(kChat);
    fake_td_put_message(kChat, kCardId, 100000 + clientId, "Now Playing: Track 1");

    fake_td_clear_requests();
    fake_td_inject(clientId,
                   callbackUpdate(kChat, kOwner, kCardId, kQueryId,
                                  "controls pause " + std::to_string(kChat))
                       .c_str());
    CHECK(waitFor([] { return fake_td_count_of_type("answerCallbackQuery") >= 1; }),
          "the press reached Plugins::onControls and the query was answered");

    CHECK(fake_td_count_of_type("getMessage") >= 1,
          "onControls read the card text back before editing it");

    const std::string edit = fake_td_last_request_of_type("editMessageText");
    CHECK(!edit.empty(), "the card was edited");
    CHECK(int64Of(edit, "chat_id") == kChat && int64Of(edit, "message_id") == kCardId,
          "... the exact card the button sits on");
    CHECK(sentText(edit) == "Now Playing: Track 1",
          "... keeping its text, round-tripped through getMessage");
    CHECK(has(edit, "inlineKeyboardButtonTypeCallback"),
          "... under a fresh keyboard of callback buttons");
    CHECK(has(edit, base64("controls resume " + std::to_string(kChat))),
          "... whose payloads are base64-encoded and now offer resume");

    const std::string answer = fake_td_last_request_of_type("answerCallbackQuery");
    CHECK(int64Of(answer, "callback_query_id") == kQueryId, "the right query was answered");

    CHECK(transport.state(kChat).pauseCount == 1,
          "the press reached the injected voice transport");
    CHECK(!rt.cache().isPlaying(kChat), "and the chat is marked paused");

    std::printf("  [ok] controls button: base64 payload -> engine -> edited card\n");
}

void checkStop(Runtime& rt) {
    fake_td_clear_requests();
    rt.stop();

    const std::string bye = fake_td_last_request_of_type("sendMessage");
    CHECK(!bye.empty() && int64Of(bye, "chat_id") == kLogger,
          "the shutdown notice goes to the log group");
    CHECK(has(sentText(bye), "Bot Stopped"), "... and says the bot stopped");
    CHECK(fake_td_count_of_type("close") >= 1, "the bot account was closed on the way out");

    const std::size_t after = fake_td_request_count();
    rt.stop();
    CHECK(fake_td_request_count() == after, "stop() is idempotent");

    rt.bot().exit();
    CHECK(fake_td_request_count() == after, "a closed client does not ask to close again");

    std::printf("  [ok] Runtime::stop(): notice, close, pump joined, idempotent\n");
}

void checkStopAfterSessionClosed(const Config& config, FakeVoiceTransport& transport) {
    fake_td_reset();
    Runtime rt(config, transport, makeOptions());
    CHECK(rt.start(), "a second Runtime boots (the pump restarts after stopRuntime())");

    rt.bot().exit();
    CHECK(!rt.bot().authorized(), "the bot account is no longer authorized");

    fake_td_clear_requests();
    rt.stop();
    CHECK(fake_td_count_of_type("sendMessage") == 0,
          "no shutdown notice is sent over a closed session");
    CHECK(fake_td_count_of_type("close") == 0, "and the account is not closed twice");
    CHECK(fake_td_request_count() == 0, "stopping a closed bot is silent");

    std::printf("  [ok] Runtime::stop() after the session closed: nothing on the wire\n");
}

void run() {
    removeDb();
    const Config config = makeConfig();
    FakeVoiceTransport transport;

    {
        fake_td_reset();
        Runtime rt(config, transport, makeOptions());
        CHECK(rt.start(), "Runtime::start() brings the whole bot up on the fake TDLib");

        const int clientId = rt.bot().raw().clientId();
        checkBoot(config, rt);
        checkCommand(config, rt, clientId);
        checkButton(rt, transport, clientId);
        checkStop(rt);
    }
    checkStopAfterSessionClosed(config, transport);
    removeDb();
}

}

int main() {
    LogSink::instance().init("telegram_demo_log.txt", 10u * 1024u * 1024u, 5, LogLevel::Warning);

    std::printf("Phase 4 — Telegram layer tests\n");
    testFiltersAndParsing();
    testDispatchRouting();
    testCallbackRouting();
    testBotBootAndOps();
    testUserbotManager();
    router::run();
    integration::run();

    TdClient::stopRuntime();
    std::printf("ALL TELEGRAM TESTS PASSED\n");
    return 0;
}
