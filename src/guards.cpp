#include "anonx/guards.hpp"

namespace anonx {
namespace guards {

bool isAdmin(BotApi& api, std::int64_t chatId, std::int64_t userId) {
    const std::string status = api.getChatMemberStatus(chatId, userId);
    return status == "chatMemberStatusAdministrator" ||
           status == "chatMemberStatusCreator";
}

bool isSudo(Database& db, const Config& config, std::int64_t userId) {
    return userId == config.owner_id || db.isSudo(userId);
}

bool canManageVc(BotApi& api, Database& db, std::int64_t chatId, std::int64_t userId) {

    if (db.isSudo(userId))
        return true;
    if (db.isAuth(chatId, userId))
        return true;
    return isAdmin(api, chatId, userId);
}

bool adminCheck(BotApi& api, Database& db, bool isPrivate,
                std::int64_t chatId, std::int64_t userId) {
    if (isPrivate)
        return true;
    if (db.isSudo(userId))
        return true;
    return isAdmin(api, chatId, userId);
}

bool isFlag(const std::string& token) {

    return token == "-f" || token == "-force" || token == "-v" || token == "-video";
}

std::string resolveUrl(const std::vector<std::string>& command) {
    for (std::size_t i = 1; i < command.size(); ++i) {
        const std::string& tok = command[i];
        if (tok.rfind("http://", 0) == 0 || tok.rfind("https://", 0) == 0) {
            std::string link = tok;

            std::size_t p = link.find("&si");
            if (p != std::string::npos) link = link.substr(0, p);
            p = link.find("?si");
            if (p != std::string::npos) link = link.substr(0, p);
            return link;
        }
    }
    return "";
}

PlayPreflight runPlayPreflight(BotApi& api, Database& db, Queue& queue,
                               YouTube& yt, const Config& config,
                               const PlayRequest& req) {
    PlayPreflight r;

    if (req.fromUserId == 0) {
        r.gate = PlayGate::UserInvalid;
        return r;
    }

    if (!req.isSupergroup) {
        r.gate = PlayGate::ChatInvalid;
        return r;
    }

    const auto& cmd = req.command;

    bool argForce = false, argVideo = false;
    std::string query;
    for (std::size_t i = 1; i < cmd.size(); ++i) {
        if (isFlag(cmd[i])) {
            if (cmd[i][1] == 'f') argForce = true; else argVideo = true;
            continue;
        }
        if (!query.empty()) query.push_back(' ');
        query += cmd[i];
    }
    r.query = query;

    if (!req.hasReply && query.empty()) {
        r.gate = PlayGate::Usage;
        return r;
    }

    if (static_cast<int>(queue.size(req.chatId)) >= config.queue_limit) {
        r.gate = PlayGate::QueueFull;
        return r;
    }

    const std::string& name = cmd.empty() ? std::string() : cmd[0];
    const bool nameForce = name.size() >= 5 &&
                           name.compare(name.size() - 5, 5, "force") == 0;
    r.force = nameForce || argForce;

    r.video = ((!name.empty() && name[0] == 'v') || argVideo) && config.video_play;

    r.url = resolveUrl(cmd);
    if (!r.url.empty() && yt.invalid(r.url)) {
        r.gate = PlayGate::NotFound;
        return r;
    }

    r.m3u8 = !r.url.empty() && !yt.valid(r.url);

    if (db.getPlayMode(req.chatId) || r.force) {
        const bool allowed = isAdmin(api, req.chatId, req.fromUserId) ||
                             db.isAuth(req.chatId, req.fromUserId) ||
                             db.isSudo(req.fromUserId);
        if (!allowed) {
            r.gate = PlayGate::AdminOnly;
            return r;
        }
    }

    r.cmdDelete = db.getCmdDelete(req.chatId);

    r.gate = PlayGate::Proceed;
    return r;
}

}
}
