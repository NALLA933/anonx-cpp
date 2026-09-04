#ifndef SENPAI_ADMIN_PLUGINS_HPP
#define SENPAI_ADMIN_PLUGINS_HPP

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "senpai/bot_api.hpp"
#include "senpai/buttons.hpp"
#include "senpai/cache_manager.hpp"
#include "senpai/call_manager.hpp"
#include "senpai/config.hpp"
#include "senpai/database.hpp"
#include "senpai/lang.hpp"
#include "senpai/plugins.hpp"
#include "senpai/sysinfo.hpp"

namespace senpai {

class AdminPlugins {
public:
    struct Deps {
        BotApi&         api;
        Database&       db;
        CacheManager&   cache;
        CallManager&    calls;
        SystemInfo&     sys;
        const Language& lang;
        const Config&   config;
    };

    explicit AdminPlugins(const Deps& deps);

    void onAuth(const CommandEvent& ev);
    void onAuthList(const CommandEvent& ev);

    void onBlacklist(const CommandEvent& ev);

    void onGcast(const CommandEvent& ev);

    void onSudo(const CommandEvent& ev);
    void onSudoList(const CommandEvent& ev);

    void onLang(const CommandEvent& ev);

    void onPing(const CommandEvent& ev);
    void onStats(const CommandEvent& ev);
    void onActiveVc(const CommandEvent& ev);

    void onStart(const CommandEvent& ev);
    void onHelp(const CommandEvent& ev);
    void onSettings(const CommandEvent& ev);

    void onLogger(const CommandEvent& ev);

    void onSeen(const CommandEvent& ev);

    void onMenu(const ButtonEvent& ev);

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

    std::string startCard(const LangView& L, const CommandEvent& ev) const;
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

    std::int64_t resolveTarget(const CommandEvent& ev) const;

    bool toLogGroup(const std::string& html);

    bool mayConfigure(const CommandEvent& ev) const;

    BotApi&         api_;
    Database&       db_;
    CacheManager&   cache_;
    CallManager&    calls_;
    SystemInfo&     sys_;
    const Language& lang_;
    const Config&   config_;

    std::atomic<bool> broadcasting_{false};

    mutable std::mutex                   mutex_;
    std::map<std::int64_t, std::int64_t>  status_;
};

}

#endif
