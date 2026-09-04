#include "senpai/voice_signaling.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "senpai/logger.hpp"

namespace senpai {
namespace {

using nlohmann::json;

Logger log() { return Logger("senpai.voice.signaling"); }

std::string strField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return std::string();
}

std::int64_t intField(const json& j, const char* key) {
    if (j.is_object() && j.contains(key) && j[key].is_number()) {
        return j[key].get<std::int64_t>();
    }
    return 0;
}

bool isError(const json& j) {
    return j.is_object() && strField(j, "@type") == "error";
}

bool looksLikeUnknownMethod(const json& err) {
    const std::string msg = strField(err, "message");
    return msg.find("Unknown") != std::string::npos ||
           msg.find("unknown") != std::string::npos ||
           msg.find("not supported") != std::string::npos;
}

std::int32_t audioSourceId(const json& params) {
    for (const char* key : {"ssrc", "source", "audio_source"}) {
        if (params.is_object() && params.contains(key) && params[key].is_number()) {
            return static_cast<std::int32_t>(intField(params, key));
        }
    }
    return 0;
}

struct CallState {
    std::mutex mtx;
    std::unordered_map<std::int64_t, std::int32_t> groupCallIdByChat;

    void remember(std::int64_t chatId, std::int32_t groupCallId) {
        std::lock_guard<std::mutex> lk(mtx);
        groupCallIdByChat[chatId] = groupCallId;
    }
    std::int32_t take(std::int64_t chatId) {
        std::lock_guard<std::mutex> lk(mtx);
        const auto it = groupCallIdByChat.find(chatId);
        if (it == groupCallIdByChat.end()) return 0;
        const std::int32_t id = it->second;
        groupCallIdByChat.erase(it);
        return id;
    }
};

std::atomic<int> g_dialect{0};

const char* joinName(int dialect) {
    return dialect == 2 ? "joinVideoChat" : "joinGroupCall";
}
const char* leaveName(int dialect) {
    return dialect == 2 ? "leaveVideoChat" : "leaveGroupCall";
}

std::int32_t activeGroupCallId(TelegramClient& client, std::int64_t chatId) {
    json req;
    req["@type"] = "getChat";
    req["chat_id"] = chatId;
    json chat = json::parse(client.raw().invoke(req.dump()), nullptr, false);
    if (chat.is_discarded() || !chat.is_object() || isError(chat)) {
        if (isError(chat)) {
            log().warning("activeGroupCallId: getChat error for chat " +
                          std::to_string(chatId) + ": " + strField(chat, "message"));
        }
        return 0;
    }

    for (const char* key : {"video_chat", "voice_chat"}) {
        if (chat.contains(key) && chat[key].is_object()) {
            const std::int32_t id =
                static_cast<std::int32_t>(intField(chat[key], "group_call_id"));
            if (id != 0) {
                log().info("activeGroupCallId: found group_call_id=" +
                          std::to_string(id) + " for chat " + std::to_string(chatId));
                return id;
            }
        }
    }
    log().warning("activeGroupCallId: no active group_call_id found for chat " +
                  std::to_string(chatId));
    return 0;
}

}

