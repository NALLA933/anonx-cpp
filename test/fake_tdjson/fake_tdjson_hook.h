// AnonXMusic C++ port — Phase 4 / integration TEST SCAFFOLDING (not production)
//
// Test-only hooks into the fake TDLib. Three groups:
//
//   1. injection — push an arbitrary update (an updateNewMessage, an
//      updateNewCallbackQuery, …) at a specific client id, so the full
//      receive-pump -> TelegramClient -> Dispatcher path runs deterministically;
//   2. recording — read back every request the code under test sent. This is how
//      the integration test verifies what the bot would actually put on the wire
//      (the card text, the inline keyboard, the answerCallbackQuery) instead of
//      only checking that nothing crashed;
//   3. a small message store — so getMessage() can answer, which is what the
//      reply-based commands and the "read the card back" paths need.
//
// None of this is part of the real TDLib API.

#ifndef ANONX_FAKE_TDJSON_HOOK_H
#define ANONX_FAKE_TDJSON_HOOK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Push `updateJson` into the fake's receive queue, tagged for `clientId`.
// The pump will deliver it just like a real TDLib update.
void fake_td_inject(int clientId, const char* updateJson);

// --- recording of outgoing requests -----------------------------------------

// Forget every recorded request. Call this right before the interaction under
// test so the indices below are relative to it.
void fake_td_clear_requests();

std::size_t fake_td_request_count();

// All recorded requests, oldest first, as the raw JSON the code sent.
std::vector<std::string> fake_td_requests();

std::size_t fake_td_count_of_type(const char* type);

// The nth (0-based) recorded request whose "@type" is `type`; "" if there is
// none. `fake_td_last_request_of_type` returns the most recent one.
std::string fake_td_request_of_type(const char* type, std::size_t nth = 0);
std::string fake_td_last_request_of_type(const char* type);

// --- message store -----------------------------------------------------------

// Make getMessage(chatId, messageId) answer with a text message from
// `senderUserId` whose body is `text` verbatim (the fake does no HTML parsing).
// Messages the code itself sends are stored automatically.
void fake_td_put_message(std::int64_t chatId, std::int64_t messageId,
                         std::int64_t senderUserId, const char* text);

// Drop the pending update queue, the recorded requests and the stored messages.
// Client ids and the authorization states already delivered are unaffected.
void fake_td_reset();

#endif  // ANONX_FAKE_TDJSON_HOOK_H
