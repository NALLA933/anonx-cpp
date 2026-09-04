#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "anonx/admin_plugins.hpp"
#include "anonx/guards.hpp"
#include "../test/fake_bot_api/fake_bot_api.hpp"
#include "../test/fake_ntgcalls/fake_voice_transport.hpp"
#include "../test/fake_sysinfo/fake_system_info.hpp"

using anonx::AdminPlugins;
using anonx::ButtonEvent;
using anonx::CacheManager;
using anonx::CallManager;
using anonx::CommandEvent;
using anonx::Config;
using anonx::Database;
using anonx::FakeBotApi;
using anonx::FakeSystemInfo;
using anonx::FakeVoiceTransport;
using anonx::InlineKeyboard;
using anonx::Language;
using anonx::LangView;
using anonx::Plugins;
using anonx::Queue;
using anonx::SystemInfo;

namespace buttons = anonx::buttons;
namespace guards  = anonx::guards;

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::cerr << "FAIL: " << (msg) << "  [line " << __LINE__ << "]\n"; \
        }                                                                      \
    } while (0)

namespace {

constexpr std::int64_t kChat   = -1001234567890LL;
constexpr std::int64_t kLogger = -1009999999999LL;
constexpr std::int64_t kAdmin  = 7;
constexpr std::int64_t kUser   = 8;
constexpr std::int64_t kOwner  = 9;
constexpr std::int64_t kSudo   = 10;

const char* kFallbackEn = R"JSON({
  "user_no_perms": "You? Manage the video chat? Not with those permissions, sweetie.",
  "user_not_found": "I'll need a user ID — or would you mind replying to someone's message?",
  "auth_is_admin": "The user is already an <b>admin</b> and can't be added to authorized users.",
  "auth_added": "Added {0} to the authorized users list.",
  "auth_removed": "Removed {0} from the authorized users list.",
  "auth_empty": "Authorized users list is empty.",
  "auth_list": "<u><b>List of authorized users in {0}:</b></u>\n",
  "bl_invalid": "Only chat IDs and user IDs are supported.",
  "bl_usage": "<b>Usage:</b>\n\n/{0} [chat_id|user_id]",
  "bl_already": "This chat is already blacklisted.",
  "bl_added": "This chat has been blacklisted.",
  "bl_removed": "This chat has been removed from the blacklist.",
  "bl_not": "This chat is not blacklisted.",
  "bl_user_notify": "You've been blacklisted from using this bot. Please reach out in the <a href={0}>support chat</a> for more info.",
  "gcast_usage": "Reply to a message for broadcasting the message.",
  "gcast_active": "Please wait for the ongoing broadcast to finish.",
  "gcast_start": "Broadcasting started.",
  "gcast_end": "Broadcasted the message to {0} groups and {1} users.",
  "sudo_already": "{0} is already an sudo user.",
  "sudo_added": "Added {0} to the sudo users list.",
  "sudo_not": "{0} is not an sudo user.",
  "sudo_removed": "Removed {0} from the sudo users list.",
  "sudo_fetching": "Fetching sudo users list...",
  "sudo_owner": "<u><b>Owner:</b></u>\n- {0}\n\n",
  "sudo_users": "<u><b>Sudo users:</b></u>",
  "lang_choose": "Please choose the language you want to set for the current chat:",
  "lang_same": "The language of the current chat is already set to: {0}",
  "lang_change": "Changing the language of the current chat to: {0}",
  "lang_changed": "The language of the current chat has been changed to: <i>{0}</i>",
  "pinging": "Pinging… please wait…",
  "ping_pong": "<u><b>Pong!</b></u>\n\n<b>Latency:</b> <code>{0}ms</code>\n\n<b>Uptime:</b> {1}\n<b>CPU:</b> <code>{2}%</code>\n<b>RAM:</b> <code>{3}%</code>\n<b>Disk:</b> <code>{4}%</code>\n<b>PyTgCalls Latency:</b> <code>{5}ms</code>",
  "stats_fetching": "Fetching stats...",
  "stats_user": "<u><b>{0} stats</b></u>\n\n<b>Assistants:</b> {1}\n<b>Auto leave:</b> {2}\n\n<b>Blocked chats:</b> {3}\n<b>Blocked users:</b> {4}\n<b>Sudo users:</b> {5}\n\n<b>Served chats:</b> {6}\n<b>Served users:</b> {7}",
  "stats_sudo": "\n\n<b>Modules:</b> {0}\n<b>Platform:</b> {1}\n<b>Ram usage:</b> <code>{2}MB | {3}GB</code>\n<b>CPU usage:</b> <code>{4}% ({5} cores)</code>\n<b>Storage:</b> <code>{6}GB | {7}GB</code>\n\n<b>Python:</b> <code>v{8}</code>\n<b>Pyrogram:</b> <code>v{9}</code>\n<b>PyTgCalls:</b> <code>v{10}</code>",
  "vc_empty": "Looks like there aren't any active streams on the bot.",
  "vc_count": "Active streams on the bot: <b>{0}</b>",
  "vc_fetching": "Fetching list of active streams...",
  "vc_list": "<u><b>List of active streams:</b></u>",
  "start_pm": "Hey {0},\nThis is {1} !\n\nA music player bot with some awesome and useful features.\n\n<b><i>Click on the help button for more info.</i></b>",
  "start_gp": "Hey,\nThis is {0}\n\n<u><b>A music player bot with some awesome and useful features.</b></u>",
  "start_settings": "<u><b>{0} settings</b></u>\n\nClick the buttons below to change this chat's current settings.",
  "help_menu": "<b>Click the buttons below to get information about my commands.</b>\n\n<i><b>Note:</b> All commands can be used with /</i>",
  "logger_on": "Logger enabled.",
  "logger_off": "Logger disabled.",
  "logger_usage": "<b>Usage:</b>\n\n/{0} [on|off]",
  "log_user": "<u><b>New User Log</b></u>\n\n<b>ID:</b> <code>{0}</code>\n<b>Name:</b> {1} | {2}",
  "log_chat": "<u><b>New Chat Log</b></u>\n\n<b>Chat:</b> <code>{0}</code> | {1}\n<b>User:</b> <code>{2}</code> | {3}",
  "help": "Help",
  "add_me": "Add me to your group",
  "support": "Support",
  "channel": "Channel",
  "source": "Source",
  "language": "Language",
  "cmd_delete": "Command delete",
  "play_mode": "Admin only play",
  "back": "Back",
  "close": "Close",
  "help_0": "Admins",
  "help_1": "Auth",
  "help_2": "Blacklist",
  "help_3": "Language",
  "help_4": "Ping",
  "help_5": "Play",
  "help_6": "Queue",
  "help_7": "Stats",
  "help_8": "Sudoers",
  "help_admins": "<u><b>Admin commands:</b></u>",
  "help_auth": "<u><b>Auth commands:</b></u>",
  "help_blist": "<u><b>Blacklist commands:</b></u>",
  "help_lang": "<u><b>Language commands:</b></u>",
  "help_ping": "<u><b>Ping commands:</b></u>",
  "help_play": "<u><b>Play commands:</b></u>",
  "help_queue": "<u><b>Queue commands:</b></u>",
  "help_stats": "<u><b>Stats commands:</b></u>",
  "help_sudo": "<b><u>Sudo commands:</b></u>"
})JSON";

