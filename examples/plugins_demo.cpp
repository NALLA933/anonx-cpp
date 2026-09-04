#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "anonx/guards.hpp"
#include "anonx/plugins.hpp"
#include "../test/fake_bot_api/fake_bot_api.hpp"
#include "../test/fake_ntgcalls/fake_voice_transport.hpp"
#include "../test/fake_youtube/fake_youtube.hpp"

using anonx::ButtonEvent;
using anonx::CacheManager;
using anonx::CallManager;
using anonx::CommandEvent;
using anonx::Config;
using anonx::Database;
using anonx::FakeBotApi;
using anonx::FakeVoiceTransport;
using anonx::FakeYouTube;
using anonx::Language;
using anonx::LangView;
using anonx::MediaItem;
using anonx::PlayResult;
using anonx::Plugins;
using anonx::Queue;
using anonx::Track;

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::cerr << "FAIL: " << (msg) << "  [line " << __LINE__ << "]\n"; \
        }                                                                   \
    } while (0)

namespace {

constexpr std::int64_t kChat  = -1001234567890LL;
constexpr std::int64_t kBasic = -12345LL;
constexpr std::int64_t kAdmin = 7;
constexpr std::int64_t kUser  = 8;

const char* kFallbackEn = R"JSON({
  "play_searching": "Searching...",
  "play_downloading": "Downloading...",
  "play_media": "<u><b>| Started streaming</b></u>\n\n<b>Title:</b> <a href={0}>{1}</a>\n\n<b>Duration:</b> {2} min\n<b>Requested by:</b> {3}",
  "play_queued": "<u><b>Added to queue: {0}\n\n<b>Title:</b> <a href={1}>{2}</a>\n\n<b>Duration:</b> {3} min\n<b>Requested by:</b> {4}",
  "play_now": "Play Now",
  "play_usage": "<b>Usage:</b>\n\n<code>/play attention</code>",
  "play_user_invalid": "You're an anonymous admin.\n\nRevert back to user account.",
  "play_chat_invalid": "This bot can only be used in <b>supergroups</b>.",
  "play_queue_full": "The queue limit ({0}) has been reached.",
  "play_not_found": "Failed to process the query.\n\nIf the issue persists, report it to the <a href={0}>support chat</a>.",
  "play_admin": "<u><b>Admin only play</b></u>\n\nOnly admins are allowed to play in this chat.",
  "play_duration_limit": "Streams longer than {0} minutes are not allowed to play.",
  "play_paused": "<b>Stream paused by</b> {0}",
  "play_resumed": "<b>Stream resumed by</b> {0}",
  "play_skipped": "<b>Stream skipped by</b> {0}",
  "play_stopped": "<b>Stream ended by</b> {0}",
  "play_already_paused": "Do you remember that you resumed the stream?",
  "play_not_paused": "Do you remember that you paused the stream?",
  "play_expired": "This button has expired.",
  "play_next": "Hold on...\n\nDownloading next media from the queue.",
  "play_again": "Replaying the current media...",
  "play_seeking": "Seeking the current stream...",
  "play_seeked": "<b>Stream skipped {0} and started from {1} seconds by</b> {2}",
  "play_seek_usage": "<b>Usage:</b> /{0} duration\n<b>Example:</b> <code>/{0} 15</code>",
  "play_seek_min": "Minimum seek time is 10 seconds — try a bit longer!",
  "play_seek_no_dur": "Failed to fetch the duration of the ongoing stream.",
  "playlist_fetch": "Fetching the playlist...\n\nPlease hold on.",
  "playlist_error": "Something went wrong while fetching the playlist.",
  "playlist_queued": "<u><b>Added {0} tracks from the playlist to queue:</b></u>\n\n",
  "queue_curr": "<u><b>Currently playing:</b></u>\n\n<b>Title:</b> <a href={0}>{1}</a>\n<b>Duration:</b> {2}\n<b>Requested by:</b> {3}\n\n",
  "queue_item": "<b>{0}. Title:</b> {1}\n     - {2} min\n\n",
  "queue_fetching": "Fetching queue...",
  "loop_count": "The current loop count is: {0}",
  "loop_set": "Loop count set to: {0}",
  "loop_off": "Loop disabled.",
  "loop_usage": "<b>Usage:</b>\n\n<code>/loop [count]</code>\n<code>/loop off</code>",
  "not_playing": "The bot isn't streaming in the video chat.",
  "user_not_admin": "Oh sure, go ahead and manage the video chat... oh wait, you're not an admin.",
  "error_no_file": "Download failed.\n\nIf the issue persists, report it to the <a href={0}>support chat</a>",
  "error_no_call": "<b>No active video chat found.</b>\n\nPlease start one and <b>try again</b>.",
  "error_no_audio": "Moving to the next track...\n\nAudio source not found in the file.",
  "error_tg_server": "<u><b>Telegram server error</b></u>",
  "error_rtmp": "RTMP streaming is not supported.",
  "playing": "Playing",
  "paused": "Stream paused",
  "skipped": "Stream skipped",
  "stopped": "Stream ended",
  "replayed": "Stream replayed",
  "forward": "forward",
  "backward": "backward"
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
    const std::string path = "anonx_plugins_test_" + std::to_string(++counter) + ".db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    return path;
}