NtgCallsTransport::Signaling makeAssistantSignaling(TelegramClient& assistant) {

    auto state = std::make_shared<CallState>();
    TelegramClient* client = &assistant;

    NtgCallsTransport::Signaling sig;

    sig.joinGroupCall = [client, state](std::int64_t chatId,
                                       const std::string& localParams) -> std::string {
        const std::int32_t groupCallId = activeGroupCallId(*client, chatId);
        if (groupCallId == 0) {
            throw VoiceError(PlayResult::NoActiveGroupCall,
                             "no open voice chat in " + std::to_string(chatId));
        }

        json payload = localParams.empty()
                           ? json::object()
                           : json::parse(localParams, nullptr, false);
        if (payload.is_discarded()) {
            throw VoiceError(PlayResult::ServerError,
                             "voice engine produced unparseable join parameters");
        }

        std::int64_t myId = client->me().id;
        if (myId == 0) {
            myId = client->getMe().id;
        }

        json req;
        req["chat_id"] = chatId;
        req["group_call_id"] = groupCallId;
        json participant;
        participant["@type"] = "messageSenderUser";
        participant["user_id"] = myId;
        req["participant_id"] = participant;
        req["audio_source_id"] = audioSourceId(payload);
        req["payload"] = localParams;
        req["is_muted"] = false;
        req["is_my_video_enabled"] = false;
        req["invite_hash"] = "";

        int dialect = g_dialect.load();
        const int first = dialect == 0 ? 1 : dialect;
        json reply;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const int tryDialect = attempt == 0 ? first : (first == 1 ? 2 : 1);
            req["@type"] = joinName(tryDialect);
            reply = json::parse(client->raw().invoke(req.dump(), 30000), nullptr, false);
            if (!reply.is_discarded() && !isError(reply)) {
                g_dialect.store(tryDialect);
                break;
            }

            if (reply.is_discarded() || !looksLikeUnknownMethod(reply) ||
                dialect != 0) {
                break;
            }
        }

        if (reply.is_discarded() || isError(reply)) {
            const std::string msg =
                reply.is_discarded() ? "unparseable reply" : strField(reply, "message");
            log().warning("join failed for chat " + std::to_string(chatId) + ": " + msg);
            if (msg.find("ALREADY") != std::string::npos ||
                msg.find("already") != std::string::npos) {
                log().info("assistant already in group call for chat " + std::to_string(chatId));
                state->remember(chatId, groupCallId);
                return "{}";
            }
            if (msg.find("GROUPCALL_INVALID") != std::string::npos ||
                msg.find("not found") != std::string::npos ||
                msg.find("not active") != std::string::npos) {
                throw VoiceError(PlayResult::NoActiveGroupCall, "join rejected: " + msg);
            }
            throw VoiceError(PlayResult::ServerError, "join rejected: " + msg);
        }

        state->remember(chatId, groupCallId);

        const std::string remote = strField(reply, "text");
        return remote.empty() ? "{}" : remote;
    };

    sig.leaveGroupCall = [client, state](std::int64_t chatId) {
        std::int32_t groupCallId = state->take(chatId);
        if (groupCallId == 0) groupCallId = activeGroupCallId(*client, chatId);
        if (groupCallId == 0) return;

        const int dialect = g_dialect.load();
        json req;
        req["@type"] = leaveName(dialect == 0 ? 1 : dialect);
        req["group_call_id"] = groupCallId;
        client->raw().send(req.dump());
    };

    return sig;
}

NtgCallsTransport::Signaling makeDeferredAssistantSignaling(
    std::function<TelegramClient*()> provider) {

    struct Lazy {
        std::mutex mtx;
        std::function<TelegramClient*()> provider;
        NtgCallsTransport::Signaling inner;
        bool bound = false;

        NtgCallsTransport::Signaling* resolve() {
            std::lock_guard<std::mutex> lk(mtx);
            if (!bound) {
                TelegramClient* client = provider ? provider() : nullptr;
                if (!client) return nullptr;
                inner = makeAssistantSignaling(*client);
                bound = true;
            }
            return &inner;
        }
    };

    auto lazy = std::make_shared<Lazy>();
    lazy->provider = std::move(provider);

    NtgCallsTransport::Signaling sig;

    sig.joinGroupCall = [lazy](std::int64_t chatId,
                               const std::string& localParams) -> std::string {
        NtgCallsTransport::Signaling* inner = lazy->resolve();
        if (!inner) {
            log().warning("join requested for chat " + std::to_string(chatId) +
                          " but no assistant account is available");
            throw VoiceError(PlayResult::NoActiveGroupCall,
                             "no assistant account is available to join with");
        }
        return inner->joinGroupCall(chatId, localParams);
    };

    sig.leaveGroupCall = [lazy](std::int64_t chatId) {

        std::lock_guard<std::mutex> lk(lazy->mtx);
        if (lazy->bound && lazy->inner.leaveGroupCall) {
            lazy->inner.leaveGroupCall(chatId);
        }
    };

    return sig;
}

}