void loadLanguages(Language& lang, const char* argvDir) {
    std::vector<std::string> candidates;
    if (argvDir && *argvDir)
        candidates.push_back(argvDir);
#ifdef ANONX_LOCALES_DIR
    candidates.push_back(ANONX_LOCALES_DIR);
#endif
    candidates.push_back("locales");
    candidates.push_back("../locales");
    candidates.push_back("../../locales");

    for (const std::string& dir : candidates) {
        if (lang.loadDir(dir) > 0 && lang.loaded("en")) {
            std::cout << "locales loaded from: " << dir << "\n";
            return;
        }
    }
    std::cout << "locales not found on disk — using the built-in table\n";
    lang.loadJsonText("en", kFallbackEn);
}

std::string freshDbPath() {
    static int counter = 0;
    const std::string path = "anonx_admin_test_" + std::to_string(++counter) + ".db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    return path;
}

struct Bot {
    std::string        dbPath;
    FakeBotApi         api;
    Database           db;
    CacheManager       cache;
    Queue              queue;
    FakeVoiceTransport tp;
    CallManager        calls;
    FakeSystemInfo     sys;
    Config             config;
    AdminPlugins       admin;
    LangView           L;

    explicit Bot(const Language& lang)
        : dbPath(freshDbPath()), db(dbPath), calls(tp, queue, cache),
          admin(AdminPlugins::Deps{api, db, cache, calls, sys, lang, config}),
          L(lang.view("en")) {
        config.owner_id       = kOwner;
        config.logger_id      = kLogger;
        config.support_chat   = "https://t.me/support";
        config.support_channel = "https://t.me/channel";
        config.auto_leave     = false;
        api.makeAdmin(kChat, kAdmin);
        api.titles[kChat] = "Test Group";
    }

    ~Bot() {
        std::remove(dbPath.c_str());
        std::remove((dbPath + "-wal").c_str());
        std::remove((dbPath + "-shm").c_str());
    }

    Bot(const Bot&)            = delete;
    Bot& operator=(const Bot&) = delete;
};

CommandEvent cmd(std::int64_t chatId, std::int64_t userId,
                 std::vector<std::string> tokens, std::int64_t replyTo = 0,
                 std::int64_t messageId = 500) {
    CommandEvent ev;
    ev.chatId            = chatId;
    ev.messageId         = messageId;
    ev.fromUserId        = userId;
    ev.isPrivate         = chatId > 0;
    ev.command           = std::move(tokens);
    ev.replyToMessageId  = replyTo;
    return ev;
}

ButtonEvent btn(std::int64_t chatId, std::int64_t userId, std::string data,
                std::int64_t messageId = 700, std::int64_t queryId = 991) {
    ButtonEvent ev;
    ev.chatId     = chatId;
    ev.messageId  = messageId;
    ev.fromUserId = userId;
    ev.queryId    = queryId;
    ev.data       = std::move(data);
    return ev;
}

std::string answerText(const Bot& bot) {
    const FakeBotApi::Record* r = bot.api.last("answer");
    return r ? r->text : std::string("<no callback answered>");
}
bool answerWasAlert(const Bot& bot) {
    const FakeBotApi::Record* r = bot.api.last("answer");
    return r != nullptr && r->alert;
}

InlineKeyboard lastKb(const Bot& bot) {
    for (auto it = bot.api.log.rbegin(); it != bot.api.log.rend(); ++it)
        if (it->op == "send" || it->op == "edit" || it->op == "markup")
            return it->kb;
    return {};
}

std::vector<anonx::InlineButton> lastButtons(const Bot& bot) {
    std::vector<anonx::InlineButton> out;
    for (const auto& row : lastKb(bot))
        for (const auto& b : row)
            out.push_back(b);
    return out;
}

std::string sentTo(const Bot& bot, std::int64_t chatId) {
    for (const auto& r : bot.api.log)
        if (r.op == "send" && r.chatId == chatId)
            return r.text;
    return {};
}

std::vector<std::int64_t> sorted(std::vector<std::int64_t> v) {
    std::sort(v.begin(), v.end());
    return v;
}