struct Bot {
    std::string      dbPath;
    FakeBotApi       api;
    Database         db;
    CacheManager     cache;
    Queue            queue;
    FakeYouTube      yt;
    FakeVoiceTransport tp;
    CallManager      calls;
    Config           config;
    Plugins          plugins;
    LangView         L;

    explicit Bot(const Language& lang)
        : dbPath(freshDbPath()), db(dbPath), calls(tp, queue, cache),
          plugins(Plugins::Deps{api, db, cache, queue, yt, calls, lang, config}),
          L(lang.view("en")) {
        plugins.attachCallbacks();
        api.makeAdmin(kChat, kAdmin);
    }

    ~Bot() {
        std::remove(dbPath.c_str());
        std::remove((dbPath + "-wal").c_str());
        std::remove((dbPath + "-shm").c_str());
    }

    Bot(const Bot&)            = delete;
    Bot& operator=(const Bot&) = delete;
};

Track mk(const std::string& id, int durationSec = 200) {
    Track t;
    t.id           = id;
    t.title        = "Song " + id;
    t.url          = "https://youtu.be/" + id;
    t.duration     = "3:20";
    t.duration_sec = durationSec;
    return t;
}

CommandEvent cmd(std::int64_t chatId, std::int64_t userId,
                 std::vector<std::string> tokens, std::int64_t messageId = 500) {
    CommandEvent ev;
    ev.chatId     = chatId;
    ev.messageId  = messageId;
    ev.fromUserId = userId;
    ev.isPrivate  = chatId > 0;
    ev.command    = std::move(tokens);
    return ev;
}

