#include "anonx/plugins.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <utility>

#include "anonx/buttons.hpp"
#include "anonx/guards.hpp"

namespace anonx {
namespace {

std::vector<std::string> splitWs(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok)
        out.push_back(tok);
    return out;
}

bool parseU32(const std::string& text, long& out) {
    if (text.empty() || text.size() > 9)
        return false;
    long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool parseI64(const std::string& text, std::int64_t& out) {
    if (text.empty() || text.size() > 20)
        return false;
    std::size_t i = 0;
    bool negative = false;
    if (text[0] == '-' || text[0] == '+') {
        negative = text[0] == '-';
        i = 1;
        if (text.size() == 1)
            return false;
    }
    std::int64_t value = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        value = value * 10 + (text[i] - '0');
    }
    out = negative ? -value : value;
    return true;
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool isPlaylistUrl(const std::string& url) {
    return url.find("list=") != std::string::npos ||
           url.find("/playlist") != std::string::npos;
}

constexpr int kMinSeekSeconds = 10;

constexpr int kSeekTailGuard = 10;

}

Plugins::Plugins(const Deps& deps)
    : api_(deps.api), db_(deps.db), cache_(deps.cache), queue_(deps.queue),
      yt_(deps.yt), calls_(deps.calls), lang_(deps.lang), config_(deps.config) {}

bool Plugins::isSupergroupId(std::int64_t chatId) {
    return chatId <= -1000000000000LL;
}

std::string Plugins::htmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            default:  out.push_back(c);
        }
    }
    return out;
}

LangView Plugins::tr(std::int64_t chatId) const {
    return lang_.view(db_.getLang(chatId));
}

void Plugins::setStatus(std::int64_t chatId, std::int64_t messageId) {
    if (messageId == 0)
        return;
    std::lock_guard<std::mutex> lk(mutex_);
    status_[chatId] = messageId;
}

std::int64_t Plugins::takeStatus(std::int64_t chatId) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = status_.find(chatId);
    if (it == status_.end())
        return 0;
    const std::int64_t id = it->second;
    status_.erase(it);
    return id;
}

std::int64_t Plugins::say(std::int64_t chatId, const std::string& html,
                          const InlineKeyboard& kb) {
    const std::int64_t pending = takeStatus(chatId);
    if (pending != 0 && api_.editMessageText(chatId, pending, html, kb))
        return pending;
    return api_.sendMessage(chatId, html, kb);
}

std::string Plugins::nowPlayingCard(const LangView& L, const MediaItem& item) const {
    return L.fmt("play_media", item.url, htmlEscape(item.title), item.duration,
                 item.user);
}

std::string Plugins::queuedCard(const LangView& L, const MediaItem& item,
                                int position) const {
    return L.fmt("play_queued", position, item.url, htmlEscape(item.title),
                 item.duration, item.user);
}

std::int64_t Plugins::renderNowPlaying(std::int64_t chatId, const MediaItem& item) {
    const LangView L = tr(chatId);

    return say(chatId, nowPlayingCard(L, item), buttons::controls(chatId));
}

void Plugins::renderNotice(std::int64_t chatId, CallManager::Notice notice) {
    const LangView L = tr(chatId);
    std::string text;
    bool becomesCard = false;

    switch (notice) {
        case CallManager::Notice::PlayAgain:
            text = L["play_again"];
            becomesCard = true;
            break;
        case CallManager::Notice::PlayNext:
            text = L["play_next"];
            becomesCard = true;
            break;
        case CallManager::Notice::ErrorNoFile:
            text = L.fmt("error_no_file", config_.support_chat);
            break;
        case CallManager::Notice::ErrorNoCall:
            text = L["error_no_call"];
            break;
        case CallManager::Notice::ErrorNoAudio:
            text = L["error_no_audio"];
            break;
        case CallManager::Notice::ErrorServer:
            text = L["error_tg_server"];
            break;
        case CallManager::Notice::ErrorRtmp:
            text = L["error_rtmp"];
            break;
    }

    const std::int64_t id = say(chatId, text);
    if (becomesCard)
        setStatus(chatId, id);
}

CallManager::Callbacks Plugins::callbacks() {
    CallManager::Callbacks cb;
    cb.download = [this](const std::string& videoId, bool video) {
        return yt_.download(videoId, video);
    };
    cb.onNowPlaying = [this](std::int64_t chatId, const MediaItem& item) {
        return renderNowPlaying(chatId, item);
    };
    cb.onNotice = [this](std::int64_t chatId, CallManager::Notice notice) {
        renderNotice(chatId, notice);
    };
    cb.onDeleteMessage = [this](std::int64_t chatId, std::int64_t messageId) {
        api_.deleteMessage(chatId, messageId);
    };
    return cb;
}

