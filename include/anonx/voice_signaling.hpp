#ifndef ANONX_VOICE_SIGNALING_HPP
#define ANONX_VOICE_SIGNALING_HPP

#include <functional>

#include "anonx/ntgcalls_transport.hpp"
#include "anonx/telegram_client.hpp"

namespace anonx {

NtgCallsTransport::Signaling makeAssistantSignaling(TelegramClient& assistant);

NtgCallsTransport::Signaling makeDeferredAssistantSignaling(
    std::function<TelegramClient*()> provider);

}

#endif
