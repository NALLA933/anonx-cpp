// AnonXMusic C++ port — Phase 6a (command plugins)
// plugins.hpp — the playback commands and the inline "controls" router.
//
// WHAT THIS PORTS
//   anony/plugins/play/play.py        -> onPlay        (/play /vplay /playforce /vplayforce)
//   anony/plugins/admins/pause.py     -> onPause       (/pause)
//   anony/plugins/admins/resume.py    -> onResume      (/resume)
//   anony/plugins/admins/skip.py      -> onSkip        (/skip)
//   anony/plugins/admins/stop.py      -> onStop        (/stop /end)
//   anony/plugins/admins/loop.py      -> onLoop        (/loop [count|off])
//   anony/plugins/admins/seek.py      -> onSeek        (/seek /seekback <sec>)
//   anony/plugins/tools/queue.py      -> onQueue       (/queue)
//   anony/plugins/bot/inline.py       -> onControls    (the "controls …" buttons)
//
// DESIGN — WHY THE HANDLERS TAKE PLAIN STRUCTS
// A handler never sees a Dispatcher context. It receives a CommandEvent /
// ButtonEvent (ids + tokens) and talks to Telegram only through BotApi. That
// keeps this translation unit free of every transport header, so the whole
// command layer links and runs against a FakeBotApi + FakeVoiceTransport with no
// TDLib and no network — the same trick Phase 5 used for the voice layer.
// The Dispatcher wiring (filters, registration, MessageContext -> CommandEvent)
// lives in the separate plugins_router.hpp/.cpp so that the dependency stays out
// of this file's link closure.
//
// THE STATUS-MESSAGE HANDOFF
// The Python bot sends one message ("Searching…"), then edits it in place as the
// request progresses, and finally turns it into the "now playing" card. The card
// is rendered from deep inside CallManager (via the onNowPlaying callback), which
// knows nothing about the command that started it. Plugins bridges the two with
// a small per-chat "pending status message" slot: the command stashes its status
// message id, and whichever callback fires next TAKES the slot and edits that
// message instead of sending a new one. See takeStatus()/setStatus().
//
// NOT IN 6a (each is a documented, deliberate gap)
//   * replied-media play — MessageContext carries no reply/entity data yet, so
//     PlayRequest::hasReply is always false;
//   * live download progress and the "cancel_dl" button (Phase 7);
//   * the per-request play log — the `play_log` template needs a message link,
//     so it waits on the same reply/entity data as replied-media play. (The
//     served-chat/served-user registries it used to be grouped with are done:
//     the Phase 6b chat watcher, AdminPlugins::onSeen, fills them.)
//   * /shuffle and /remove (no strings for them exist in the locale files).

#ifndef ANONX_PLUGINS_HPP
#define ANONX_PLUGINS_HPP

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "anonx/bot_api.hpp"
#include "anonx/cache_manager.hpp"
#include "anonx/call_manager.hpp"
#include "anonx/config.hpp"
#include "anonx/database.hpp"
#include "anonx/lang.hpp"
#include "anonx/queue.hpp"
#include "anonx/youtube.hpp"

namespace anonx {

// A command message, reduced to what the handlers need.
struct CommandEvent {
    std::int64_t chatId = 0;
    std::int64_t messageId = 0;
    std::int64_t fromUserId = 0;            // 0 == anonymous admin / channel post
    bool         isPrivate = false;
    std::vector<std::string> command;       // [name, arg1, …], name without prefix

    // Message this command replies to (same chat), else 0. Admin commands take
    // their target this way: "/auth" on a member's message authorizes them.
    std::int64_t replyToMessageId = 0;