void Plugins::attachCallbacks() { calls_.setCallbacks(callbacks()); }

std::vector<std::string> Plugins::playCommands() {

    return {"play", "vplay", "playforce", "vplayforce"};
}
std::vector<std::string> Plugins::skipCommands()   { return {"skip"}; }
std::vector<std::string> Plugins::pauseCommands()  { return {"pause"}; }
std::vector<std::string> Plugins::resumeCommands() { return {"resume"}; }
std::vector<std::string> Plugins::stopCommands()   { return {"stop", "end"}; }
std::vector<std::string> Plugins::loopCommands()   { return {"loop"}; }
std::vector<std::string> Plugins::queueCommands()  { return {"queue"}; }
std::vector<std::string> Plugins::seekCommands()   { return {"seek", "seekback"}; }

void Plugins::onPlay(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);

    guards::PlayRequest req;
    req.chatId       = ev.chatId;
    req.fromUserId   = ev.fromUserId;
    req.isSupergroup = !ev.isPrivate && isSupergroupId(ev.chatId);
    req.hasReply     = false;
    req.command      = ev.command;

    const guards::PlayPreflight pre =
        guards::runPlayPreflight(api_, db_, queue_, yt_, config_, req);

    switch (pre.gate) {
        case guards::PlayGate::UserInvalid:
            api_.sendMessage(ev.chatId, L["play_user_invalid"]);
            return;
        case guards::PlayGate::ChatInvalid:
            api_.sendMessage(ev.chatId, L["play_chat_invalid"]);
            api_.leaveChat(ev.chatId);
            return;
        case guards::PlayGate::Usage:
            api_.sendMessage(ev.chatId, L["play_usage"]);
            return;
        case guards::PlayGate::QueueFull:
            api_.sendMessage(ev.chatId, L.fmt("play_queue_full", config_.queue_limit));
            return;
        case guards::PlayGate::NotFound:
            api_.sendMessage(ev.chatId, L.fmt("play_not_found", config_.support_chat));
            return;
        case guards::PlayGate::AdminOnly:
            api_.sendMessage(ev.chatId, L["play_admin"]);
            return;
        case guards::PlayGate::Proceed:
            break;
    }

    const std::string mention = api_.userMention(ev.fromUserId);

    setStatus(ev.chatId, api_.sendMessage(ev.chatId, L["play_searching"]));

    if (pre.m3u8) {

        MediaItem item;
        item.id        = pre.url;
        item.title     = pre.url;
        item.url       = pre.url;
        item.file_path = pre.url;
        item.duration  = "00:00";
        item.video     = pre.video;
        item.user      = mention;

        const CallManager::PlayDecision decision =
            calls_.play(ev.chatId, item, pre.force);
        if (decision.outcome == CallManager::PlayOutcome::Queued) {
            say(ev.chatId, queuedCard(L, item, decision.position),
                buttons::playQueued(ev.chatId, item.id, L["play_now"]));
        }
    } else if (!pre.url.empty() && isPlaylistUrl(pre.url)) {

        setStatus(ev.chatId, say(ev.chatId, L["playlist_fetch"]));

        const std::vector<MediaItem> tracks =
            yt_.playlist(pre.url, config_.playlist_limit, mention, pre.video);
        if (tracks.empty()) {
            say(ev.chatId, L["playlist_error"]);
            return;
        }

        for (const MediaItem& track : tracks)
            calls_.play(ev.chatId, track, false);

        std::string summary = L.fmt("playlist_queued", tracks.size());
        int index = 1;
        for (const MediaItem& track : tracks) {
            summary += L.fmt("queue_item", index++, htmlEscape(track.title),
                             track.duration);
        }

        say(ev.chatId, summary);
    } else {
        const std::string query = pre.url.empty() ? pre.query : pre.url;
        std::optional<MediaItem> track = yt_.search(query, ev.messageId, pre.video);
        if (!track) {
            say(ev.chatId, L.fmt("play_not_found", config_.support_chat));
            return;
        }
        if (config_.duration_limit_seconds > 0 &&
            track->duration_sec > config_.duration_limit_seconds) {
            say(ev.chatId, L.fmt("play_duration_limit",
                                 config_.duration_limit_seconds / 60));
            return;
        }

        track->user  = mention;
        track->video = pre.video;

        setStatus(ev.chatId, say(ev.chatId, L["play_downloading"]));

        const CallManager::PlayDecision decision =
            calls_.play(ev.chatId, *track, pre.force);
        if (decision.outcome == CallManager::PlayOutcome::Queued) {
            say(ev.chatId, queuedCard(L, *track, decision.position),
                buttons::playQueued(ev.chatId, track->id, L["play_now"]));
        }
    }

    if (pre.cmdDelete)
        api_.deleteMessage(ev.chatId, ev.messageId);
}

