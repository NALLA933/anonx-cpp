#include <anonx/telegram/dispatcher.hpp>
#include <anonx/audio/audio_streamer.hpp>
#include <anonx/audio/ntgcalls_client.hpp>
#include <anonx/database/mongo_client.hpp>
#include <anonx/core/logger.hpp>
#include <chrono>
#include <sstream>
#include <string_view>


namespace anonx::telegram {

namespace {

int64_t get_current_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::pair<std::string_view, std::string_view> parse_command_and_args(std::string_view text) noexcept {
    if (text.empty() || text.front() != '/') return {"", ""};
    text.remove_prefix(1); // remove '/'
    size_t space_pos = text.find(' ');
    std::string_view cmd = (space_pos != std::string_view::npos) ? text.substr(0, space_pos) : text;
    size_t at_pos = cmd.find('@');
    if (at_pos != std::string_view::npos) {
        cmd = cmd.substr(0, at_pos);
    }
    std::string_view args;
    if (space_pos != std::string_view::npos) {
        args = text.substr(space_pos + 1);
        size_t first_non_space = args.find_first_not_of(' ');
        if (first_non_space != std::string_view::npos) {
            args.remove_prefix(first_non_space);
        } else {
            args = {};
        }
    }
    return {cmd, args};
}

} // namespace

CommandDispatcher::CommandDispatcher() = default;

CommandDispatcher& CommandDispatcher::instance() {
    static CommandDispatcher dispatcher;
    return dispatcher;
}

void CommandDispatcher::init(const core::BotConfig& config, std::shared_ptr<TDLibClient> client) {
    config_ = config;
    client_ = client;
    start_time_ = get_current_epoch_ms();
    thread_pool_ = std::make_unique<core::ThreadPool>();
    rate_limiter_ = std::make_unique<core::RateLimiter>(3.0, 6.0, 64);

    // Register update listener
    if (client_) {
        client_->add_update_listener("updateNewMessage", [this](const nlohmann::json& update) {
            if (update.contains("message")) {
                this->dispatch_message(update["message"]);
            }
        });
    }

    // Hook track end callback to auto-play next track in queue asynchronously
    audio::AudioStreamer::instance().set_on_track_ended([this](int64_t chat_id, const database::TrackItem& track) {
        ANONX_LOG_INFO("Dispatcher", "Auto-advancing track in chat: ", chat_id, " (finished: ", track.title, ")");
        if (this->thread_pool_) {
            this->thread_pool_->enqueue([this, chat_id]() {
                this->play_next_in_queue(chat_id);
            });
        } else {
            this->play_next_in_queue(chat_id);
        }
    });

    ANONX_LOG_INFO("Dispatcher", "Command Dispatcher initialized with async ThreadPool (workers: ",
                   thread_pool_->worker_count(), ") and RateLimiter.");
}

void CommandDispatcher::dispatch_message(const nlohmann::json& message) {
    if (!message.contains("content") || !message.contains("chat_id")) return;

    int64_t chat_id = message["chat_id"].get<int64_t>();
    int64_t msg_id = message.value("id", int64_t{0});

    int64_t sender_user_id = 0;
    if (message.contains("sender_id") && message["sender_id"].value("@type", "") == "messageSenderUser") {
        sender_user_id = message["sender_id"].value("user_id", int64_t{0});
    }

    // Check blacklist / whitelist
    if (database::MongoClient::instance().is_chat_blocked(chat_id)) {
        return;
    }

    const auto& content = message["content"];
    std::string text;
    if (content.value("@type", "") == "messageText" && content.contains("text")) {
        text = content["text"].value("text", "");
    }

    if (text.empty() || text[0] != '/') {
        return;
    }

    auto [cmd, args] = parse_command_and_args(text);
    if (cmd.empty()) return;

    // Concurrency Protection: Token-bucket rate limiting per entity (user / chat)
    int64_t rate_entity = (sender_user_id != 0) ? sender_user_id : chat_id;
    if (rate_limiter_ && !rate_limiter_->allow(rate_entity)) {
        ANONX_LOG_WARN("Dispatcher", "Rate limit exceeded for entity: ", rate_entity, " in chat: ", chat_id);
        return;
    }

    ANONX_LOG_INFO("Dispatcher", "Command received: /", cmd, " from user ", sender_user_id, " in chat ", chat_id);

    // Offload command execution off the TDLib receiver thread with backpressure protection
    if (thread_pool_) {
        try {
            thread_pool_->enqueue([this, cmd_str = std::string(cmd), args_str = std::string(args),
                                   chat_id, sender_user_id, msg_id]() {
                this->execute_command(cmd_str, args_str, chat_id, sender_user_id, msg_id);
            });
        } catch (const std::exception& ex) {
            ANONX_LOG_WARN("Dispatcher", "Backpressure triggered: thread pool saturated, shedding command: ", ex.what());
        }
    } else {
        execute_command(std::string(cmd), std::string(args), chat_id, sender_user_id, msg_id);
    }
}

void CommandDispatcher::execute_command(const std::string& cmd, const std::string& args,
                                        int64_t chat_id, int64_t sender_user_id, int64_t msg_id) {
    if (cmd == "play" || cmd == "p") {
        handle_play(chat_id, sender_user_id, msg_id, args);
    } else if (cmd == "skip" || cmd == "next") {
        handle_skip(chat_id, sender_user_id, msg_id);
    } else if (cmd == "pause") {
        handle_pause(chat_id, sender_user_id, msg_id);
    } else if (cmd == "resume") {
        handle_resume(chat_id, sender_user_id, msg_id);
    } else if (cmd == "stop" || cmd == "end") {
        handle_stop(chat_id, sender_user_id, msg_id);
    } else if (cmd == "queue" || cmd == "q") {
        handle_queue(chat_id, sender_user_id, msg_id);
    } else if (cmd == "volume" || cmd == "vol") {
        handle_volume(chat_id, sender_user_id, msg_id, args);
    } else if (cmd == "ping") {
        handle_ping(chat_id, msg_id);
    } else if (cmd == "help" || cmd == "start") {
        handle_help(chat_id, msg_id);
    }
}

void CommandDispatcher::handle_play(int64_t chat_id, int64_t user_id, int64_t msg_id, const std::string& query) {
    if (query.empty()) {
        client_->send_message(chat_id, "❗ Please provide a song name, YouTube URL, or stream link.\nExample: `/play Believer`", msg_id);
        return;
    }

    database::TrackItem track;
    track.id = std::to_string(get_current_epoch_ms());
    track.title = query;
    track.url = query;
    track.requester_id = user_id;
    track.added_timestamp = get_current_epoch_ms();

    auto current_state = audio::AudioStreamer::instance().get_state(chat_id);

    if (current_state == audio::PlayerState::Idle) {
        // Directly invoke WebRTC group call bridge on NTgCallsClient
        nlohmann::json transport_desc = {
            {"chat_id", chat_id},
            {"mode", "rtc"},
            {"codec", "opus"}
        };
        audio::NTgCallsClient::instance().join_group_call(chat_id, transport_desc.dump());

        bool started = audio::AudioStreamer::instance().play(chat_id, track);
        if (started) {
            std::string msg = "▶️ **Now Playing:** " + track.title + "\n👤 **Requested by:** `" + std::to_string(user_id) + "`";
            client_->send_message(chat_id, msg, msg_id);
        } else {
            client_->send_message(chat_id, "❌ Error starting audio stream pipeline.", msg_id);
        }
    } else {
        // Enqueue track into database
        database::MongoClient::instance().enqueue_track(chat_id, track);
        auto q = database::MongoClient::instance().get_queue(chat_id);
        std::string msg = "⏳ **Queued at position #" + std::to_string(q.size()) + ":** " + track.title;
        client_->send_message(chat_id, msg, msg_id);
    }
}

void CommandDispatcher::handle_skip(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id) {
    auto current_state = audio::AudioStreamer::instance().get_state(chat_id);
    if (current_state == audio::PlayerState::Idle) {
        client_->send_message(chat_id, "ℹ️ Nothing is currently playing to skip.", msg_id);
        return;
    }

    client_->send_message(chat_id, "⏭️ Skipped current track.", msg_id);
    play_next_in_queue(chat_id);
}

void CommandDispatcher::handle_pause(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id) {
    if (audio::AudioStreamer::instance().pause(chat_id)) {
        client_->send_message(chat_id, "⏸️ Playback paused.", msg_id);
    } else {
        client_->send_message(chat_id, "ℹ️ No active playback to pause.", msg_id);
    }
}

void CommandDispatcher::handle_resume(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id) {
    if (audio::AudioStreamer::instance().resume(chat_id)) {
        client_->send_message(chat_id, "▶️ Playback resumed.", msg_id);
    } else {
        client_->send_message(chat_id, "ℹ️ Playback is not paused.", msg_id);
    }
}

void CommandDispatcher::handle_stop(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id) {
    database::MongoClient::instance().clear_queue(chat_id);
    audio::AudioStreamer::instance().stop(chat_id);
    audio::NTgCallsClient::instance().leave_group_call(chat_id);
    client_->send_message(chat_id, "⏹️ Playback stopped and queue cleared. Left voice chat.", msg_id);
}

void CommandDispatcher::handle_queue(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id) {
    auto current = audio::AudioStreamer::instance().get_current_track(chat_id);
    auto queue = database::MongoClient::instance().get_queue(chat_id);

    if (!current.has_value() && queue.empty()) {
        client_->send_message(chat_id, "ℹ️ The queue is currently empty.", msg_id);
        return;
    }

    std::ostringstream ss;
    ss << "🎵 **Current Track & Queue:**\n\n";
    if (current.has_value()) {
        ss << "▶️ **Now Playing:** " << current->title << "\n\n";
    }

    if (!queue.empty()) {
        ss << "📜 **Upcoming Tracks:**\n";
        for (size_t i = 0; i < queue.size() && i < 10; ++i) {
            ss << (i + 1) << ". " << queue[i].title << "\n";
        }
        if (queue.size() > 10) {
            ss << "... and " << (queue.size() - 10) << " more tracks.\n";
        }
    }

    client_->send_message(chat_id, ss.str(), msg_id);
}

void CommandDispatcher::handle_volume(int64_t chat_id, int64_t /*user_id*/, int64_t msg_id, const std::string& arg) {
    if (arg.empty()) {
        client_->send_message(chat_id, "❗ Please provide a volume percentage (1 - 200).\nExample: `/volume 120`", msg_id);
        return;
    }

    try {
        int vol = std::stoi(arg);
        if (vol < 1 || vol > 200) {
            client_->send_message(chat_id, "⚠️ Volume must be between 1% and 200%.", msg_id);
            return;
        }

        audio::AudioStreamer::instance().set_volume(chat_id, vol);
        client_->send_message(chat_id, "🔊 Volume set to " + std::to_string(vol) + "%.", msg_id);
    } catch (...) {
        client_->send_message(chat_id, "⚠️ Invalid volume value.", msg_id);
    }
}

void CommandDispatcher::handle_ping(int64_t chat_id, int64_t msg_id) {
    int64_t now = get_current_epoch_ms();
    int64_t uptime_sec = (now - start_time_) / 1000;

    int hours = static_cast<int>(uptime_sec / 3600);
    int mins = static_cast<int>((uptime_sec % 3600) / 60);
    int secs = static_cast<int>(uptime_sec % 60);

    size_t pending = thread_pool_ ? thread_pool_->pending_tasks() : 0;
    uint64_t completed = thread_pool_ ? thread_pool_->completed_tasks() : 0;
    size_t workers = thread_pool_ ? thread_pool_->worker_count() : 0;

    std::ostringstream ss;
    ss << "🏓 **Pong!**\n"
       << "⏱️ **Uptime:** " << hours << "h " << mins << "m " << secs << "s\n"
       << "🚀 **Engine:** AnonX C++20 High-Performance Core\n"
       << "⚡ **ThreadPool:** " << workers << " workers | " << completed << " completed | " << pending << " queued\n"
       << "🛡️ **RateLimiting:** Active (Token-Bucket Striped)";
    std::string ping_msg = ss.str();

    client_->send_message(chat_id, ping_msg, msg_id);
}

void CommandDispatcher::handle_help(int64_t chat_id, int64_t msg_id) {
    std::string help_text =
        "🎶 **AnonX-CPP Music Bot Commands**\n\n"
        "• `/play <name or URL>` - Stream YouTube audio or direct audio link\n"
        "• `/pause` - Pause currently playing stream\n"
        "• `/resume` - Resume paused stream\n"
        "• `/skip` - Skip to next song in queue\n"
        "• `/stop` - Stop streaming, clear queue, and leave voice chat\n"
        "• `/queue` - Display active song queue\n"
        "• `/volume <1-200>` - Change audio playback volume\n"
        "• `/ping` - Check bot status and uptime\n"
        "• `/help` - Show this help menu";

    client_->send_message(chat_id, help_text, msg_id);
}

void CommandDispatcher::play_next_in_queue(int64_t chat_id) {
    auto next_track = database::MongoClient::instance().pop_next_track(chat_id);
    if (next_track.has_value()) {
        bool started = audio::AudioStreamer::instance().play(chat_id, *next_track);
        if (started) {
            std::string msg = "▶️ **Now Playing Next Track:** " + next_track->title;
            client_->send_message(chat_id, msg);
        } else {
            // If failed, recurse to next
            play_next_in_queue(chat_id);
        }
    } else {
        // Queue finished
        audio::AudioStreamer::instance().stop(chat_id);
        audio::NTgCallsClient::instance().leave_group_call(chat_id);
        client_->send_message(chat_id, "⏹️ Queue completed. Voice chat playback ended.");
    }
}

} // namespace anonx::telegram