    bool hasReply() const { return replyToMessageId != 0; }
};

// An inline-button press, reduced likewise.
struct ButtonEvent {
    std::int64_t chatId = 0;                // chat the card lives in
    std::int64_t messageId = 0;             // the card
    std::int64_t fromUserId = 0;
    std::int64_t queryId = 0;               // for answerCallback
    std::string  data;                      // "controls <action> <chatId> [extra]"
};

// The playback command set. One instance owns no state beyond the pending
// status-message slots; every collaborator is injected by reference and must
// outlive it (in particular CallManager, which holds callbacks bound to `this`).
class Plugins {
public:
    struct Deps {
        BotApi&         api;
        Database&       db;
        CacheManager&   cache;
        Queue&          queue;
        YouTube&        yt;
        CallManager&    calls;
        const Language& lang;
        const Config&   config;
    };

    explicit Plugins(const Deps& deps);

    // Hand CallManager the callbacks that render cards and notices. install()
    // (plugins_router.cpp) does this too; call it directly when wiring by hand.
    void attachCallbacks();

    // The CallManager::Callbacks bundle — download, card rendering, notices,
    // stale-card deletion. Exposed so tests can drive the engine alone.
    CallManager::Callbacks callbacks();

    // ---- command handlers -------------------------------------------------
    void onPlay(const CommandEvent& ev);     // /play /vplay /playforce /vplayforce
    void onSkip(const CommandEvent& ev);     // /skip
    void onPause(const CommandEvent& ev);    // /pause
    void onResume(const CommandEvent& ev);   // /resume
    void onStop(const CommandEvent& ev);     // /stop /end
    void onLoop(const CommandEvent& ev);     // /loop [count|off]
    void onQueue(const CommandEvent& ev);    // /queue
    void onSeek(const CommandEvent& ev);     // /seek /seekback <seconds>

    // ---- inline keyboard --------------------------------------------------
    void onControls(const ButtonEvent& ev);  // "controls <action> <chatId> [extra]"

    // The command names each handler answers to (also used by the router's
    // filters, and by tests, so the two can never drift apart).
    static std::vector<std::string> playCommands();
    static std::vector<std::string> skipCommands();
    static std::vector<std::string> pauseCommands();
    static std::vector<std::string> resumeCommands();
    static std::vector<std::string> stopCommands();
    static std::vector<std::string> loopCommands();
    static std::vector<std::string> queueCommands();
    static std::vector<std::string> seekCommands();

    // Telegram's own rule: only supergroups can host a voice chat, and their ids
    // are always below -1000000000000. Ports the ChatType.SUPERGROUP test.
    static bool isSupergroupId(std::int64_t chatId);

    // Escape text that goes into an HTML message body (titles can contain "&").
    static std::string htmlEscape(const std::string& text);

private:
    LangView tr(std::int64_t chatId) const;

    // Pending-status-message slot (see the handoff note above).
    void         setStatus(std::int64_t chatId, std::int64_t messageId);
    std::int64_t takeStatus(std::int64_t chatId);

    // Send `html`, or edit the pending status message when there is one.
    std::int64_t say(std::int64_t chatId, const std::string& html,
                     const InlineKeyboard& kb = {});

    // Shared preamble for the admin-only playback commands: resolves permission
    // and an active call, replying with the right string and returning false
    // when the command must stop.
    bool requireControl(const CommandEvent& ev, const LangView& L);

    // Cards.
    std::string nowPlayingCard(const LangView& L, const MediaItem& item) const;
    std::string queuedCard(const LangView& L, const MediaItem& item, int position) const;

    // CallManager callback bodies.
    std::int64_t renderNowPlaying(std::int64_t chatId, const MediaItem& item);
    void         renderNotice(std::int64_t chatId, CallManager::Notice notice);

    BotApi&         api_;
    Database&       db_;
    CacheManager&   cache_;
    Queue&          queue_;
    YouTube&        yt_;
    CallManager&    calls_;
    const Language& lang_;
    const Config&   config_;

    mutable std::mutex                    mutex_;
    std::map<std::int64_t, std::int64_t>  status_;  // chatId -> pending message id
};

}  // namespace anonx

#endif  // ANONX_PLUGINS_HPP
