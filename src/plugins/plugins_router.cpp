#include "senpai/plugins/plugins_router.hpp"

namespace senpai {

void installPlugins(Dispatcher& disp, Plugins& plugins, AdminPlugins& admin,
                    Database& db) {

    plugins.attachCallbacks();

    const Filter allowed =
        !filters::userWhere([&db](std::int64_t userId) {
            return userId != 0 && db.isBlacklistedUser(userId);
        }) &&
        !Filter([&db](const MessageContext& m) { return db.isBlacklistedChat(m.chatId); });

    const Filter inGroup = filters::groupChat() && allowed;

    disp.onMessage(filters::command(Plugins::playCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onPlay(m); });
    disp.onMessage(filters::command(Plugins::skipCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onSkip(m); });
    disp.onMessage(filters::command(Plugins::pauseCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onPause(m); });
    disp.onMessage(filters::command(Plugins::resumeCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onResume(m); });
    disp.onMessage(filters::command(Plugins::stopCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onStop(m); });
    disp.onMessage(filters::command(Plugins::loopCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onLoop(m); });
    disp.onMessage(filters::command(Plugins::queueCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onQueue(m); });
    disp.onMessage(filters::command(Plugins::seekCommands()) && inGroup,
                   [&plugins](MessageContext& m) { plugins.onSeek(m); });

    const auto adminHandler = [&admin](void (AdminPlugins::*fn)(const MessageContext&)) {
        return [&admin, fn](MessageContext& m) { (admin.*fn)(m); };
    };

    disp.onMessage(filters::command(AdminPlugins::authCommands()) && inGroup,
                   adminHandler(&AdminPlugins::onAuth));
    disp.onMessage(filters::command(AdminPlugins::authListCommands()) && inGroup,
                   adminHandler(&AdminPlugins::onAuthList));
    disp.onMessage(filters::command(AdminPlugins::settingsCommands()) && inGroup,
                   adminHandler(&AdminPlugins::onSettings));

    disp.onMessage(filters::command(AdminPlugins::blacklistCommands()) && allowed,
                   adminHandler(&AdminPlugins::onBlacklist));
    disp.onMessage(filters::command(AdminPlugins::gcastCommands()) && allowed,
                   adminHandler(&AdminPlugins::onGcast));
    disp.onMessage(filters::command(AdminPlugins::sudoCommands()) && allowed,
                   adminHandler(&AdminPlugins::onSudo));
    disp.onMessage(filters::command(AdminPlugins::sudoListCommands()) && allowed,
                   adminHandler(&AdminPlugins::onSudoList));
    disp.onMessage(filters::command(AdminPlugins::langCommands()) && allowed,
                   adminHandler(&AdminPlugins::onLang));
    disp.onMessage(filters::command(AdminPlugins::pingCommands()) && allowed,
                   adminHandler(&AdminPlugins::onPing));
    disp.onMessage(filters::command(AdminPlugins::statsCommands()) && allowed,
                   adminHandler(&AdminPlugins::onStats));
    disp.onMessage(filters::command(AdminPlugins::activeVcCommands()) && allowed,
                   adminHandler(&AdminPlugins::onActiveVc));
    disp.onMessage(filters::command(AdminPlugins::startCommands()) && allowed,
                   adminHandler(&AdminPlugins::onStart));
    disp.onMessage(filters::command(AdminPlugins::helpCommands()) && allowed,
                   adminHandler(&AdminPlugins::onHelp));
    disp.onMessage(filters::command(AdminPlugins::loggerCommands()) && allowed,
                   adminHandler(&AdminPlugins::onLogger));

    disp.onEveryMessage([&admin](MessageContext& m) { admin.onSeen(m); });

    disp.onCallback(filters::callbackDataPrefix("controls"),
                    [&plugins](CallbackContext& c) { plugins.onControls(c); });

    for (const char* prefix : {"help", "lang", "settings", "start", "close"}) {
        disp.onCallback(filters::callbackDataPrefix(prefix),
                        [&admin](CallbackContext& c) { admin.onMenu(c); });
    }
}

}
