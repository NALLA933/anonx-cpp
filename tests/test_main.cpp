#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "senpai/core/config.hpp"
#include "senpai/core/version.hpp"
#include "senpai/database/cache_manager.hpp"
#include "senpai/plugins/lang.hpp"
#include "senpai/utils/string_utils.hpp"
#include "senpai/voice/queue.hpp"

void testVersion() {
    std::cout << "[RUN] testVersion..." << std::endl;
    assert(std::string(senpai::kVersion) == "3.0.3-cpp");
    std::cout << "[PASS] testVersion" << std::endl;
}

void testStringUtils() {
    std::cout << "[RUN] testStringUtils..." << std::endl;

    assert(senpai::utils::toLower("HeLLo WoRLd 123!") == "hello world 123!");

    assert(senpai::utils::trim("  \t senpai bot \n\r") == "senpai bot");
    assert(senpai::utils::trim("   ") == "");

    auto tokens = senpai::utils::splitWs("  play   force   track1  ");
    assert(tokens.size() == 3);
    assert(tokens[0] == "play");
    assert(tokens[1] == "force");
    assert(tokens[2] == "track1");

    std::int64_t val = 0;
    assert(senpai::utils::parseI64("1234567890", val) && val == 1234567890LL);
    assert(senpai::utils::parseI64("-9876543210", val) && val == -9876543210LL);
    assert(!senpai::utils::parseI64("not_a_number", val));
    assert(!senpai::utils::parseI64("99999999999999999999999", val));
    (void)val;

    assert(senpai::utils::htmlEscape("<b>Tom & Jerry</b>") == "&lt;b&gt;Tom &amp; Jerry&lt;/b&gt;");

    std::cout << "[PASS] testStringUtils" << std::endl;
}

void testQueue() {
    std::cout << "[RUN] testQueue..." << std::endl;
    senpai::Queue q;
    std::int64_t chat = -1001234567890LL;

    assert(q.empty(chat));
    assert(q.size(chat) == 0);

    senpai::MediaItem track1;
    track1.id = "vid1";
    track1.title = "Song One";

    senpai::MediaItem track2;
    track2.id = "vid2";
    track2.title = "Song Two";

    int pos0 = q.add(chat, track1);
    assert(pos0 == 0);
    assert(!q.empty(chat));
    assert(q.size(chat) == 1);
    (void)pos0;

    int pos1 = q.add(chat, track2);
    assert(pos1 == 1);
    assert(q.size(chat) == 2);
    (void)pos1;

    auto cur = q.getCurrent(chat);
    assert(cur.has_value() && cur->id == "vid1");

    auto next = q.getNext(chat);
    assert(next.has_value() && next->id == "vid2");
    assert(q.size(chat) == 1);

    q.clear(chat);
    assert(q.empty(chat));
    std::cout << "[PASS] testQueue" << std::endl;
}

void testCacheManager() {
    std::cout << "[RUN] testCacheManager..." << std::endl;
    senpai::CacheManager cache;
    std::int64_t chat = -100999LL;

    assert(!cache.isActiveCall(chat));
    assert(!cache.isPlaying(chat));

    cache.addCall(chat);
    assert(cache.isActiveCall(chat));
    assert(cache.isPlaying(chat));

    cache.setPaused(chat, true);
    assert(!cache.isPlaying(chat));

    cache.setPaused(chat, false);
    assert(cache.isPlaying(chat));

    cache.setLoop(chat, 3);
    assert(cache.getLoop(chat) == 3);

    cache.clearChat(chat);
    assert(!cache.isActiveCall(chat));
    assert(cache.getLoop(chat) == 0);
    std::cout << "[PASS] testCacheManager" << std::endl;
}

void testLanguage() {
    std::cout << "[RUN] testLanguage..." << std::endl;
    senpai::Language lang;
    int count = lang.loadDir("locales");
    std::cout << "Loaded " << count << " languages from locales/" << std::endl;
    assert(count > 0);

    auto en = lang.view("en");
    std::string playUsage = en.get("play_1");
    assert(!playUsage.empty());
    std::cout << "[PASS] testLanguage" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     SenpaiMusic C++ Offline Tests      " << std::endl;
    std::cout << "========================================" << std::endl;

    testVersion();
    testStringUtils();
    testQueue();
    testCacheManager();
    testLanguage();

    std::cout << "========================================" << std::endl;
    std::cout << "     ALL UNIT TESTS PASSED (5/5)        " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
