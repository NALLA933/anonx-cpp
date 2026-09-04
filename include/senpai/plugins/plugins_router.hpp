#ifndef SENPAI_PLUGINS_ROUTER_HPP
#define SENPAI_PLUGINS_ROUTER_HPP

#include "senpai/plugins/admin_plugins.hpp"
#include "senpai/database/database.hpp"
#include "senpai/telegram/dispatcher.hpp"
#include "senpai/plugins/plugins.hpp"

namespace senpai {

void installPlugins(Dispatcher& disp, Plugins& plugins, AdminPlugins& admin,
                    Database& db);

}

#endif