ButtonEvent btn(std::int64_t chatId, std::int64_t userId, std::string data,
                std::int64_t messageId, std::int64_t queryId = 991) {
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

std::int64_t startPlaying(Bot& bot, const std::string& id = "aaa") {
    bot.yt.nextSearch = mk(id);
    bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", id}));
    const FakeBotApi::Record* card = bot.api.last();
    return card ? card->messageId : 0;
}

void testGuards(const Language& lang) {
    Bot bot(lang);
    namespace guards = anonx::guards;

    CHECK(guards::isAdmin(bot.api, kChat, kAdmin), "admin status recognised");
    CHECK(!guards::isAdmin(bot.api, kChat, kUser), "plain member is not admin");
    bot.api.setStatus(kChat, kUser, "chatMemberStatusCreator");
    CHECK(guards::isAdmin(bot.api, kChat, kUser), "creator counts as admin");
    bot.api.setStatus(kChat, kUser, "chatMemberStatusMember");

    CHECK(!guards::canManageVc(bot.api, bot.db, kChat, kUser), "plain member cannot manage");
    bot.db.addAuth(kChat, kUser);
    CHECK(guards::canManageVc(bot.api, bot.db, kChat, kUser), "authorised user can manage");
    bot.db.removeAuth(kChat, kUser);
    bot.db.addSudo(kUser);
    CHECK(guards::canManageVc(bot.api, bot.db, kChat, kUser), "sudoer can manage");
    CHECK(guards::adminCheck(bot.api, bot.db, false, kChat, kUser), "sudoer passes adminCheck");
    bot.db.removeSudo(kUser);
    CHECK(!guards::adminCheck(bot.api, bot.db, false, kChat, kUser), "member fails adminCheck");
    CHECK(guards::adminCheck(bot.api, bot.db, true, kUser, kUser), "PM always passes adminCheck");

    CHECK(guards::resolveUrl({"play", "hello"}) == "", "no url in a plain query");
    CHECK(guards::resolveUrl({"play", "https://youtu.be/abc?si=xyz"}) ==
              "https://youtu.be/abc", "?si tracking suffix trimmed");
    CHECK(guards::resolveUrl({"play", "-f", "https://y/a&si=b"}) == "https://y/a",
          "&si trimmed, flag skipped");
    CHECK(guards::isFlag("-f") && guards::isFlag("-v"), "-f and -v are flags");
    CHECK(!guards::isFlag("song"), "a word is not a flag");

    guards::PlayRequest req;
    req.chatId       = kChat;
    req.fromUserId   = kAdmin;
    req.isSupergroup = true;
    req.command      = {"play", "never", "gonna"};
    auto run = [&](const guards::PlayRequest& r) {
        return guards::runPlayPreflight(bot.api, bot.db, bot.queue, bot.yt, bot.config, r);
    };

    auto ok = run(req);
    CHECK(ok.gate == guards::PlayGate::Proceed, "plain query proceeds");
    CHECK(ok.query == "never gonna", "query joined from the non-flag args");
    CHECK(!ok.force && !ok.video && !ok.m3u8, "no flags set");

    guards::PlayRequest anon = req;
    anon.fromUserId = 0;
    CHECK(run(anon).gate == guards::PlayGate::UserInvalid, "anonymous sender rejected");

    guards::PlayRequest basic = req;
    basic.isSupergroup = false;
    CHECK(run(basic).gate == guards::PlayGate::ChatInvalid, "non-supergroup rejected");

    guards::PlayRequest bare = req;
    bare.command = {"play"};
    CHECK(run(bare).gate == guards::PlayGate::Usage, "no arguments -> usage");
    bare.command = {"play", "-f"};
    CHECK(run(bare).gate == guards::PlayGate::Usage, "only -f -> usage");
    bare.command = {"play", "-v", "-f"};
    CHECK(run(bare).gate == guards::PlayGate::Usage, "only flags -> usage");

    guards::PlayRequest forced = req;
    forced.command    = {"playforce", "song"};
    forced.fromUserId = kUser;
    CHECK(run(forced).gate == guards::PlayGate::AdminOnly, "force needs permission");
    forced.fromUserId = kAdmin;
    auto forceOk = run(forced);
    CHECK(forceOk.gate == guards::PlayGate::Proceed && forceOk.force,
          "admin may force play");
    guards::PlayRequest flagForce = req;
    flagForce.command = {"play", "song", "-f"};
    CHECK(run(flagForce).force, "-f anywhere sets force");

    guards::PlayRequest video = req;
    video.command = {"vplay", "song"};
    CHECK(run(video).video, "vplay -> video");
    video.command = {"play", "song", "-v"};
    CHECK(run(video).video, "-v -> video");
    bot.config.video_play = false;
    CHECK(!run(video).video, "VIDEO_PLAY=false disables video");
    bot.config.video_play = true;

    guards::PlayRequest badLink = req;
    badLink.command = {"play", "https://www.youtube.com/channel/UCsomething"};
    CHECK(run(badLink).gate == guards::PlayGate::NotFound,
          "unhandled youtube link -> not found");

    guards::PlayRequest stream = req;
    stream.command = {"play", "https://cdn.example.com/live.m3u8"};
    auto streamPre = run(stream);
    CHECK(streamPre.gate == guards::PlayGate::Proceed && streamPre.m3u8,
          "direct link -> m3u8");

    guards::PlayRequest watch = req;
    watch.command = {"play", "https://www.youtube.com/watch?v=dQw4w9WgXcQ"};
    auto watchPre = run(watch);
    CHECK(watchPre.gate == guards::PlayGate::Proceed && !watchPre.m3u8,
          "valid youtube link is not m3u8");

    bot.db.setPlayMode(kChat, true);
    guards::PlayRequest modeReq = req;
    modeReq.fromUserId = kUser;
    CHECK(run(modeReq).gate == guards::PlayGate::AdminOnly, "play mode is admin only");
    bot.db.addAuth(kChat, kUser);
    CHECK(run(modeReq).gate == guards::PlayGate::Proceed, "authorised user passes play mode");
    bot.db.removeAuth(kChat, kUser);
    bot.db.setPlayMode(kChat, false);

    CHECK(!run(req).cmdDelete, "cmd delete off by default");
    bot.db.setCmdDelete(kChat, true);
    CHECK(run(req).cmdDelete, "cmd delete follows the setting");
    bot.db.setCmdDelete(kChat, false);

    for (int i = 0; i < bot.config.queue_limit; ++i)
        bot.queue.add(kChat, mk("q" + std::to_string(i)));
    CHECK(run(req).gate == guards::PlayGate::QueueFull, "queue limit enforced");
    bot.queue.clear(kChat);
}

void testPlay(const Language& lang) {

    {
        Bot bot(lang);
        bot.yt.nextSearch = mk("aaa");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "never", "gonna"}));

        CHECK(bot.yt.searchCalls == 1, "search ran once");
        CHECK(bot.yt.lastQuery == "never gonna", "search got the flag-free query");
        CHECK(bot.yt.downloadCalls == 1, "download ran once");
        CHECK(bot.tp.totalPlays == 1, "transport started one stream");
        CHECK(bot.cache.isActiveCall(kChat), "call registered as active");

        CHECK(bot.api.count("send") == 1, "exactly one message sent");
        CHECK(bot.api.count("edit") == 2, "two edits (downloading, card)");
        CHECK(bot.api.log[0].text == bot.L["play_searching"], "first text is 'searching'");
        CHECK(bot.api.log[1].text == bot.L["play_downloading"], "then 'downloading'");
        const std::int64_t id = bot.api.log[0].messageId;
        CHECK(bot.api.log[1].messageId == id && bot.api.log[2].messageId == id,
              "all three touch one message");

        const MediaItem card = *bot.queue.getCurrent(kChat);
        CHECK(bot.api.log[2].text ==
                  bot.L.fmt("play_media", card.url, card.title, card.duration, "@u7"),
              "card rendered from play_media with url/title/duration/mention");
        CHECK(card.message_id == id, "card id stored on the queued item");

        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 5, "five transport buttons");
        CHECK(data[0] == "controls resume " + std::to_string(kChat), "resume payload");
        CHECK(data[4] == "controls stop " + std::to_string(kChat), "stop payload");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.api.clear();
        bot.yt.nextSearch = mk("bbb");
        bot.plugins.onPlay(cmd(kChat, kUser, {"play", "second"}));

        CHECK(bot.queue.size(kChat) == 2, "second track queued");
        CHECK(bot.tp.totalPlays == 1, "nothing restarted");
        CHECK(bot.api.lastSaid() ==
                  bot.L.fmt("play_queued", 1, "https://youtu.be/bbb", "Song bbb", "3:20",
                            "@u8"),
              "queued card rendered with position 1");
        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 1 &&
                  data[0] == "controls force " + std::to_string(kChat) + " bbb",
              "queued card offers Play Now for that item");
    }

    {
        Bot bot(lang);
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play"}));
        CHECK(bot.api.lastSaid() == bot.L["play_usage"], "/play alone -> usage");
        CHECK(bot.yt.searchCalls == 0, "no search on a usage error");

        bot.api.clear();
        bot.plugins.onPlay(cmd(kChat, 0, {"play", "x"}));
        CHECK(bot.api.lastSaid() == bot.L["play_user_invalid"], "anonymous -> user invalid");

        bot.api.clear();
        bot.plugins.onPlay(cmd(kBasic, kAdmin, {"play", "x"}));
        CHECK(bot.api.said(bot.L["play_chat_invalid"]), "legacy group -> chat invalid");
        CHECK(bot.api.count("leave") == 1, "and the bot leaves");

        bot.api.clear();
        bot.db.setPlayMode(kChat, true);
        bot.plugins.onPlay(cmd(kChat, kUser, {"play", "x"}));
        CHECK(bot.api.lastSaid() == bot.L["play_admin"], "play mode -> admin only");
        bot.db.setPlayMode(kChat, false);

        bot.api.clear();
        for (int i = 0; i < bot.config.queue_limit; ++i)
            bot.queue.add(kChat, mk("q" + std::to_string(i)));
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "x"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_queue_full", bot.config.queue_limit),
              "full queue -> queue full");
        bot.queue.clear(kChat);

        bot.api.clear();
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "https://youtube.com/channel/UCx"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_not_found", bot.config.support_chat),
              "unhandled link -> not found");
    }

    {
        Bot bot(lang);
        bot.yt.nextSearch = std::nullopt;
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "nothing"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_not_found", bot.config.support_chat),
              "search miss -> not found");
        CHECK(bot.tp.totalPlays == 0, "nothing played");

        CHECK(bot.api.count("send") == 1 && bot.api.count("edit") == 1,
              "the status message became the error");

        bot.api.clear();
        bot.config.duration_limit_seconds = 600;
        bot.yt.nextSearch = mk("long", 4000);
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "long"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_duration_limit", 10),
              "over-long track rejected with the limit in minutes");
        CHECK(bot.tp.totalPlays == 0, "and it never plays");
    }

    {
        Bot bot(lang);
        bot.yt.nextSearch = mk("vid");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"vplay", "clip"}));
        CHECK(bot.yt.lastVideo, "vplay searched in video mode");
        CHECK(bot.tp.state(kChat).lastSource.video, "and streamed as video");
    }
    {
        Bot bot(lang);
        const std::string url = "https://cdn.example.com/live.m3u8";
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", url}));
        CHECK(bot.yt.searchCalls == 0 && bot.yt.downloadCalls == 0,
              "a direct link needs no search or download");
        CHECK(bot.tp.state(kChat).lastSource.path == url, "the url is streamed as-is");
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_media", url, url, "00:00", "@u7"),
              "the stream gets a now-playing card with a 00:00 duration");
    }
    {
        Bot bot(lang);
        bot.yt.nextPlaylist = {mk("p1"), mk("p2"), mk("p3")};
        bot.plugins.onPlay(
            cmd(kChat, kAdmin, {"play", "https://www.youtube.com/playlist?list=PLabc"}));
        CHECK(bot.yt.playlistCalls == 1, "playlist fetched");
        CHECK(bot.yt.lastLimit == bot.config.playlist_limit, "with PLAYLIST_LIMIT");
        CHECK(bot.queue.size(kChat) == 3, "all three tracks queued");
        CHECK(bot.tp.totalPlays == 1, "the first one starts");

        std::string expected = bot.L.fmt("playlist_queued", 3);
        for (int i = 1; i <= 3; ++i)
            expected += bot.L.fmt("queue_item", i, "Song p" + std::to_string(i), "3:20");
        CHECK(bot.api.lastSaid() == expected,
              "summary lists the count and every track, in order");

        Bot empty(lang);
        empty.yt.nextPlaylist.clear();
        empty.plugins.onPlay(
            cmd(kChat, kAdmin, {"play", "https://www.youtube.com/playlist?list=PLabc"}));
        CHECK(empty.api.lastSaid() == empty.L["playlist_error"], "empty playlist -> error");
        CHECK(empty.tp.totalPlays == 0, "and nothing plays");
    }
    {
        Bot bot(lang);
        bot.db.setCmdDelete(kChat, true);
        bot.yt.nextSearch = mk("del");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "x"}, 4242));
        const FakeBotApi::Record* del = bot.api.last("delete");
        CHECK(del && del->messageId == 4242, "the command message is deleted");
    }
}