void testTables(const Language& lang) {
    const LangView L = lang.view("en");

    CHECK(AdminPlugins::allCommandGroups().size() == 14u,
          "fourteen admin command groups");
    CHECK(AdminPlugins::moduleCount() == 8 + 14,
          "moduleCount = the eight playback groups plus the admin groups");
    CHECK(AdminPlugins::helpTopics().size() == 9u, "nine help topics");

    std::set<std::string> seen;
    std::size_t total = 0;
    std::vector<std::vector<std::string>> groups = AdminPlugins::allCommandGroups();
    for (const auto& g : {Plugins::playCommands(), Plugins::skipCommands(),
                          Plugins::pauseCommands(), Plugins::resumeCommands(),
                          Plugins::stopCommands(), Plugins::loopCommands(),
                          Plugins::queueCommands(), Plugins::seekCommands()})
        groups.push_back(g);
    for (const auto& group : groups)
        for (const std::string& name : group) {
            ++total;
            seen.insert(name);
        }
    CHECK(seen.size() == total, "every command name is registered exactly once");

    for (const auto& topic : AdminPlugins::helpTopics()) {
        CHECK(L[topic.first] != "{" + topic.first + "}",
              "help label " + topic.first + " resolves");
        CHECK(L[topic.second] != "{" + topic.second + "}",
              "help page " + topic.second + " resolves");
    }

    CHECK(SystemInfo::formatDuration(0) == "0s", "0s");
    CHECK(SystemInfo::formatDuration(45) == "45s", "45s");
    CHECK(SystemInfo::formatDuration(90) == "1m 30s", "1m 30s");
    CHECK(SystemInfo::formatDuration(3725) == "1h 2m 5s", "1h 2m 5s");
    CHECK(SystemInfo::formatDuration(90061) == "1d 1h 1m 1s", "1d 1h 1m 1s");
    CHECK(SystemInfo::formatDuration(-5) == "0s", "negative uptime clamps to 0s");
    CHECK(SystemInfo::round1(12.34) == "12.3", "round1 truncates down");
    CHECK(SystemInfo::round1(99.96) == "100.0", "round1 carries over");
    CHECK(SystemInfo::round1(7.0) == "7.0", "round1 always shows one decimal");
}

void testAuth(const Language& lang) {
    Bot bot(lang);

    bot.admin.onAuth(cmd(kChat, kUser, {"auth", "8"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "a member cannot /auth");
    CHECK(!bot.db.isAuth(kChat, 8), "and nothing was stored");

    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth"}));
    CHECK(bot.api.lastSaid() == bot.L["user_not_found"], "/auth needs a target");

    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth", "8"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("auth_added", "@u8"), "auth_added rendered");
    CHECK(bot.db.isAuth(kChat, 8), "and the user is authorized");

    bot.api.seedMessage(kChat, 400, "hello", 11);
    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth"}, 400));
    CHECK(bot.api.lastSaid() == bot.L.fmt("auth_added", "@u11"),
          "the replied-to sender is the target");
    CHECK(bot.db.isAuth(kChat, 11), "reply target authorized");

    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth", "7"}));
    CHECK(bot.api.lastSaid() == bot.L["auth_is_admin"], "admins are refused");
    CHECK(!bot.db.isAuth(kChat, kAdmin), "and not stored");

    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["user_not_found"], "negative ids are not users");
    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"auth", "not-a-number"}));
    CHECK(bot.api.lastSaid() == bot.L["user_not_found"], "garbage is not a user");

    bot.api.titles[kChat] = "Rock & Roll";
    bot.api.clear();
    bot.admin.onAuthList(cmd(kChat, kUser, {"authlist"}));
    const std::string list = bot.api.lastSaid();
    CHECK(list.rfind(bot.L.fmt("auth_list", Plugins::htmlEscape("Rock & Roll")), 0) == 0,
          "the list opens with the escaped chat title");
    CHECK(list.find("- @u8\n") != std::string::npos, "first authorized user listed");
    CHECK(list.find("- @u11\n") != std::string::npos, "second authorized user listed");

    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kAdmin, {"unauth", "8"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("auth_removed", "@u8"), "auth_removed rendered");
    CHECK(!bot.db.isAuth(kChat, 8), "and the user lost the authorization");
    bot.admin.onAuth(cmd(kChat, kAdmin, {"unauth", "11"}));

    bot.api.clear();
    bot.admin.onAuthList(cmd(kChat, kUser, {"authlist"}));
    CHECK(bot.api.lastSaid() == bot.L["auth_empty"], "an empty list says so");

    bot.db.addSudo(kSudo);
    bot.api.clear();
    bot.admin.onAuth(cmd(kChat, kSudo, {"auth", "8"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("auth_added", "@u8"), "a sudo user may /auth");
    bot.api.clear();
    bot.admin.onAuth(cmd(kUser, kUser, {"auth", "12"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("auth_added", "@u12"), "no gate in private");
}

void testBlacklist(const Language& lang) {
    Bot bot(lang);

    bot.admin.onBlacklist(cmd(kChat, kAdmin, {"blacklist", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"],
          "a group admin is not sudo enough to blacklist");
    CHECK(!bot.db.isBlacklistedChat(-1001), "and nothing was stored");

    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("bl_usage", "blacklist"),
          "no target -> usage, naming the command that was used");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"unblacklist"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("bl_usage", "unblacklist"),
          "usage names /unblacklist too");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "abc"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_invalid"], "garbage id rejected");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "0"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_invalid"], "zero is not an id");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "99999999999999999999"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_invalid"], "an overflowing id is rejected");

    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_added"], "chat blacklisted");
    CHECK(bot.db.isBlacklistedChat(-1001), "and persisted");
    CHECK(bot.api.count("send") == 1u, "a chat gets no courtesy DM");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_already"], "a second time is a no-op");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"unblacklist", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_removed"], "chat removed");
    CHECK(!bot.db.isBlacklistedChat(-1001), "and gone from the DB");
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"whitelist", "-1001"}));
    CHECK(bot.api.lastSaid() == bot.L["bl_not"],
          "/whitelist is an alias of /unblacklist");

    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kOwner, {"blacklist", "55"}));
    CHECK(sentTo(bot, kChat) == bot.L["bl_added"], "user blacklisted");
    CHECK(bot.db.isBlacklistedUser(55), "and persisted as a user, not a chat");
    CHECK(!bot.db.isBlacklistedChat(55), "positive ids never land in the chat list");
    CHECK(sentTo(bot, 55) == bot.L.fmt("bl_user_notify", bot.config.support_chat),
          "the user is told, with the support link");

    bot.db.addSudo(kSudo);
    bot.api.seedMessage(kChat, 401, "spam", 66);
    bot.api.clear();
    bot.admin.onBlacklist(cmd(kChat, kSudo, {"blacklist"}, 401));
    CHECK(sentTo(bot, kChat) == bot.L["bl_added"],
          "a sudo user may blacklist by reply");
    CHECK(bot.db.isBlacklistedUser(66), "the replied-to sender is blacklisted");
    CHECK(sentTo(bot, 66) == bot.L.fmt("bl_user_notify", bot.config.support_chat),
          "and that user is notified too");
}