bool Plugins::requireControl(const CommandEvent& ev, const LangView& L) {
    if (!guards::canManageVc(api_, db_, ev.chatId, ev.fromUserId)) {
        api_.sendMessage(ev.chatId, L["user_not_admin"]);
        return false;
    }
    if (!cache_.isActiveCall(ev.chatId)) {
        api_.sendMessage(ev.chatId, L["not_playing"]);
        return false;
    }
    return true;
}

void Plugins::onSkip(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    if (!requireControl(ev, L))
        return;

    api_.sendMessage(ev.chatId, L.fmt("play_skipped", api_.userMention(ev.fromUserId)));

    calls_.playNext(ev.chatId);
}

void Plugins::onPause(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    if (!requireControl(ev, L))
        return;
    if (!cache_.isPlaying(ev.chatId)) {
        api_.sendMessage(ev.chatId, L["play_already_paused"]);
        return;
    }
    if (!calls_.pause(ev.chatId)) {

        api_.sendMessage(ev.chatId, L["error_no_call"]);
        return;
    }
    api_.sendMessage(ev.chatId, L.fmt("play_paused", api_.userMention(ev.fromUserId)));
}

void Plugins::onResume(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    if (!requireControl(ev, L))
        return;
    if (cache_.isPlaying(ev.chatId)) {
        api_.sendMessage(ev.chatId, L["play_not_paused"]);
        return;
    }
    if (!calls_.resume(ev.chatId)) {
        api_.sendMessage(ev.chatId, L["error_no_call"]);
        return;
    }
    api_.sendMessage(ev.chatId, L.fmt("play_resumed", api_.userMention(ev.fromUserId)));
}

void Plugins::onStop(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    if (!requireControl(ev, L))
        return;
    calls_.stop(ev.chatId);
    api_.sendMessage(ev.chatId, L.fmt("play_stopped", api_.userMention(ev.fromUserId)));
}

void Plugins::onLoop(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    if (!requireControl(ev, L))
        return;

    if (ev.command.size() < 2) {
        api_.sendMessage(ev.chatId, L.fmt("loop_count", cache_.getLoop(ev.chatId)));
        return;
    }

    const std::string arg = toLower(ev.command[1]);
    if (arg == "off" || arg == "disable" || arg == "0") {
        cache_.setLoop(ev.chatId, 0);
        api_.sendMessage(ev.chatId, L["loop_off"]);
        return;
    }

    long count = 0;
    if (!parseU32(arg, count)) {
        api_.sendMessage(ev.chatId, L["loop_usage"]);
        return;
    }
    cache_.setLoop(ev.chatId, static_cast<int>(count));
    api_.sendMessage(ev.chatId, L.fmt("loop_set", count));
}

void Plugins::onQueue(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);

    if (!cache_.isActiveCall(ev.chatId) || queue_.empty(ev.chatId)) {
        api_.sendMessage(ev.chatId, L["not_playing"]);
        return;
    }

    setStatus(ev.chatId, api_.sendMessage(ev.chatId, L["queue_fetching"]));

    const std::vector<MediaItem> items = queue_.getQueue(ev.chatId);
    std::string text = L.fmt("queue_curr", items[0].url, htmlEscape(items[0].title),
                             items[0].duration, items[0].user);
    for (std::size_t i = 1; i < items.size(); ++i) {
        text += L.fmt("queue_item", i, htmlEscape(items[i].title), items[i].duration);
    }

    const bool playing = cache_.isPlaying(ev.chatId);
    say(ev.chatId, text,
        buttons::queueMarkup(ev.chatId, playing ? L["playing"] : L["paused"], playing));
}