void testAdminCommands(const Language& lang) {

    {
        Bot bot(lang);
        startPlaying(bot);
        bot.api.clear();
        bot.plugins.onPause(cmd(kChat, kUser, {"pause"}));
        CHECK(bot.api.lastSaid() == bot.L["user_not_admin"], "non-admin cannot pause");
        CHECK(bot.tp.state(kChat).pauseCount == 0, "and nothing is paused");

        Bot idle(lang);
        idle.plugins.onPause(cmd(kChat, kAdmin, {"pause"}));
        CHECK(idle.api.lastSaid() == idle.L["not_playing"], "pause with no stream");
        idle.api.clear();
        idle.plugins.onSkip(cmd(kChat, kAdmin, {"skip"}));
        CHECK(idle.api.lastSaid() == idle.L["not_playing"], "skip with no stream");
        idle.api.clear();
        idle.plugins.onQueue(cmd(kChat, kAdmin, {"queue"}));
        CHECK(idle.api.lastSaid() == idle.L["not_playing"], "queue with no stream");
    }

    {
        Bot bot(lang);
        startPlaying(bot);
        bot.api.clear();
        bot.plugins.onPause(cmd(kChat, kAdmin, {"pause"}));
        CHECK(bot.tp.state(kChat).pauseCount == 1, "transport paused");
        CHECK(!bot.cache.isPlaying(kChat), "cache says paused");
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_paused", "@u7"), "pause reply mentions user");

        bot.api.clear();
        bot.plugins.onPause(cmd(kChat, kAdmin, {"pause"}));
        CHECK(bot.api.lastSaid() == bot.L["play_already_paused"], "pausing twice is caught");
        CHECK(bot.tp.state(kChat).pauseCount == 1, "and does not touch the transport");

        bot.api.clear();
        bot.plugins.onResume(cmd(kChat, kAdmin, {"resume"}));
        CHECK(bot.tp.state(kChat).resumeCount == 1, "transport resumed");
        CHECK(bot.cache.isPlaying(kChat), "cache says playing");
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_resumed", "@u7"), "resume reply");

        bot.api.clear();
        bot.plugins.onResume(cmd(kChat, kAdmin, {"resume"}));
        CHECK(bot.api.lastSaid() == bot.L["play_not_paused"], "resuming twice is caught");
    }

    {
        Bot bot(lang);
        startPlaying(bot);
        bot.api.clear();
        bot.plugins.onStop(cmd(kChat, kAdmin, {"stop"}));
        CHECK(bot.tp.state(kChat).stopCount == 1, "transport stopped");
        CHECK(bot.queue.empty(kChat), "queue cleared");
        CHECK(!bot.cache.isActiveCall(kChat), "call deregistered");
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_stopped", "@u7"), "stop reply");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.yt.nextSearch = mk("bbb");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "bbb"}));
        bot.api.clear();
        bot.plugins.onSkip(cmd(kChat, kAdmin, {"skip"}));
        CHECK(bot.api.log[0].text == bot.L.fmt("play_skipped", "@u7"), "skip reply first");
        const MediaItem current = *bot.queue.getCurrent(kChat);
        CHECK(current.id == "bbb", "the queued track is now playing");
        CHECK(bot.queue.size(kChat) == 1, "and the finished one is gone");
        CHECK(bot.tp.totalPlays == 2, "transport started the next stream");
    }

    {
        Bot bot(lang);
        startPlaying(bot);
        bot.api.clear();
        bot.plugins.onLoop(cmd(kChat, kAdmin, {"loop"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("loop_count", 0), "bare /loop reports the count");
        bot.plugins.onLoop(cmd(kChat, kAdmin, {"loop", "3"}));
        CHECK(bot.cache.getLoop(kChat) == 3, "loop count set");
        CHECK(bot.api.lastSaid() == bot.L.fmt("loop_set", 3), "loop_set reply");
        bot.plugins.onLoop(cmd(kChat, kAdmin, {"loop", "OFF"}));
        CHECK(bot.cache.getLoop(kChat) == 0, "loop off (case-insensitive)");
        CHECK(bot.api.lastSaid() == bot.L["loop_off"], "loop_off reply");
        bot.plugins.onLoop(cmd(kChat, kAdmin, {"loop", "lots"}));
        CHECK(bot.api.lastSaid() == bot.L["loop_usage"], "garbage -> usage");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.yt.nextSearch = mk("bbb");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "bbb"}));
        bot.api.clear();
        bot.plugins.onQueue(cmd(kChat, kUser, {"queue"}));

        const std::string text = bot.api.lastSaid();
        const MediaItem current = *bot.queue.getCurrent(kChat);
        CHECK(text.find(bot.L.fmt("queue_curr", current.url, current.title,
                                  current.duration, current.user)) == 0,
              "queue card starts with the current track");
        CHECK(text.find(bot.L.fmt("queue_item", 1, "Song bbb", "3:20")) != std::string::npos,
              "and lists the queued one");
        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 1 &&
                  data[0] == "controls pause " + std::to_string(kChat) + " q",
              "queue card toggles pause and is marked with 'q'");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.api.clear();
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seek"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_seek_usage", "seek"), "bare /seek -> usage");
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seekback", "soon"}));
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_seek_usage", "seekback"),
              "usage names the command actually used");
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seek", "5"}));
        CHECK(bot.api.lastSaid() == bot.L["play_seek_min"], "under 10s rejected");
        CHECK(bot.tp.totalPlays == 1, "no reseek yet");

        bot.api.clear();
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seek", "30"}));
        CHECK(bot.tp.state(kChat).lastSource.seekSeconds == 31,
              "forward seek offsets from the current position");
        CHECK(bot.queue.getCurrent(kChat)->time == 31, "offset remembered on the item");
        CHECK(bot.api.lastSaid() == bot.L.fmt("play_seeked", bot.L["forward"], 31, "@u7"),
              "seek reply reports direction, offset and user");

        bot.api.clear();
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seekback", "11"}));
        CHECK(bot.tp.state(kChat).lastSource.seekSeconds == 20, "backward seek subtracts");
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seekback", "500"}));
        CHECK(bot.tp.state(kChat).lastSource.seekSeconds == 0, "clamped at the start");
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seek", "9999"}));
        CHECK(bot.tp.state(kChat).lastSource.seekSeconds == 190,
              "clamped 10s before the end");

        MediaItem live = *bot.queue.getCurrent(kChat);
        live.duration_sec = 0;
        bot.queue.replaceCurrent(kChat, live);
        bot.api.clear();
        bot.plugins.onSeek(cmd(kChat, kAdmin, {"seek", "30"}));
        CHECK(bot.api.lastSaid() == bot.L["play_seek_no_dur"], "no duration -> no seek");
    }
}

