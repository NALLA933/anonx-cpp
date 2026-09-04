// AnonXMusic C++ port — Phase 5 (voice + queue)
// examples/voice_demo.cpp
//
// Self-checking test harness for the Phase 5 playback layer. It exercises:
//   * Queue        — every operation ported from _queue.py, plus per-chat
//                    isolation and the C++ replaceCurrent helper.
//   * CallManager  — the full TgCall orchestration, driven through a scripted
//                    FakeVoiceTransport: enqueue-vs-start, now-playing card,
//                    pause/resume (and stop-on-failure), stream-end auto-
//                    advance, loop replays, download-on-demand, skip-on-
//                    download-failure, the play_media error branches, stale
//                    card deletion, ping, and stop-on-call-closed.
//
// No network, no NTgCalls: it links only the header-only Queue + CacheManager,
// the CallManager source, and the fake transport. Exits non-zero on any failure
// so CMake/ctest treats a regression as a build break.

#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "anonx/call_manager.hpp"
#include "anonx/queue.hpp"
#include "../test/fake_ntgcalls/fake_voice_transport.hpp"

using anonx::CallManager;
using anonx::CacheManager;
using anonx::FakeVoiceTransport;
using anonx::MediaItem;
using anonx::PlayResult;
using anonx::Queue;
using anonx::StreamKind;
using anonx::Track;

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::cerr << "FAIL: " << (msg) << "  [line " << __LINE__ << "]\n"; \
        }                                                                   \
    } while (0)

