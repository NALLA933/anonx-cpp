#ifndef ANONX_PLUGINS_ROUTER_HPP
#define ANONX_PLUGINS_ROUTER_HPP

#include "anonx/admin_plugins.hpp"
#include "anonx/database.hpp"
#include "anonx/dispatcher.hpp"
#include "anonx/plugins.hpp"

namespace anonx {

CommandEvent toCommandEvent(const MessageContext& ctx);
ButtonEvent  toButtonEvent(const CallbackContext& ctx);

void installPlugins(Dispatcher& disp, Plugins& plugins, AdminPlugins& admin,
                    Database& db);

}

#endif
