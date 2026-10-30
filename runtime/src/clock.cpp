#include "clock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace ps2 {

namespace {

struct Event {
    uint64_t when;
    uint64_t id;
    EventFn fn;
    uint64_t param;
};

bool g_virtual = false;
std::atomic<bool> g_noThrottle{false};
uint64_t g_virtualNow = 0;
uint64_t g_skipped = 0;    // cycles skipped while idle in no-throttle mode
std::chrono::steady_clock::time_point g_start;
std::chrono::steady_clock::time_point g_pausedAt;
bool g_paused = false;
std::vector<Event> g_events;   // small; kept sorted by time
uint64_t g_nextId = 1;
uint64_t g_nextWhen = UINT64_MAX;
// A real-time wait, which clockWake() can end early.
std::mutex g_waitMutex;
std::condition_variable g_waitEnded;
bool g_wake = false;

uint64_t realCycles() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_start).count();
    return static_cast<uint64_t>(static_cast<double>(ns) * (static_cast<double>(kEeClockHz) / 1e9));
}

} // namespace

void clockInit(bool virtualClock, bool noThrottle) {
    g_virtual = virtualClock;
    g_noThrottle = noThrottle;
    g_virtualNow = 0;
    g_skipped = 0;
    g_start = std::chrono::steady_clock::now();
    g_paused = false;
    g_events.clear();
    g_nextWhen = UINT64_MAX;
}

uint64_t nowCycles() { return g_virtual ? g_virtualNow : realCycles() + g_skipped; }

int64_t rtcTime() {
    if (g_virtual) return 994248000 + static_cast<int64_t>(g_virtualNow / kEeClockHz);
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void clockAdvance(uint64_t cycles) {
    if (g_virtual) g_virtualNow += cycles;
}

void clockWaitUntil(uint64_t target) {
    const uint64_t now = nowCycles();
    if (target <= now) return;
    if (g_virtual) {
        g_virtualNow = target;
    } else if (g_noThrottle) {
        g_skipped += target - now;
    } else {
        const auto ns = static_cast<int64_t>(static_cast<double>(target - now) * (1e9 / static_cast<double>(kEeClockHz)));
        std::unique_lock<std::mutex> lock(g_waitMutex);
        g_waitEnded.wait_for(lock, std::chrono::nanoseconds(ns), [] { return g_wake; });
        g_wake = false;
    }
}

void clockWake() {
    {
        const std::lock_guard<std::mutex> lock(g_waitMutex);
        g_wake = true;
    }
    g_waitEnded.notify_one();
}

void clockPause() {
    if (g_paused) return;
    g_paused = true;
    g_pausedAt = std::chrono::steady_clock::now();
}

void clockResume() {
    if (!g_paused) return;
    g_paused = false;
    g_start += std::chrono::steady_clock::now() - g_pausedAt;
}

void clockSetNoThrottle(bool on) { g_noThrottle = on; }
bool clockNoThrottle() { return g_noThrottle; }

uint64_t scheduleEvent(uint64_t when, EventFn fn, uint64_t param) {
    const uint64_t id = g_nextId++;
    auto it = g_events.begin();
    while (it != g_events.end() && it->when <= when) ++it;
    g_events.insert(it, Event{when, id, fn, param});
    g_nextWhen = g_events.front().when;
    return id;
}

void cancelEvent(uint64_t id) {
    for (auto it = g_events.begin(); it != g_events.end(); ++it) {
        if (it->id == id) {
            g_events.erase(it);
            break;
        }
    }
    g_nextWhen = g_events.empty() ? UINT64_MAX : g_events.front().when;
}

uint64_t nextEventTime() { return g_nextWhen; }

void runDueEvents() {
    if (g_nextWhen == UINT64_MAX) return;
    uint64_t now = nowCycles();
    while (!g_events.empty() && g_events.front().when <= now) {
        const Event e = g_events.front();
        g_events.erase(g_events.begin());
        g_nextWhen = g_events.empty() ? UINT64_MAX : g_events.front().when;
        e.fn(e.param);
        now = nowCycles();
    }
}

} // namespace ps2