// Build a Track with the fields the queue/manager care about.
static Track mk(const std::string& id, const std::string& path = "", bool video = false) {
    Track t;
    t.id        = id;
    t.title     = "Title-" + id;
    t.url       = "https://y/" + id;
    t.duration  = "1:00";
    t.user      = "@user";
    t.file_path = path;
    t.video     = video;
    return t;
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------
static void testQueue() {
    Queue q;
    const std::int64_t c = 1;

    // add returns 0-based positions; size tracks.
    CHECK(q.add(c, mk("A")) == 0, "add A -> pos 0");
    CHECK(q.add(c, mk("B")) == 1, "add B -> pos 1");
    CHECK(q.add(c, mk("C")) == 2, "add C -> pos 2");
    CHECK(q.size(c) == 3, "size == 3");
    CHECK(!q.empty(c), "not empty");

    // getCurrent = head; empty chat -> nullopt.
    auto cur = q.getCurrent(c);
    CHECK(cur && cur->id == "A", "current == A");
    CHECK(!q.getCurrent(999).has_value(), "empty chat current == nullopt");

    // getNext(check=true) peeks index 1 without mutating.
    auto peek = q.getNext(c, /*check=*/true);
    CHECK(peek && peek->id == "B", "peek next == B");
    CHECK(q.size(c) == 3, "peek does not mutate");

    // getQueue returns full order.
    auto all = q.getQueue(c);
    CHECK(all.size() == 3 && all[0].id == "A" && all[1].id == "B" && all[2].id == "C",
          "getQueue order A,B,C");

    // checkItem finds by id; missing -> (-1, nullopt).
    auto found = q.checkItem(c, "C");
    CHECK(found.first == 2 && found.second && found.second->id == "C", "checkItem C -> pos 2");
    auto missing = q.checkItem(c, "Z");
    CHECK(missing.first == -1 && !missing.second.has_value(), "checkItem Z -> (-1,nullopt)");

    // getNext() pops current and returns the new head.
    auto nxt = q.getNext(c);
    CHECK(nxt && nxt->id == "B", "getNext pops A -> B");
    CHECK(q.size(c) == 2, "size 2 after getNext");

    // replaceCurrent overwrites the head.
    CHECK(q.replaceCurrent(c, mk("B2")), "replaceCurrent ok");
    CHECK(q.getCurrent(c)->id == "B2", "current now B2");
    CHECK(!q.replaceCurrent(777, mk("X")), "replaceCurrent empty chat -> false");

    // removeCurrent pops only the head.
    q.removeCurrent(c);
    CHECK(q.getCurrent(c)->id == "C", "removeCurrent -> C");
    CHECK(q.size(c) == 1, "size 1");

    // getNext on the last item empties and returns nullopt.
    CHECK(!q.getNext(c).has_value(), "getNext last -> nullopt");
    CHECK(q.empty(c), "empty after draining");

    // forceAdd replaces the current head; size stays when one was playing.
    q.add(c, mk("D"));
    q.add(c, mk("E"));
    q.forceAdd(c, mk("F"));  // remove_current(D) + appendleft(F) -> [F, E]
    auto fq = q.getQueue(c);
    CHECK(fq.size() == 2 && fq[0].id == "F" && fq[1].id == "E", "forceAdd -> [F,E]");

    // forceAdd with removeAt erases the element that lands at that index.
    q.clear(c);
    q.add(c, mk("G"));
    q.add(c, mk("H"));
    q.add(c, mk("I"));                 // [G,H,I]
    q.forceAdd(c, mk("J"), /*removeAt=*/2);  // remove G, prepend J -> [J,H,I] then erase idx2 -> [J,H]
    auto rq = q.getQueue(c);
    CHECK(rq.size() == 2 && rq[0].id == "J" && rq[1].id == "H", "forceAdd removeAt=2 -> [J,H]");

    // clear empties the chat.
    q.clear(c);
    CHECK(q.empty(c), "clear -> empty");

    // Per-chat isolation.
    q.add(10, mk("X"));
    q.add(20, mk("Y"));
    CHECK(q.getCurrent(10)->id == "X" && q.getCurrent(20)->id == "Y", "chats isolated");
    CHECK(q.size(10) == 1 && q.size(20) == 1, "isolated sizes");
}

// ---------------------------------------------------------------------------
// CallManager
// ---------------------------------------------------------------------------
struct Recorder {
    std::vector<std::pair<std::int64_t, std::string>>          nowPlaying;  // (chat, id)
    std::vector<std::pair<std::int64_t, CallManager::Notice>>  notices;     // (chat, notice)
    std::vector<std::pair<std::int64_t, std::int64_t>>         deletes;     // (chat, msgId)
    std::vector<std::pair<std::string, bool>>                  downloads;   // (id, video)
    std::set<std::string>                                      downloadFail;
    std::int64_t                                               nextMsgId = 900;

    int notice(std::int64_t chat, CallManager::Notice n) const {
        int c = 0;
        for (const auto& p : notices)
            if (p.first == chat && p.second == n) ++c;
        return c;
    }
    int played(std::int64_t chat, const std::string& id) const {
        int c = 0;
        for (const auto& p : nowPlaying)
            if (p.first == chat && p.second == id) ++c;
        return c;
    }
    bool everPlayed(std::int64_t chat, const std::string& id) const { return played(chat, id) > 0; }
    bool deleted(std::int64_t chat, std::int64_t msgId) const {
        for (const auto& p : deletes)
            if (p.first == chat && p.second == msgId) return true;
        return false;
    }
    bool downloaded(const std::string& id) const {
        for (const auto& p : downloads)
            if (p.first == id) return true;
        return false;
    }
};

static void testCallManager() {
    FakeVoiceTransport fake;
    Queue queue;
    CacheManager cache;
    CallManager mgr(fake, queue, cache);

    Recorder rec;
    CallManager::Callbacks cb;
    cb.download = [&](const std::string& id, bool video) -> std::optional<std::string> {
        rec.downloads.push_back({id, video});
        if (rec.downloadFail.count(id))
            return std::nullopt;
        return std::string("/dl/") + id + (video ? ".mp4" : ".webm");
    };
    cb.onNowPlaying = [&](std::int64_t chat, const MediaItem& m) -> std::int64_t {
        rec.nowPlaying.push_back({chat, m.id});
        return rec.nextMsgId++;  // unique, nonzero
    };
    cb.onNotice = [&](std::int64_t chat, CallManager::Notice n) { rec.notices.push_back({chat, n}); };
    cb.onDeleteMessage = [&](std::int64_t chat, std::int64_t mid) { rec.deletes.push_back({chat, mid}); };
    mgr.setCallbacks(cb);

    using N = CallManager::Notice;
    using O = CallManager::PlayOutcome;

    // --- A: play on an idle chat starts immediately ---
    {
        const std::int64_t chat = 101;
        fake.playResult = PlayResult::Ok;
        auto d = mgr.play(chat, mk("A", "/dl/A.webm"));
        CHECK(d.outcome == O::StartedNow && d.position == 0, "A: StartedNow @ pos 0");
        CHECK(fake.state(chat).playCount == 1, "A: transport.play called once");
        CHECK(cache.isActiveCall(chat), "A: call marked active");
        CHECK(cache.isPlaying(chat), "A: playing (not paused)");
        CHECK(rec.everPlayed(chat, "A"), "A: now-playing card shown");
        auto curA = queue.getCurrent(chat);
        CHECK(curA && curA->time == 1, "A: current.time persisted == 1");
        CHECK(curA && curA->message_id != 0, "A: current.message_id persisted");

        // --- B: enqueue while active ---
        auto d2 = mgr.play(chat, mk("B", "/dl/B.webm"));
        CHECK(d2.outcome == O::Queued && d2.position == 1, "B: Queued @ pos 1");
        CHECK(fake.state(chat).playCount == 1, "B: transport.play NOT called again");
        CHECK(queue.size(chat) == 2, "B: queue size 2");

        // --- C: force replaces current and starts now ---
        auto d3 = mgr.play(chat, mk("C", "/dl/C.webm"), /*force=*/true);
        CHECK(d3.outcome == O::StartedNow, "C: force StartedNow");
        CHECK(queue.getCurrent(chat)->id == "C", "C: current == C (forced to front)");
        CHECK(queue.size(chat) == 2, "C: size still 2 (replaced current)");

        // --- D: pause ---
        CHECK(mgr.pause(chat) == true, "D: pause returns true");
        CHECK(!cache.isPlaying(chat) && cache.isActiveCall(chat), "D: paused but still active");

        // --- E: resume ---
        CHECK(mgr.resume(chat) == true, "E: resume returns true");
        CHECK(cache.isPlaying(chat), "E: playing again");

        // --- F: pause failure stops the chat ---
        fake.pauseOk = false;
        CHECK(mgr.pause(chat) == false, "F: pause returns false on engine failure");
        CHECK(!cache.isActiveCall(chat), "F: chat stopped (call removed)");
        CHECK(queue.empty(chat), "F: queue cleared by stop");
        CHECK(fake.state(chat).stopCount >= 1, "F: transport.stop called");
        fake.pauseOk = true;
    }

    // --- G: stream-end advances, then stops when queue empties ---
    {
        const std::int64_t chat = 102;
        fake.playResult = PlayResult::Ok;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        mgr.play(chat, mk("B", "/dl/B.webm"));  // queued
        CHECK(queue.size(chat) == 2, "G: two queued");
        fake.fireStreamEnd(chat);  // A ends -> advance to B
        CHECK(rec.everPlayed(chat, "B"), "G: advanced to B on stream end");
        CHECK(queue.getCurrent(chat)->id == "B" && queue.size(chat) == 1, "G: B is current");
        fake.fireStreamEnd(chat);  // B ends -> queue empty -> stop
        CHECK(!cache.isActiveCall(chat), "G: stopped after last track");
        CHECK(queue.empty(chat), "G: queue empty");
    }

    // --- H: loop replays N times, then advances/stops ---
    {
        const std::int64_t chat = 103;
        fake.playResult = PlayResult::Ok;
        mgr.play(chat, mk("A", "/dl/A.webm"));   // now-playing #1
        cache.setLoop(chat, 2);
        fake.fireStreamEnd(chat);  // loop 2 -> 1, replay  (#2)
        fake.fireStreamEnd(chat);  // loop 1 -> 0, replay  (#3)
        CHECK(cache.getLoop(chat) == 0, "H: loop drained to 0");
        CHECK(rec.played(chat, "A") == 3, "H: A played 3x (initial + 2 loops)");
        fake.fireStreamEnd(chat);  // no loop, queue has only A -> pop -> empty -> stop
        CHECK(!cache.isActiveCall(chat), "H: stopped after loop exhausted");
    }

    // --- I: download-on-demand for the next track ---
    {
        const std::int64_t chat = 104;
        fake.playResult = PlayResult::Ok;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        mgr.play(chat, mk("B", /*path=*/""));  // queued, no file yet
        fake.fireStreamEnd(chat);              // A ends -> B needs download
        CHECK(rec.downloaded("B"), "I: downloaded B on demand");
        CHECK(rec.everPlayed(chat, "B"), "I: played B after download");
        CHECK(queue.getCurrent(chat)->file_path == "/dl/B.webm", "I: current.file_path filled");
    }

    // --- J: failed download skips to the following track ---
    {
        const std::int64_t chat = 105;
        fake.playResult = PlayResult::Ok;
        rec.downloadFail.insert("B");
        mgr.play(chat, mk("A", "/dl/A.webm"));
        mgr.play(chat, mk("B", ""));            // will fail to download
        mgr.play(chat, mk("C", "/dl/C.webm"));  // fallback
        fake.fireStreamEnd(chat);               // A ends -> B fails -> skip -> C
        CHECK(!rec.everPlayed(chat, "B"), "J: B skipped (never played)");
        CHECK(rec.everPlayed(chat, "C"), "J: advanced to C");
        CHECK(queue.getCurrent(chat)->id == "C", "J: current == C");
        CHECK(rec.notice(chat, N::ErrorNoFile) >= 1, "J: no-file notice emitted");
        rec.downloadFail.clear();
    }

    // --- K: NoActiveGroupCall -> stop + no-call notice ---
    {
        const std::int64_t chat = 106;
        fake.playResult = PlayResult::NoActiveGroupCall;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        CHECK(!cache.isActiveCall(chat), "K: not active after NoActiveGroupCall");
        CHECK(queue.empty(chat), "K: queue cleared");
        CHECK(rec.notice(chat, N::ErrorNoCall) >= 1, "K: no-call notice");
        fake.playResult = PlayResult::Ok;
    }

    // --- L: NoAudioSource -> notice + advance (drains then stops) ---
    {
        const std::int64_t chat = 107;
        fake.playResult = PlayResult::NoAudioSource;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        CHECK(rec.notice(chat, N::ErrorNoAudio) >= 1, "L: no-audio notice");
        CHECK(!cache.isActiveCall(chat), "L: stopped after draining");
        fake.playResult = PlayResult::Ok;
    }

    // --- N: stale card on the next head is deleted ---
    {
        const std::int64_t chat = 108;
        fake.playResult = PlayResult::Ok;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        Track b = mk("B", "/dl/B.webm");
        b.message_id = 555;                 // pretend B already carried a card
        mgr.play(chat, b);                  // queued with message_id 555
        fake.fireStreamEnd(chat);           // A ends -> B becomes head, its 555 gets deleted
        CHECK(rec.deleted(chat, 555), "N: stale card 555 deleted");
        CHECK(rec.everPlayed(chat, "B"), "N: B played after cleanup");
    }

    // --- M: ping passes through the transport ---
    {
        fake.pingValue = 42.0;
        CHECK(mgr.ping() == 42.0, "M: ping == 42.0");
    }

    // --- O: external call-closed stops the chat ---
    {
        const std::int64_t chat = 110;
        fake.playResult = PlayResult::Ok;
        mgr.play(chat, mk("A", "/dl/A.webm"));
        CHECK(cache.isActiveCall(chat), "O: active before close");
        fake.fireCallClosed(chat);  // kicked / left / voice chat closed
        CHECK(!cache.isActiveCall(chat), "O: stopped on call closed");
        CHECK(queue.empty(chat), "O: queue cleared on close");
    }
}

int main() {
    std::cout << "== Phase 5 voice/queue tests ==\n";
    testQueue();
    testCallManager();

    std::cout << "checks run: " << g_checks << ", failures: " << g_failures << "\n";
    if (g_failures == 0) {
        std::cout << "ALL VOICE TESTS PASSED\n";
        return 0;
    }
    std::cout << "VOICE TESTS FAILED\n";
    return 1;
}