void testControls(const Language& lang) {
    const std::string cid = std::to_string(kChat);

    {
        Bot bot(lang);
        const std::int64_t card = startPlaying(bot);
        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kUser, "controls status " + cid, card));
        const FakeBotApi::Record* answer = bot.api.last("answer");
        CHECK(answer && answer->text == bot.L["playing"] && !answer->alert,
              "status button reports the live state to anyone");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kUser, "controls pause " + cid, card));
        answer = bot.api.last("answer");
        CHECK(answer && answer->text == bot.L["user_not_admin"] && answer->alert,
              "a non-admin gets an alert");
        CHECK(bot.tp.state(kChat).pauseCount == 0, "and nothing happens");
    }

    {
        Bot bot(lang);
        const std::int64_t card = startPlaying(bot);
        const std::string cardText = bot.api.lastSaid();
        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls pause " + cid, card));
        CHECK(bot.tp.state(kChat).pauseCount == 1, "button paused the stream");

        const FakeBotApi::Record* edit = bot.api.last("edit");
        CHECK(edit && edit->messageId == card, "the card itself was edited");
        CHECK(edit && edit->text == cardText, "its text is preserved");
        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 6, "a status row was added above the transport row");
        CHECK(data[0] == "controls status " + cid, "status row payload");
        const FakeBotApi::Record* answer = bot.api.last("answer");
        CHECK(answer && answer->text == bot.L["paused"], "toast shows the new state");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls pause " + cid, card));
        answer = bot.api.last("answer");
        CHECK(answer && answer->alert && answer->text == bot.L["play_already_paused"],
              "pausing an already paused stream is refused");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls resume " + cid, card));
        CHECK(bot.tp.state(kChat).resumeCount == 1, "button resumed the stream");
        CHECK(bot.cache.isPlaying(kChat), "and the cache agrees");
    }

    {
        Bot bot(lang);
        const std::int64_t card = startPlaying(bot);
        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls replay " + cid, card));
        CHECK(bot.tp.totalPlays == 2, "replay restarted the stream");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls stop " + cid, card));
        CHECK(bot.tp.state(kChat).stopCount == 1, "stop button stopped playback");
        CHECK(!bot.cache.isActiveCall(kChat), "call deregistered");
        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 1 && data[0] == "controls status " + cid,
              "a terminal action drops the transport row");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls pause " + cid, card));
        const FakeBotApi::Record* answer = bot.api.last("answer");
        CHECK(answer && answer->text == bot.L["not_playing"] && answer->alert,
              "buttons on a dead card report 'not playing'");
    }
    {
        Bot bot(lang);
        const std::int64_t card = startPlaying(bot, "aaa");
        bot.yt.nextSearch = mk("bbb");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "bbb"}));
        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls skip " + cid, card));
        CHECK(bot.queue.getCurrent(kChat)->id == "bbb", "skip button advanced the queue");
        CHECK(answerText(bot) == bot.L["skipped"], "toast says skipped");
    }

    {
        Bot bot(lang);
        startPlaying(bot);
        bot.plugins.onQueue(cmd(kChat, kAdmin, {"queue"}));
        const std::int64_t queueCard = bot.api.last()->messageId;
        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls pause " + cid + " q", queueCard));
        CHECK(bot.api.count("edit") == 0, "the queue card's text is untouched");
        const FakeBotApi::Record* markup = bot.api.last("markup");
        CHECK(markup && markup->messageId == queueCard, "only its markup changed");
        const std::vector<std::string> data = bot.api.lastKeyboardData();
        CHECK(data.size() == 1 && data[0] == "controls resume " + cid + " q",
              "the toggle now offers resume");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        for (const char* id : {"bbb", "ccc"}) {
            bot.yt.nextSearch = mk(id);
            bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", id}));
        }
        CHECK(bot.queue.size(kChat) == 3, "three tracks queued");
        const std::int64_t queuedCard = bot.api.last()->messageId;
        bot.api.clear();

        bot.plugins.onControls(btn(kChat, kAdmin, "controls force " + cid + " ccc",
                                   queuedCard));
        CHECK(bot.queue.getCurrent(kChat)->id == "ccc", "the chosen track plays now");
        CHECK(bot.queue.size(kChat) == 2, "its old queue entry was removed");
        const std::vector<MediaItem> rest = bot.queue.getQueue(kChat);
        CHECK(rest.size() == 2 && rest[1].id == "bbb", "the untouched track stays queued");
        CHECK(bot.tp.totalPlays == 2, "the transport switched streams");
        const FakeBotApi::Record* markup = bot.api.last("markup");
        CHECK(markup && markup->kb.empty(), "the spent Play Now button is cleared");
        CHECK(answerText(bot) == bot.L["play_now"], "and the press is answered");

        bot.api.clear();
        bot.plugins.onControls(btn(kChat, kAdmin, "controls force " + cid + " zzz",
                                   queuedCard));
        const FakeBotApi::Record* answer = bot.api.last("answer");
        CHECK(answer && answer->alert && answer->text == bot.L["play_expired"],
              "an unknown item id is an expired button");
    }

    {
        Bot bot(lang);
        const std::int64_t card = startPlaying(bot);
        for (const std::string& data : {std::string("controls"),
                                        std::string("controls pause"),
                                        std::string("controls pause notanid"),
                                        std::string("controls frobnicate " + cid)}) {
            bot.api.clear();
            bot.plugins.onControls(btn(kChat, kAdmin, data, card));
            CHECK(answerWasAlert(bot) && answerText(bot) == bot.L["play_expired"],
                  "malformed payload -> expired: " + data);
        }
    }
}

