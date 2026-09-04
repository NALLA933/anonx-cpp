#ifndef SENPAI_VOICE_SIGNALING_HPP
#define SENPAI_VOICE_SIGNALING_HPP

#include <functional>

#include "senpai/voice/ntgcalls_transport.hpp"
#include "senpai/telegram/telegram_client.hpp"

namespace senpai {

NtgCallsTransport::Signaling makeAssistantSignaling(TelegramClient& assistant);

NtgCallsTransport::Signaling makeDeferredAssistantSignaling(
    std::function<TelegramClient*()> provider);

}

#endif