void Plugins::onSeek(const CommandEvent& ev) {
    const LangView L = tr(ev.chatId);
    const std::string name = ev.command.empty() ? std::string("seek") : ev.command[0];
    const bool backward = name == "seekback";

    if (!requireControl(ev, L))
        return;

    long requested = 0;
    if (ev.command.size() < 2 || !parseU32(ev.command[1], requested)) {
        api_.sendMessage(ev.chatId, L.fmt("play_seek_usage", name));
        return;
    }
    if (requested < kMinSeekSeconds) {
        api_.sendMessage(ev.chatId, L["play_seek_min"]);
        return;
    }

    std::optional<MediaItem> current = queue_.getCurrent(ev.chatId);
    if (!current) {
        api_.sendMessage(ev.chatId, L["not_playing"]);
        return;
    }
    if (current->duration_sec <= 0) {

        api_.sendMessage(ev.chatId, L["play_seek_no_dur"]);
        return;
    }

    const long long from = current->time > 0 ? current->time : 0;
    long long target = backward ? from - requested : from + requested;
    const long long last = current->duration_sec > kSeekTailGuard
                               ? current->duration_sec - kSeekTailGuard
                               : 0;
    target = std::max<long long>(0, std::min<long long>(target, last));

    setStatus(ev.chatId, api_.sendMessage(ev.chatId, L["play_seeking"]));

    current->time = static_cast<int>(target);
    queue_.replaceCurrent(ev.chatId, *current);

    calls_.playMedia(ev.chatId, *current, static_cast<int>(target));

    say(ev.chatId, L.fmt("play_seeked", backward ? L["backward"] : L["forward"],
                         target, api_.userMention(ev.fromUserId)));
}

void Plugins::onControls(const ButtonEvent& ev) {

    const std::vector<std::string> parts = splitWs(ev.data);
    std::int64_t chatId = 0;
    if (parts.size() < 3 || parts[0] != "controls" || !parseI64(parts[2], chatId)) {
        api_.answerCallback(ev.queryId, lang_.view(config_.lang_code)["play_expired"],
                            true);
        return;
    }

    const std::string& action = parts[1];
    const std::string  extra  = parts.size() > 3 ? parts[3] : std::string();
    const bool queueCard = extra == "q";
    const LangView L = tr(chatId);

    if (action == "status") {
        api_.answerCallback(ev.queryId,
                            cache_.isPlaying(chatId) ? L["playing"] : L["paused"],
                            false);
        return;
    }

    if (!guards::canManageVc(api_, db_, chatId, ev.fromUserId)) {
        api_.answerCallback(ev.queryId, L["user_not_admin"], true);
        return;
    }

    if (action == "force") {

        const std::pair<int, std::optional<MediaItem>> found =
            queue_.checkItem(chatId, extra);
        if (found.first < 0 || !found.second) {
            api_.answerCallback(ev.queryId, L["play_expired"], true);
            return;
        }

        if (found.first > 0)
            queue_.forceAdd(chatId, *found.second, found.first);
        calls_.play(chatId, *found.second, true);

        api_.editMessageReplyMarkup(chatId, ev.messageId, {});
        api_.answerCallback(ev.queryId, L["play_now"], false);
        return;
    }

    if (!cache_.isActiveCall(chatId)) {
        api_.answerCallback(ev.queryId, L["not_playing"], true);
        return;
    }

    std::string status;
    bool removeTransport = false;

    if (action == "pause") {
        if (!cache_.isPlaying(chatId)) {
            api_.answerCallback(ev.queryId, L["play_already_paused"], true);
            return;
        }
        calls_.pause(chatId);
        status = L["paused"];
    } else if (action == "resume") {
        if (cache_.isPlaying(chatId)) {
            api_.answerCallback(ev.queryId, L["play_not_paused"], true);
            return;
        }
        calls_.resume(chatId);
        status = L["playing"];
    } else if (action == "replay") {
        calls_.replay(chatId);
        status = L["replayed"];
    } else if (action == "skip") {
        calls_.playNext(chatId);
        status = L["skipped"];
        removeTransport = true;
    } else if (action == "stop") {
        calls_.stop(chatId);
        status = L["stopped"];
        removeTransport = true;
    } else {
        api_.answerCallback(ev.queryId, L["play_expired"], true);
        return;
    }

    if (queueCard) {

        api_.editMessageReplyMarkup(
            chatId, ev.messageId,
            buttons::queueMarkup(chatId, status, cache_.isPlaying(chatId)));
    } else {

        const std::string text = api_.getMessageText(chatId, ev.messageId);
        const InlineKeyboard kb = buttons::controls(chatId, status, "", removeTransport);
        if (text.empty())
            api_.editMessageReplyMarkup(chatId, ev.messageId, kb);
        else
            api_.editMessageText(chatId, ev.messageId, text, kb);
    }

    api_.answerCallback(ev.queryId, status, false);
}

}
