// SPDX-License-Identifier: GPL-3.0-or-later
// The host's rules that need nothing of PCSX2: when the pointer hides over a
// game's picture (host/pointer_hiding.h, docs/adr/0068).
#include "testing.h"

#include "pointer_hiding.h"

#include <chrono>

using ps2n::host::window::PointerHiding;
using namespace std::chrono_literals;

namespace {

const PointerHiding::Clock::time_point t0{};

} // namespace

TEST_CASE(pointer_never_hides_in_a_window) {
    PointerHiding p;
    p.SetFullscreen(false, t0);
    CHECK(!p.Until());
    CHECK(p.Moved(10, 10, t0));
    CHECK(!p.Until());
    p.Tick(false, t0 + 60s);
    CHECK(!p.Hidden());
    p.Pressed(false);
    CHECK(!p.Hidden());
}

TEST_CASE(pointer_hides_once_still_in_full_screen) {
    PointerHiding p;
    p.SetFullscreen(true, t0);
    CHECK(p.Until() == t0 + PointerHiding::kStill);
    CHECK(PointerHiding::kStill == 2s);
    p.Tick(false, t0 + 1999ms);
    CHECK(!p.Hidden());
    p.Tick(false, t0 + 2s);
    CHECK(p.Hidden());
    CHECK(!p.Until());
}

TEST_CASE(pointer_shows_when_it_moves_and_is_still_from_then) {
    PointerHiding p;
    p.SetFullscreen(true, t0);
    p.Tick(false, t0 + 2s);
    CHECK(p.Hidden());
    CHECK(p.Moved(100, 100, t0 + 5s));
    CHECK(!p.Hidden());
    CHECK(p.Until() == t0 + 7s);
    // Moved again before its time is up: two seconds from the last move.
    CHECK(p.Moved(101, 100, t0 + 6500ms));
    p.Tick(false, t0 + 7s);
    CHECK(!p.Hidden());
    p.Tick(false, t0 + 8499ms);
    CHECK(!p.Hidden());
    p.Tick(false, t0 + 8500ms);
    CHECK(p.Hidden());
}

TEST_CASE(pointer_where_it_already_was_has_not_moved) {
    // The window moved under it, or the same place was said twice: the same
    // place on the screen.
    PointerHiding p;
    p.SetFullscreen(true, t0);
    CHECK(p.Moved(50, 60, t0));
    p.Tick(false, t0 + 2s);
    CHECK(p.Hidden());
    CHECK(!p.Moved(50, 60, t0 + 3s));
    CHECK(p.Hidden());
    CHECK(!p.Until());
    CHECK(p.Moved(50, 61, t0 + 4s));
    CHECK(!p.Hidden());
}

TEST_CASE(pointer_hides_at_once_at_a_click_in_full_screen) {
    PointerHiding p;
    p.SetFullscreen(true, t0);
    CHECK(p.Moved(1, 1, t0 + 500ms));
    p.Pressed(false);
    CHECK(p.Hidden());
    CHECK(!p.Until());
    CHECK(p.Moved(2, 1, t0 + 1s));
    CHECK(!p.Hidden());
    CHECK(p.Until() == t0 + 3s);
}

TEST_CASE(pointer_used_is_never_hidden) {
    // The menu open: neither a click nor the time hides it, which waits as
    // long again, and hides when its time is up after the menu has closed.
    PointerHiding p;
    p.SetFullscreen(true, t0);
    CHECK(p.Moved(5, 5, t0));
    p.Pressed(true);
    CHECK(!p.Hidden());
    p.Tick(true, t0 + 2s);
    CHECK(!p.Hidden());
    CHECK(p.Until() == t0 + 4s);
    p.Tick(false, t0 + 3s);
    CHECK(!p.Hidden());
    p.Tick(false, t0 + 4s);
    CHECK(p.Hidden());
}

TEST_CASE(pointer_hidden_stays_hidden_when_used_until_it_moves) {
    // Hidden when the menu opens: it stays hidden until it moves.
    PointerHiding p;
    p.SetFullscreen(true, t0);
    p.Tick(false, t0 + 2s);
    CHECK(p.Hidden());
    p.Tick(true, t0 + 10s);
    p.Pressed(true);
    CHECK(p.Hidden());
    CHECK(p.Moved(9, 9, t0 + 11s));
    CHECK(!p.Hidden());
}

TEST_CASE(pointer_shows_when_the_window_stops_covering_the_screen) {
    PointerHiding p;
    p.SetFullscreen(true, t0);
    p.Tick(false, t0 + 2s);
    CHECK(p.Hidden());
    p.SetFullscreen(false, t0 + 3s);
    CHECK(!p.Hidden());
    CHECK(!p.Until());
    CHECK(p.Moved(3, 3, t0 + 4s));
    CHECK(!p.Until());
    // Covering it again, still from then.
    p.SetFullscreen(true, t0 + 5s);
    CHECK(!p.Hidden());
    CHECK(p.Until() == t0 + 7s);
}

int main(int argc, char** argv) { return ::testing::runAll(argc, argv); }
