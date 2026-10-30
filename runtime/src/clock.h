// Emulated time and the event queue.
//
// Time is counted in EE cycles (294.912 MHz). In the default mode it follows
// the host's monotonic clock, so games run at their real speed; with
// --virtual-clock it only advances as guest code runs (a fixed amount per
// safepoint and syscall) and jumps straight to the next event when every
// thread is idle, which makes runs reproducible.
#pragma once

#include <cstdint>

namespace ps2 {

constexpr uint64_t kEeClockHz = 294912000;
constexpr uint64_t kBusClockHz = kEeClockHz / 2;

void clockInit(bool virtualClock, bool noThrottle);
uint64_t nowCycles();
// The console's real-time clock, in seconds since the Unix epoch (UTC): the
// host's time, or with --virtual-clock 2001-07-04 12:00 plus emulated time.
int64_t rtcTime();
// Virtual mode: guest work done since the last call (no-op in real-time mode).
void clockAdvance(uint64_t cycles);
// Idle: wait (or skip, in virtual/no-throttle mode) until `target`.
void clockWaitUntil(uint64_t target);
// Any thread: ends the current or next real-time wait early, for an event
// from another thread (the GS reaching a FINISH) that the guest must see.
void clockWake();
// The guest thread stopped (pause, settings overlay): real time spent paused
// does not count, so the game carries on where it stopped.
void clockPause();
void clockResume();
// Turbo (fast forward): no-throttle mode, switchable from any thread.
void clockSetNoThrottle(bool on);
bool clockNoThrottle();

using EventFn = void (*)(uint64_t param);
// One-shot event at absolute cycle `when`. Returns an id for cancelEvent.
uint64_t scheduleEvent(uint64_t when, EventFn fn, uint64_t param);
void cancelEvent(uint64_t id);
uint64_t nextEventTime();   // UINT64_MAX when the queue is empty
// Fires every event that is due. Cheap when nothing is due.
void runDueEvents();

} // namespace ps2
