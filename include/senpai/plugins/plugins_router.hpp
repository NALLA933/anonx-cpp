#ifndef SENPAI_PLUGINS_ROUTER_HPP
#define SENPAI_PLUGINS_ROUTER_HPP

#include "senpai/admin_plugins.hpp"
#include "senpai/database.hpp"
#include "senpai/dispatcher.hpp"
#include "senpai/plugins.hpp"

namespace senpai {

CommandEvent toCommandEvent(const MessageContext& ctx);
ButtonEvent  toButtonEvent(const CallbackContext& ctx);

void installPlugins(Dispatcher& disp, Plugins& plugins, AdminPlugins& admin,
                    Database& db);

}

#endif
