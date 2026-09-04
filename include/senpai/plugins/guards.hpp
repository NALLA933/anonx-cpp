#ifndef SENPAI_GUARDS_HPP
#define SENPAI_GUARDS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "senpai/telegram/telegram_client.hpp"
#include "senpai/core/config.hpp"
#include "senpai/database/database.hpp"
#include "senpai/voice/queue.hpp"
#include "senpai/utils/youtube.hpp"

namespace senpai {
namespace guards {

bool isAdmin(TelegramClient& api, std::int64_t chatId, std::int64_t userId);

bool isSudo(Database& db, const Config& config, std::int64_t userId);

bool canManageVc(TelegramClient& api, Database& db, std::int64_t chatId, std::int64_t userId);

bool adminCheck(TelegramClient& api, Database& db, bool isPrivate,
                std::int64_t chatId, std::int64_t userId);

struct PlayRequest {
    std::int64_t chatId = 0;
    std::int64_t fromUserId = 0;
    bool isSupergroup = true;
    bool hasReply = false;
    std::vector<std::string> command;
};

enum class PlayGate {
    Proceed,
    UserInvalid,
    ChatInvalid,
    Usage,
    QueueFull,
    NotFound,
    AdminOnly,
};

struct PlayPreflight {
    PlayGate    gate = PlayGate::Proceed;
    bool        force = false;
    bool        video = false;
    bool        m3u8  = false;
    std::string url;
    std::string query;

    bool        cmdDelete = false;
};

bool isFlag(const std::string& token);

PlayPreflight runPlayPreflight(TelegramClient& api, Database& db, Queue& queue,
                               YouTube& yt, const Config& config,
                               const PlayRequest& req);

std::string resolveUrl(const std::vector<std::string>& command);

}
}

#endif