void testGcast(const Language& lang) {
    Bot bot(lang);
    bot.db.addChat(-1001);
    bot.db.addChat(-1002);
    bot.db.addUser(11);
    bot.db.addUser(12);
    bot.api.seedMessage(kChat, 400, "announcement", kOwner);

    bot.admin.onGcast(cmd(kChat, kUser, {"gcast"}, 400));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "only sudo may broadcast");
    CHECK(bot.api.copyTargets.empty(), "and nothing was relayed");
    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast"}));
    CHECK(bot.api.lastSaid() == bot.L["gcast_usage"], "a reply is required");

    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast"}, 400));
    CHECK(bot.api.log.front().text == bot.L["gcast_start"], "the run announces itself");
    CHECK(sorted(bot.api.copyTargets) == std::vector<std::int64_t>({-1002, -1001}),
          "both groups reached, no users");
    CHECK(bot.api.count("forward") == 2u && bot.api.count("copy") == 0u,
          "without -copy the message is forwarded");
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 2, 0), "gcast_end counts groups");

    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast", "-user", "-copy"}, 400));
    CHECK(sorted(bot.api.copyTargets) == std::vector<std::int64_t>({-1002, -1001, 11, 12}),
          "-user adds the served users");
    CHECK(bot.api.count("copy") == 4u && bot.api.count("forward") == 0u,
          "-copy strips the forward header");
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 2, 2), "both counters reported");

    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast", "-NoChat", "-USER"}, 400));
    CHECK(sorted(bot.api.copyTargets) == std::vector<std::int64_t>({11, 12}),
          "-nochat skips the groups");
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 0, 2), "no groups counted");

    bot.db.addBlacklist(-1002);
    bot.db.addBlacklist(12);
    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"broadcast", "-user"}, 400));
    CHECK(sorted(bot.api.copyTargets) == std::vector<std::int64_t>({-1001, 11}),
          "the blacklisted chat and user are left out");
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 1, 1),
          "and they are not counted either");
    bot.db.removeBlacklist(-1002);
    bot.db.removeBlacklist(12);

    bot.api.copiesFail = true;
    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast", "-user"}, 400));
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 0, 0), "failed relays count zero");
    bot.api.copiesFail = false;

    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast"}, 987654));
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 0, 0), "a missing message is a no-op");

    bot.api.clear();
    bot.admin.onGcast(cmd(kChat, kOwner, {"gcast"}, 400));
    CHECK(bot.api.lastSaid() == bot.L.fmt("gcast_end", 2, 0),
          "the broadcast flag is reset after each run");
    CHECK(!bot.api.said(bot.L["gcast_active"]), "so the busy notice never appeared");
}