void testNotices(const Language& lang) {

    {
        Bot bot(lang);
        bot.yt.nextSearch = mk("aaa");
        bot.yt.downloadFails = true;
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "x"}));
        CHECK(bot.tp.totalPlays == 0, "nothing streamed");
        CHECK(bot.api.said(bot.L.fmt("error_no_file", bot.config.support_chat)),
              "download failure surfaced with the support link");
    }

    {
        Bot bot(lang);
        bot.tp.playResult = PlayResult::NoActiveGroupCall;
        bot.yt.nextSearch = mk("aaa");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "x"}));
        CHECK(bot.api.said(bot.L["error_no_call"]), "missing voice chat reported");
        CHECK(!bot.cache.isActiveCall(kChat), "and the chat is not marked active");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.yt.nextSearch = mk("bbb");
        bot.plugins.onPlay(cmd(kChat, kAdmin, {"play", "bbb"}));
        bot.api.clear();

        bot.tp.fireStreamEnd(kChat);
        CHECK(bot.queue.getCurrent(kChat)->id == "bbb", "stream end advanced the queue");
        CHECK(bot.api.log[0].op == "send" && bot.api.log[0].text == bot.L["play_next"],
              "a 'downloading next' notice is posted");
        const std::int64_t noticeId = bot.api.log[0].messageId;
        const FakeBotApi::Record* card = bot.api.last("edit");
        CHECK(card && card->messageId == noticeId,
              "and that very message becomes the new card");
        CHECK(bot.queue.getCurrent(kChat)->message_id == noticeId,
              "the new card id is recorded on the item");
    }

    {
        Bot bot(lang);
        startPlaying(bot, "aaa");
        bot.cache.setLoop(kChat, 2);
        bot.api.clear();
        bot.tp.fireStreamEnd(kChat);
        CHECK(bot.cache.getLoop(kChat) == 1, "loop counter decremented");
        CHECK(bot.queue.getCurrent(kChat)->id == "aaa", "same track still playing");
        CHECK(bot.api.said(bot.L["play_again"]), "replay announced");
        CHECK(bot.tp.totalPlays == 2, "and the transport restarted it");
    }
}

}

int main(int argc, char** argv) {
    Language lang;
    loadLanguages(lang, argc > 1 ? argv[1] : nullptr);
    lang.setDefault("en");

    const LangView L = lang.view("en");
    CHECK(L["play_media"].find("Started streaming") != std::string::npos,
          "the language table really is loaded");

    testGuards(lang);
    testPlay(lang);
    testAdminCommands(lang);
    testControls(lang);
    testNotices(lang);

    std::cout << "checks run: " << g_checks << ", failures: " << g_failures << "\n";
    if (g_failures != 0) {
        std::cerr << "PLUGIN TESTS FAILED\n";
        return 1;
    }
    std::cout << "ALL PLUGIN TESTS PASSED\n";
    return 0;
}
