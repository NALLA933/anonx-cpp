#ifndef ANONX_FAKE_TDJSON_HOOK_H
#define ANONX_FAKE_TDJSON_HOOK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

void fake_td_inject(int clientId, const char* updateJson);

void fake_td_clear_requests();

std::size_t fake_td_request_count();

std::vector<std::string> fake_td_requests();

std::size_t fake_td_count_of_type(const char* type);

std::string fake_td_request_of_type(const char* type, std::size_t nth = 0);
std::string fake_td_last_request_of_type(const char* type);

void fake_td_put_message(std::int64_t chatId, std::int64_t messageId,
                         std::int64_t senderUserId, const char* text);

void fake_td_reset();

#endif