void testSudoers(const Language& lang) {
    Bot bot(lang);

    CHECK(!bot.db.isSudo(kOwner), "the owner is not stored in the sudo table");
    CHECK(guards::isSudo(bot.db, bot.config, kOwner), "but counts as sudo");
    CHECK(!guards::isSudo(bot.db, bot.config, kAdmin), "a group admin does not");

    bot.db.addSudo(kSudo);
    bot.admin.onSudo(cmd(kChat, kSudo, {"addsudo", "55"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"],
          "even a sudo user cannot grant sudo");
    CHECK(!bot.db.isSudo(55), "and nothing was stored");

    bot.api.clear();
    bot.admin.onSudo(cmd(kChat, kOwner, {"addsudo"}));
    CHECK(bot.api.lastSaid() == bot.L["user_not_found"], "/addsudo needs a target");

    bot.api.clear();
    bot.admin.onSudo(cmd(kChat, kOwner, {"addsudo", "55"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_added", "@u55"), "sudo_added rendered");
    CHECK(bot.db.isSudo(55), "and persisted");
    bot.api.clear();
    bot.admin.onSudo(cmd(kChat, kOwner, {"addsudo", "55"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_already", "@u55"), "a second time is a no-op");

    bot.api.clear();
    bot.admin.onSudo(cmd(kChat, kOwner, {"rmsudo", "55"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_removed", "@u55"), "sudo_removed rendered");
    CHECK(!bot.db.isSudo(55), "and gone from the DB");
    bot.api.clear();
    bot.admin.onSudo(cmd(kChat, kOwner, {"rmsudo", "55"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_not", "@u55"), "removing twice is a no-op");

    bot.api.clear();
    bot.admin.onSudoList(cmd(kChat, kUser, {"sudolist"}));
    CHECK(bot.api.count("send") == 1u && bot.api.count("edit") == 1u,
          "one 'fetching' message, edited into the list");
    CHECK(bot.api.log.front().text == bot.L["sudo_fetching"], "the placeholder is shown");
    CHECK(bot.api.log.front().messageId == bot.api.log.back().messageId,
          "and it is the very message that becomes the list");
    const std::string list = bot.api.lastSaid();
    CHECK(list.rfind(bot.L.fmt("sudo_owner", "@u9"), 0) == 0, "the owner heads the list");
    CHECK(list.find(bot.L["sudo_users"]) != std::string::npos, "the sudo header follows");
    CHECK(list.find("\n- @u10") != std::string::npos, "and the sudo user is listed");

    bot.db.removeSudo(kSudo);
    bot.api.clear();
    bot.admin.onSudoList(cmd(kChat, kUser, {"sudolist"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_owner", "@u9"),
          "only the owner block remains");

    bot.api.editsFail = true;
    bot.api.clear();
    bot.admin.onSudoList(cmd(kChat, kUser, {"sudolist"}));
    CHECK(bot.api.count("send") == 2u, "a failed edit falls back to a new message");
    CHECK(bot.api.lastSaid() == bot.L.fmt("sudo_owner", "@u9"), "and still carries the list");
    bot.api.editsFail = false;
}

void testInfo(const Language& lang) {

    {
        Bot bot(lang);
        bot.tp.pingValue = 42.0;
        bot.admin.onPing(cmd(kChat, kUser, {"ping"}));
        CHECK(bot.api.count("send") == 1u && bot.api.count("edit") == 1u,
              "one 'pinging' message, edited into the card");
        CHECK(bot.api.log.front().text == bot.L["pinging"], "the placeholder is shown");
        CHECK(bot.api.log.front().messageId == bot.api.log.back().messageId,
              "and it is the message that becomes the card");
        CHECK(bot.sys.cpuCalls == 1, "the CPU is sampled exactly once");

        const std::string card = bot.api.lastSaid();
        const std::string open = "<code>";
        const std::size_t from = card.find(open) + open.size();
        const std::size_t to   = card.find("ms</code>", from);
        const std::string latency = to == std::string::npos
                                        ? std::string()
                                        : card.substr(from, to - from);
        CHECK(!latency.empty() &&
                  latency.find_first_not_of("0123456789") == std::string::npos,
              "the latency field holds a plain millisecond count");
        CHECK(card == bot.L.fmt("ping_pong", latency, "1h 2m 5s", "12.5", "43.2",
                                "60.0", "42.0"),
              "the whole ping card matches ping_pong byte for byte");

        const std::vector<anonx::InlineButton> kb = lastButtons(bot);
        CHECK(kb.size() == 1u, "one button on the ping card");
        CHECK(!kb.empty() && kb[0].text == bot.L["support"], "labelled 'Support'");
        CHECK(!kb.empty() && kb[0].kind == anonx::InlineButton::Kind::Url &&
                  kb[0].url == bot.config.support_chat,
              "and linking to SUPPORT_CHAT");

        bot.config.support_chat.clear();
        bot.api.clear();
        bot.admin.onPing(cmd(kChat, kUser, {"ping"}));
        CHECK(lastButtons(bot).empty(), "no support chat -> no button");
    }

    {
        Bot bot(lang);
        bot.db.addChat(-1001);
        bot.db.addChat(-1002);
        bot.db.addUser(11);
        bot.db.addBlacklist(-1003);
        bot.db.addBlacklist(12);
        bot.db.addSudo(kSudo);

        const std::string expectedUser =
            bot.L.fmt("stats_user", "AnonXMusic", 1, buttons::toggleMark(false), 1, 1,
                      2, 2, 1);

        bot.admin.onStats(cmd(kChat, kUser, {"stats"}));
        CHECK(bot.api.log.front().text == bot.L["stats_fetching"], "placeholder first");
        CHECK(bot.api.count("edit") == 1u, "then the card replaces it");
        CHECK(bot.api.lastSaid() == expectedUser,
              "the plain stats card matches stats_user byte for byte "
              "(owner counted as a sudo user)");

        bot.api.clear();
        bot.admin.onStats(cmd(kChat, kOwner, {"stats"}));
        const std::string expectedSudo =
            bot.L.fmt("stats_sudo", AdminPlugins::moduleCount(), "Linux 6.1.0 x86_64",
                      512, "7.8", "12.5", 4, "30.5", "50.0", "C++17 (g++ 13.2.0)",
                      "TDLib (JSON interface)", "NTgCalls");
        CHECK(bot.api.lastSaid() == expectedUser + expectedSudo,
              "a sudo user also gets the host block, appended to the same card");
        CHECK(expectedSudo.find("Modules:") != std::string::npos &&
                  expectedUser.find("Modules:") == std::string::npos,
              "and only that block mentions the module count");

        bot.config.auto_leave = true;
        bot.api.clear();
        bot.admin.onStats(cmd(kChat, kUser, {"stats"}));
        CHECK(bot.api.lastSaid().find(std::string("<b>Auto leave:</b> ") +
                                      buttons::toggleMark(true)) != std::string::npos,
              "auto leave shows the on mark");

        bot.db.addSudo(kOwner);
        bot.api.clear();
        bot.admin.onStats(cmd(kChat, kUser, {"stats"}));
        CHECK(bot.api.lastSaid().find("<b>Sudo users:</b> 2") != std::string::npos,
              "the owner is never counted twice");
    }

    {
        Bot bot(lang);
        bot.admin.onActiveVc(cmd(kChat, kUser, {"ac"}));
        CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "the lists are sudo-only");

        bot.api.clear();
        bot.admin.onActiveVc(cmd(kChat, kOwner, {"ac"}));
        CHECK(bot.api.lastSaid() == bot.L["vc_empty"], "no streams -> vc_empty");
        bot.api.clear();
        bot.admin.onActiveVc(cmd(kChat, kOwner, {"activevc"}));
        CHECK(bot.api.lastSaid() == bot.L["vc_empty"], "the long form too");

        bot.cache.addCall(-1001);
        bot.cache.addCall(kChat);
        bot.api.titles[-1001] = "Alpha & Beta";

        bot.api.clear();
        bot.admin.onActiveVc(cmd(kChat, kOwner, {"ac"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("vc_count", 2), "/ac reports just the count");
        CHECK(bot.api.count("edit") == 0u, "and needs no placeholder");

        bot.api.clear();
        bot.admin.onActiveVc(cmd(kChat, kOwner, {"activevc"}));
        CHECK(bot.api.log.front().text == bot.L["vc_fetching"], "the long form fetches");
        CHECK(bot.api.count("edit") == 1u, "and edits the placeholder into the list");
        const std::string list = bot.api.lastSaid();
        CHECK(list.rfind(bot.L["vc_list"], 0) == 0, "the header opens the list");
        CHECK(list.find("\n- <code>-1001</code> | Alpha &amp; Beta") != std::string::npos,
              "each line carries the id and the escaped title");
        CHECK(list.find("\n- <code>" + std::to_string(kChat) + "</code> | Test Group") !=
                  std::string::npos,
              "the second stream is listed too");
        CHECK(list.find("<code>" + std::to_string(kChat) + "</code>") <
                  list.find("<code>-1001</code>"),
              "and the chats come out in ascending id order");
    }
}

void testMenuCommands(const Language& lang) {
    Bot bot(lang);

    bot.admin.onStart(cmd(kUser, kUser, {"start"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("start_pm", "@u8", "AnonXMusic"),
          "the private start card greets the sender");
    InlineKeyboard kb = lastKb(bot);
    CHECK(kb.size() == 4u, "help / add-me / links / source");
    CHECK(kb.size() == 4u && kb[0][0].data == "help menu", "the help button opens the menu");
    CHECK(kb.size() == 4u && kb[0][0].text == bot.L["help"], "and is labelled Help");
    CHECK(kb.size() == 4u && kb[1][0].url == "https://t.me/AnonXMusicBot?startgroup=true",
          "the add-me link is built from the bot username");
    CHECK(kb.size() == 4u && kb[2].size() == 2u &&
              kb[2][0].url == bot.config.support_chat &&
              kb[2][1].url == bot.config.support_channel,
          "the link row carries support + channel");
    CHECK(kb.size() == 4u &&
              kb[3][0].url == "https://github.com/AnonymousX1025/AnonXMusic",
          "and the source button points at the upstream project");

    bot.api.clear();
    bot.admin.onStart(cmd(kChat, kUser, {"start"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("start_gp", "AnonXMusic"),
          "the group card names the bot only");
    CHECK(lastKb(bot).size() == 3u, "and drops the source row");

    bot.api.botUser.clear();
    bot.api.clear();
    bot.admin.onStart(cmd(kUser, kUser, {"start"}));
    kb = lastKb(bot);
    CHECK(kb.size() == 3u, "no username -> no add-me row");
    CHECK(kb.size() == 3u && kb[1].size() == 2u && kb[1][0].url == bot.config.support_chat,
          "the remaining rows keep their order");
    bot.api.botUser = "AnonXMusicBot";

    bot.api.clear();
    bot.admin.onHelp(cmd(kChat, kUser, {"help"}));
    CHECK(bot.api.lastSaid() == bot.L["help_menu"], "the help body is the menu text");
    CHECK(bot.api.lastKeyboardData() ==
              std::vector<std::string>({"help 0", "help 1", "help 2", "help 3", "help 4",
                                        "help 5", "help 6", "help 7", "help 8", "close"}),
          "nine topic payloads plus close");
    CHECK(lastKb(bot).size() == 4u, "laid out three per row");
    CHECK(!lastButtons(bot).empty() && lastButtons(bot)[0].text == bot.L["help_0"],
          "and labelled from the locale table");

    bot.api.clear();
    bot.admin.onSettings(cmd(kChat, kUser, {"settings"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "a member cannot open settings");
    bot.api.clear();
    bot.admin.onSettings(cmd(kChat, kAdmin, {"settings"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("start_settings", "AnonXMusic"),
          "an admin gets the settings card");
    CHECK(bot.api.lastKeyboardData() ==
              std::vector<std::string>({"settings toggle cmd_delete",
                                        "settings toggle play_mode", "lang menu", "close"}),
          "with the two toggles, the language shortcut and close");
    std::vector<anonx::InlineButton> row = lastButtons(bot);
    CHECK(row.size() == 4u &&
              row[0].text == bot.L["cmd_delete"] + " " + buttons::toggleMark(false),
          "both toggles start off");
    CHECK(row.size() == 4u &&
              row[1].text == bot.L["play_mode"] + " " + buttons::toggleMark(false),
          "including admin-only play");
    bot.api.clear();
    bot.admin.onSettings(cmd(kChat, kOwner, {"settings"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("start_settings", "AnonXMusic"),
          "a sudo user may open them without being an admin");

    bot.api.clear();
    bot.admin.onLang(cmd(kChat, kUser, {"lang"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "a member cannot change language");
    bot.api.clear();
    bot.admin.onLang(cmd(kChat, kAdmin, {"language"}));
    CHECK(bot.api.lastSaid() == bot.L["lang_choose"], "the picker is offered");
    const std::vector<std::string> data = bot.api.lastKeyboardData();
    CHECK(std::find(data.begin(), data.end(), "lang set en") != data.end(),
          "English is on the grid");
    CHECK(data.size() >= 3u && data[data.size() - 2] == "settings menu" &&
              data.back() == "close",
          "and the grid ends with back + close");
    std::size_t offered = 0;
    for (const std::string& d : data) {
        const std::string prefix = "lang set ";
        if (d.rfind(prefix, 0) != 0)
            continue;
        ++offered;
        CHECK(lang.loaded(d.substr(prefix.size())),
              "every offered language really is loaded: " + d);
    }
    CHECK(offered == data.size() - 2, "every button but back/close offers a language");
}

void testCallbacks(const Language& lang) {

    {
        Bot bot(lang);
        bot.admin.onMenu(btn(kChat, kUser, "close"));
        CHECK(bot.api.count("answer") == 1u && answerText(bot).empty(),
              "close answers the query silently");
        CHECK(bot.api.count("delete") == 1u &&
                  bot.api.last("delete")->messageId == 700,
              "and deletes the menu message");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "start menu"));
        CHECK(bot.api.lastSaid() == bot.L.fmt("start_gp", "AnonXMusic"),
              "'start menu' edits the group card back in");
        CHECK(bot.api.count("edit") == 1u && bot.api.count("send") == 0u,
              "by editing, never by sending a new message");
        bot.api.clear();
        bot.admin.onMenu(btn(kUser, kUser, "start menu"));
        CHECK(bot.api.lastSaid() == bot.L.fmt("start_pm", "@u8", "AnonXMusic"),
              "and a positive chat id means the private card");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "help menu"));
        CHECK(bot.api.lastSaid() == bot.L["help_menu"], "'help menu' shows the grid");
        CHECK(bot.api.lastKeyboardData().size() == 10u, "with all ten buttons");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "help 5"));
        CHECK(bot.api.lastSaid() == bot.L["help_play"], "'help 5' is the play page");
        CHECK(bot.api.lastKeyboardData() ==
                  std::vector<std::string>({"help menu", "close"}),
              "and offers back + close");
        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "help 0"));
        CHECK(bot.api.lastSaid() == bot.L["help_admins"], "'help 0' is the admin page");
        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "help 8"));
        CHECK(bot.api.lastSaid() == bot.L["help_sudo"], "'help 8' is the last page");

        for (const char* payload : {"help 9", "help -1", "help x"}) {
            bot.api.clear();
            bot.admin.onMenu(btn(kChat, kUser, payload));
            CHECK(bot.api.count("answer") == 1u && bot.api.count("edit") == 0u,
                  std::string("a bad help payload is ignored: ") + payload);
        }

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, ""));
        CHECK(bot.api.log.empty(), "an empty payload is a no-op");
        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "lang"));
        CHECK(bot.api.log.empty(), "so is a namespace with no action");
    }

    {
        Bot bot(lang);
        bot.admin.onMenu(btn(kChat, kUser, "settings menu"));
        CHECK(bot.api.lastSaid() == bot.L.fmt("start_settings", "AnonXMusic"),
              "'settings menu' shows the card");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kUser, "settings toggle cmd_delete"));
        CHECK(answerText(bot) == bot.L["user_no_perms"] && answerWasAlert(bot),
              "a member gets an alert, not a change");
        CHECK(!bot.db.getCmdDelete(kChat), "and the setting is untouched");
        CHECK(bot.api.count("markup") == 0u, "no keyboard refresh either");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kAdmin, "settings toggle cmd_delete"));
        CHECK(bot.db.getCmdDelete(kChat), "an admin flips command delete on");
        CHECK(bot.api.count("markup") == 1u && bot.api.count("edit") == 0u,
              "only the markup is refreshed, the card text stays");
        CHECK(answerText(bot).empty() && !answerWasAlert(bot),
              "and the query is answered silently");
        std::vector<anonx::InlineButton> row = lastButtons(bot);
        CHECK(row.size() == 4u &&
                  row[0].text == bot.L["cmd_delete"] + " " + buttons::toggleMark(true),
              "the refreshed button shows the on mark");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kAdmin, "settings toggle cmd_delete"));
        CHECK(!bot.db.getCmdDelete(kChat), "pressing again flips it back off");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kOwner, "settings toggle play_mode"));
        CHECK(bot.db.getPlayMode(kChat), "a sudo user may flip admin-only play");
        row = lastButtons(bot);
        CHECK(row.size() == 4u &&
                  row[1].text == bot.L["play_mode"] + " " + buttons::toggleMark(true),
              "and its mark follows");
        CHECK(!bot.db.getPlayMode(-1009), "the flag is per chat");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kAdmin, "settings toggle nonsense"));
        CHECK(bot.api.log.empty(), "an unknown toggle changes nothing");
    }

    {
        Bot bot(lang);

        bot.admin.onMenu(btn(kChat, kUser, "lang menu"));
        CHECK(bot.api.lastSaid() == bot.L["lang_choose"], "'lang menu' shows the picker");

        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kAdmin, "lang set en"));
        CHECK(answerText(bot) == bot.L.fmt("lang_same", "English") && answerWasAlert(bot),
              "picking the current language just says so");
        CHECK(bot.api.count("edit") == 0u, "and edits nothing");

        std::string target;
        for (const auto& entry : Language::allCodes())
            if (entry.first != "en" && lang.loaded(entry.first)) {
                target = entry.first;
                break;
            }

        CHECK(lang.codes().size() < 2u || !target.empty(),
              "when several locales are loaded, one of them is not English");
        if (!target.empty()) {
            const std::string name = Language::nameOf(target);
            const LangView N = lang.view(target);

            bot.api.clear();
            bot.admin.onMenu(btn(kChat, kUser, "lang set " + target));
            CHECK(answerText(bot) == bot.L["user_no_perms"] && answerWasAlert(bot),
                  "a member cannot switch the language");
            CHECK(bot.db.getLang(kChat) == "en", "and the chat stays on English");

            bot.api.clear();
            bot.admin.onMenu(btn(kChat, kAdmin, "lang set " + target));
            CHECK(answerText(bot) == bot.L.fmt("lang_change", name) && !answerWasAlert(bot),
                  "the switch is confirmed in the OLD language");
            CHECK(bot.db.getLang(kChat) == target, "the chat language is persisted");
            CHECK(bot.api.lastSaid() == N.fmt("lang_changed", name),
                  "and the card is rewritten in the NEW language");
            CHECK(bot.api.lastKeyboardData() ==
                      std::vector<std::string>({"settings menu", "close"}),
                  "its back button returns to the settings card");
            CHECK(!lastButtons(bot).empty() && lastButtons(bot)[0].text == N["back"],
                  "and even the button labels follow the new language");

            bot.api.clear();
            bot.admin.onHelp(cmd(kChat, kUser, {"help"}));
            CHECK(bot.api.lastSaid() == N["help_menu"],
                  "the chat's language now drives every card");
        }

        const std::string before = bot.db.getLang(kChat);
        bot.api.clear();
        bot.admin.onMenu(btn(kChat, kAdmin, "lang set qq"));
        CHECK(bot.api.log.empty(), "an unloaded language code is ignored");
        CHECK(bot.db.getLang(kChat) == before, "and the chat keeps its language");
    }
}

void testWatcher(const Language& lang) {
    Bot bot(lang);
    const LangView L = lang.view(lang.defaultCode());

    CHECK(bot.db.getLoggerEnabled(), "logging is on by default");

    bot.api.usernames[kUser] = "bob";
    bot.admin.onSeen(cmd(kUser, kUser, {"start"}));
    CHECK(bot.db.isUser(kUser), "the user is now served");
    CHECK(bot.api.count("send") == 1u && bot.api.last("send")->chatId == kLogger,
          "and one notice went to the log group");
    CHECK(sentTo(bot, kLogger) == L.fmt("log_user", kUser, "@u8", "@bob"),
          "log_user carries id, mention and @username");

    bot.api.clear();
    bot.admin.onSeen(cmd(kUser, kUser, {"help"}));
    CHECK(bot.api.log.empty(), "a known user is logged only once");

    bot.api.clear();
    bot.admin.onSeen(cmd(21, 21, {"start"}));
    CHECK(sentTo(bot, kLogger) == L.fmt("log_user", 21, "@u21", "-"),
          "a missing username renders as '-'");

    bot.api.clear();
    bot.admin.onSeen(cmd(kChat, kUser, {"play", "song"}));
    CHECK(bot.db.isChat(kChat), "the chat is now served");
    CHECK(sentTo(bot, kLogger) == L.fmt("log_chat", kChat, "Test Group", kUser, "@u8"),
          "log_chat carries the chat, its title and who brought the bot in");
    bot.api.clear();
    bot.admin.onSeen(cmd(kChat, kAdmin, {"skip"}));
    CHECK(bot.api.log.empty(), "a known chat is logged only once");

    bot.api.clear();
    bot.api.titles[-1005] = "Anon <Group> & Co";
    bot.admin.onSeen(cmd(-1005, 0, {"play"}));
    CHECK(bot.db.isChat(-1005), "the chat is still registered");
    CHECK(sentTo(bot, kLogger) ==
              L.fmt("log_chat", -1005, Plugins::htmlEscape("Anon <Group> & Co"), 0, "-"),
          "the title is escaped and the missing user renders as '-'");

    bot.api.clear();
    bot.admin.onSeen(cmd(kLogger, kUser, {"anything"}));
    CHECK(!bot.db.isChat(kLogger) && bot.api.log.empty(),
          "the log group never logs itself");
    bot.admin.onSeen(cmd(0, 0, {"anything"}));
    CHECK(bot.api.log.empty(), "a message with no chat is ignored");

    bot.db.addBlacklist(-1006);
    bot.admin.onSeen(cmd(-1006, kUser, {"anything"}));
    CHECK(!bot.db.isChat(-1006) && bot.api.log.empty(),
          "a blacklisted chat is not served");
    bot.db.addBlacklist(77);
    bot.admin.onSeen(cmd(77, 77, {"anything"}));
    CHECK(!bot.db.isUser(77) && bot.api.log.empty(), "nor a blacklisted user");
    bot.admin.onSeen(cmd(-1007, 77, {"anything"}));
    CHECK(!bot.db.isChat(-1007), "a blacklisted user cannot grow the chat list either");

    bot.db.setLoggerEnabled(false);
    bot.api.clear();
    bot.admin.onSeen(cmd(-1008, kUser, {"anything"}));
    CHECK(bot.db.isChat(-1008), "the chat is still served when logging is off");
    CHECK(bot.api.log.empty(), "but nothing is posted");
    bot.db.setLoggerEnabled(true);

    bot.config.logger_id = 0;
    bot.api.clear();
    bot.admin.onSeen(cmd(-1009, kUser, {"anything"}));
    CHECK(bot.db.isChat(-1009) && bot.api.log.empty(),
          "with no LOGGER_ID the notice is simply skipped");
    bot.config.logger_id = kLogger;

    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kAdmin, {"logger", "off"}));
    CHECK(bot.api.lastSaid() == bot.L["user_no_perms"], "the switch is sudo-only");
    CHECK(bot.db.getLoggerEnabled(), "and stayed on");

    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("logger_usage", "logger"),
          "no argument -> usage");
    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger", "maybe"}));
    CHECK(bot.api.lastSaid() == bot.L.fmt("logger_usage", "logger"),
          "an unknown argument -> usage");

    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger", "OFF"}));
    CHECK(bot.api.lastSaid() == bot.L["logger_off"], "'OFF' switches logging off");
    CHECK(!bot.db.getLoggerEnabled(), "and persists the choice");
    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger", "enable"}));
    CHECK(bot.api.lastSaid() == bot.L["logger_on"], "'enable' is an alias of on");
    CHECK(bot.db.getLoggerEnabled(), "and persists too");
    bot.api.clear();
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger", "disable"}));
    CHECK(bot.api.lastSaid() == bot.L["logger_off"], "'disable' is an alias of off");
    bot.admin.onLogger(cmd(kChat, kOwner, {"logger", "on"}));
    CHECK(bot.db.getLoggerEnabled(), "and back on again");
}

}

int main(int argc, char** argv) {
    Language lang;
    loadLanguages(lang, argc > 1 ? argv[1] : nullptr);
    lang.setDefault("en");

    const LangView L = lang.view("en");
    CHECK(L["ping_pong"].find("Pong!") != std::string::npos,
          "the language table really is loaded");
    CHECK(L["stats_user"].find("Served chats") != std::string::npos,
          "including the stats card");

    testTables(lang);
    testAuth(lang);
    testBlacklist(lang);
    testGcast(lang);
    testSudoers(lang);
    testInfo(lang);
    testMenuCommands(lang);
    testCallbacks(lang);
    testWatcher(lang);

    std::cout << "checks run: " << g_checks << ", failures: " << g_failures << "\n";
    if (g_failures != 0) {
        std::cerr << "ADMIN TESTS FAILED\n";
        return 1;
    }
    std::cout << "ALL ADMIN TESTS PASSED\n";
    return 0;
}
