// SPDX-License-Identifier: GPL-3.0-or-later
//
// When the pointer is hidden over the picture of a game played in its window
// (docs/adr/0068). While the window covers its screen, the pointer hides once
// it has been still for two seconds, and at once when one of its buttons is
// pressed over the picture; it shows again as soon as it moves. In a window it
// is never hidden, nor while something over the picture is used with it (the
// menu over the game, host/menu.h), where it waits as long again.
//
// The mouse is no game's control here: the window passes it on to the menu
// alone (host/window.h), whose Controls set keys and controllers, never the
// mouse. Hiding it takes nothing from a game: a click and a move reach what
// they reached before.
//
// The rule alone, without a platform's window, so that the window code of
// each platform follows it alike (host/window.cpp), and a unit test holds it
// (tests/unit/host_tests.cpp).

#pragma once

#include <chrono>
#include <optional>

namespace ps2n::host::window
{
	class PointerHiding
	{
	public:
		using Clock = std::chrono::steady_clock;

		/// How long the pointer stays still over the picture before it hides.
		static constexpr Clock::duration kStill = std::chrono::seconds(2);

		/// The window has come to cover its screen, or has stopped covering it.
		/// The pointer shows; covering the screen, it hides once it has been
		/// still for kStill.
		void SetFullscreen(bool fullscreen, Clock::time_point now)
		{
			m_fullscreen = fullscreen;
			m_hidden = false;
			m_until.reset();
			if (fullscreen)
				m_until = now + kStill;
		}

		/// The pointer is at (x, y) of the screen. Moved, it shows, and is still
		/// from now on; where it already was, nothing changes: the window moved
		/// under it, or the same place was said twice. True when it moved.
		bool Moved(int x, int y, Clock::time_point now)
		{
			if (m_placed && x == m_x && y == m_y)
				return false;
			m_placed = true;
			m_x = x;
			m_y = y;
			m_hidden = false;
			if (m_fullscreen)
				m_until = now + kStill;
			return true;
		}

		/// One of its buttons went down over the picture. `used`: whether
		/// something over the picture is used with the pointer now (the menu).
		void Pressed(bool used)
		{
			if (!m_fullscreen || used)
				return;
			m_hidden = true;
			m_until.reset();
		}

		/// Time has passed. The pointer still for kStill hides, unless it is
		/// used then (`used`, as for Pressed): then it waits as long again.
		void Tick(bool used, Clock::time_point now)
		{
			if (!m_until || now < *m_until)
				return;
			if (used)
			{
				m_until = now + kStill;
				return;
			}
			m_hidden = true;
			m_until.reset();
		}

		/// Whether the pointer is hidden over the picture now.
		bool Hidden() const
		{
			return m_hidden;
		}

		/// When Tick is next wanted, if it is.
		std::optional<Clock::time_point> Until() const
		{
			return m_until;
		}

	private:
		bool m_fullscreen = false;
		bool m_hidden = false;
		bool m_placed = false; // whether a place has been said
		int m_x = 0;
		int m_y = 0;
		std::optional<Clock::time_point> m_until; // when it hides, if it stays still
	};
} // namespace ps2n::host::window
