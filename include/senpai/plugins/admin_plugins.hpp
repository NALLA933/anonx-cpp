#ifndef SENPAI_ADMIN_PLUGINS_HPP
#define SENPAI_ADMIN_PLUGINS_HPP

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "senpai/core/config.hpp"
#include "senpai/database/cache_manager.hpp"
#include "senpai/database/database.hpp"
#include "senpai/plugins/buttons.hpp"
#include "senpai/plugins/lang.hpp"
#include "senpai/telegram/dispatcher.hpp"
#include "senpai/telegram/telegram_client.hpp"
#include "senpai/utils/sysinfo.hpp"
#include "senpai/voice/call_manager.hpp"

namespace senpai {

class AdminPlugins {
public:
    struct Deps {
        TelegramClient& api;
        Database&       db;
        CacheManager&   cache;
        CallManager&    calls;
        SystemInfo&     sys;
        const Language& lang;
        const Config&   config;
    };

    explicit AdminPlugins(const Deps& deps);

    void onAuth(const MessageContext& ev);
    void onAuthList(const MessageContext& ev);

    void onBlacklist(const MessageContext& ev);

    void onGcast(const MessageContext& ev);

    void onSudo(const MessageContext& ev);
    void onSudoList(const MessageContext& ev);

    void onLang(const MessageContext& ev);

    void onPing(const MessageContext& ev);
    void onStats(const MessageContext& ev);
    void onActiveVc(const MessageContext& ev);

    void onStart(const MessageContext& ev);
    void onHelp(const MessageContext& ev);
    void onSettings(const MessageContext& ev);

    void onLogger(const MessageContext& ev);

    void onSeen(const MessageContext& ev);

    void onMenu(const CallbackContext& ev);

    static std::vector<std::string> authCommands();
    static std::vector<std::string> authListCommands();
    static std::vector<std::string> blacklistCommands();
    static std::vector<std::string> gcastCommands();
    static std::vector<std::string> sudoCommands();
    static std::vector<std::string> sudoListCommands();
    static std::vector<std::string> langCommands();
    static std::vector<std::string> pingCommands();
    static std::vector<std::string> statsCommands();
    static std::vector<std::string> activeVcCommands();
    static std::vector<std::string> startCommands();
    static std::vector<std::string> helpCommands();
    static std::vector<std::string> settingsCommands();
    static std::vector<std::string> loggerCommands();

    static std::vector<std::vector<std::string>> allCommandGroups();
    static int moduleCount();

    static const std::vector<std::pair<std::string, std::string>>& helpTopics();

private:
    LangView tr(std::int64_t chatId) const;

    buttons::MenuText menuText(const LangView& L) const;

    std::string startCard(const LangView& L, const MessageContext& ev) const;
    std::string helpBody(const LangView& L) const;
    std::string settingsCard(const LangView& L) const;
    InlineKeyboard startKeyboard(const LangView& L, bool isPrivate) const;
    InlineKeyboard helpKeyboard(const LangView& L) const;
    InlineKeyboard settingsKeyboard(const LangView& L, std::int64_t chatId) const;
    InlineKeyboard languageKeyboard(const LangView& L) const;

    std::string addMeUrl() const;

    void         setStatus(std::int64_t chatId, std::int64_t messageId);
    std::int64_t takeStatus(std::int64_t chatId);
    std::int64_t say(std::int64_t chatId, const std::string& html,
                     const InlineKeyboard& kb = {});

    std::int64_t resolveTarget(const MessageContext& ev) const;

    bool toLogGroup(const std::string& html);

    bool mayConfigure(const MessageContext& ev) const;

    TelegramClient& api_;
    Database&       db_;
    CacheManager&   cache_;
    CallManager&    calls_;
    SystemInfo&     sys_;
    const Language& lang_;
    const Config&   config_;

    std::atomic<bool> broadcasting_{false};

    mutable std::mutex                  mutex_;
    std::map<std::int64_t, std::int64_t> status_;
};

}

#endif
