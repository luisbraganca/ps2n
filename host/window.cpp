// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window a game is played in: on Windows a Win32 window, on Linux an X11
// one (step P4.7); on other platforms Available() says no and the host stays
// without a window.
//
// The Windows part follows GSRunner::CreatePlatformWindow and its message pump
// in pcsx2-gsrunner/Main.cpp of PCSX2 v2.9.189, Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+. Changes for PS2Native: the window is PS2Native's (its class,
// its icon, its title), it follows the display's scaling, it can cover the
// screen, and it passes on the keyboard, its size and its closing. And a
// notice of PS2Native's own can show over the picture (docs/adr/0035). The
// Linux part is PS2Native's, the same window drawn as PCSX2's renderers draw
// into an X11 one (common/WindowInfo.h).

#include "window.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <type_traits>

namespace ps2n::host::window
{
	namespace
	{
		// The names are the ones PCSX2's own frontend uses for the same keys, so
		// that the core's default bindings ("Keyboard/Return", "Keyboard/Up")
		// mean here what they mean there.
		constexpr struct
		{
			uint32_t code;
			const char* name;
		} kNamedKeys[] = {
			{KeyReturn, "Return"}, {KeyBackspace, "Backspace"}, {KeyEscape, "Escape"}, {KeySpace, "Space"}, {KeyTab, "Tab"},
			{KeyUp, "Up"}, {KeyDown, "Down"}, {KeyLeft, "Left"}, {KeyRight, "Right"}, {KeyShift, "Shift"},
			{KeyControl, "Control"}, {KeyAlt, "Alt"}, {KeyInsert, "Insert"}, {KeyDelete, "Delete"}, {KeyHome, "Home"},
			{KeyEnd, "End"}, {KeyPageUp, "PageUp"}, {KeyPageDown, "PageDown"}, {KeyF1, "F1"}, {KeyF2, "F2"},
			{KeyF3, "F3"}, {KeyF4, "F4"}, {KeyF5, "F5"}, {KeyF6, "F6"}, {KeyF7, "F7"},
			{KeyF8, "F8"}, {KeyF9, "F9"}, {KeyF10, "F10"}, {KeyF11, "F11"}, {KeyF12, "F12"},
		};

		// "A" to "Z" and "0" to "9", each ending in a zero byte.
		constexpr char kCharacterNames[] = "A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U\0V\0W\0X\0Y\0Z\0"
										   "0\0001\0002\0003\0004\0005\0006\0007\0008\0009";

		bool IsCharacterKey(uint32_t code)
		{
			return (code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9');
		}
	} // namespace

	std::optional<uint32_t> KeyFromName(std::string_view name)
	{
		if (name.size() == 1)
		{
			const uint32_t code = static_cast<unsigned char>(name[0]);
			if (IsCharacterKey(code))
				return code;
			if (code >= 'a' && code <= 'z')
				return code - 'a' + 'A';
			return std::nullopt;
		}

		for (const auto& key : kNamedKeys)
		{
			if (name == key.name)
				return key.code;
		}
		return std::nullopt;
	}

	const char* NameOfKey(uint32_t code)
	{
		if (code >= 'A' && code <= 'Z')
			return &kCharacterNames[(code - 'A') * 2];
		if (code >= '0' && code <= '9')
			return &kCharacterNames[(26 + code - '0') * 2];

		for (const auto& key : kNamedKeys)
		{
			if (code == key.code)
				return key.name;
		}
		return nullptr;
	}
} // namespace ps2n::host::window

#ifdef _WIN32

#include "common/RedtapeWindows.h"
#include "common/StringUtil.h"

namespace ps2n::host::window
{
	namespace
	{
		constexpr LPCWSTR kWindowClass = L"PS2NativeGame";
		constexpr LPCWSTR kNoticeClass = L"PS2NativeNotice";
		constexpr UINT kMessageSetTitle = WM_APP + 1; // lParam: a std::wstring made with new, which the window deletes
		constexpr UINT kMessageSetFullscreen = WM_APP + 2; // wParam: whether to cover the screen
		constexpr UINT kMessageShowNotice = WM_APP + 3; // lParam: a Notice made with new, which the window deletes

		// A notice over the picture (ShowNotice).
		struct Notice
		{
			std::wstring title;
			std::wstring text;
			UINT seconds = 0;
		};

		// The brand's colours (assets/branding/README.md): navy behind, the
		// emblem's blue down the side, white text.
		constexpr COLORREF kNoticeNavy = RGB(0x0A, 0x10, 0x22);
		constexpr COLORREF kNoticeBlue = RGB(0x00, 0x60, 0xFC);
		constexpr COLORREF kNoticeWhite = RGB(0xFF, 0xFF, 0xFF);

		// Written on the main thread before the machine starts and after it has
		// stopped; read by the core's threads in between.
		std::atomic<HWND> s_window{nullptr};
		std::atomic<bool> s_fullscreen{false};
		Listener s_listener{};

		// Main thread only: where the window was before it covered the screen.
		LONG s_windowed_style = 0;
		WINDOWPLACEMENT s_windowed_placement = {sizeof(WINDOWPLACEMENT)};

		// Main thread only: the notice's window, made the first time one is
		// shown and owned by the game's, so that it stays above it; and what it
		// shows.
		HWND s_notice = nullptr;
		Notice s_notice_shown;

		std::optional<uint32_t> KeyFromVirtualKey(WPARAM vk)
		{
			if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
				return static_cast<uint32_t>(vk);
			if (vk >= VK_F1 && vk <= VK_F12)
				return static_cast<uint32_t>(KeyF1 + (vk - VK_F1));

			switch (vk)
			{
				case VK_RETURN: return KeyReturn;
				case VK_BACK: return KeyBackspace;
				case VK_ESCAPE: return KeyEscape;
				case VK_SPACE: return KeySpace;
				case VK_TAB: return KeyTab;
				case VK_UP: return KeyUp;
				case VK_DOWN: return KeyDown;
				case VK_LEFT: return KeyLeft;
				case VK_RIGHT: return KeyRight;
				case VK_SHIFT: return KeyShift;
				case VK_CONTROL: return KeyControl;
				case VK_MENU: return KeyAlt;
				case VK_INSERT: return KeyInsert;
				case VK_DELETE: return KeyDelete;
				case VK_HOME: return KeyHome;
				case VK_END: return KeyEnd;
				case VK_PRIOR: return KeyPageUp;
				case VK_NEXT: return KeyPageDown;
				default: return std::nullopt;
			}
		}

		float ScaleOf(HWND hwnd)
		{
			const UINT dpi = GetDpiForWindow(hwnd);
			return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
		}

		// The way to cover a screen without changing its mode: take the frame
		// off and put the window over the whole monitor it is on.
		void ApplyFullscreen(HWND hwnd, bool enabled)
		{
			if (enabled == s_fullscreen.load())
				return;

			if (enabled)
			{
				MONITORINFO monitor = {sizeof(MONITORINFO)};
				s_windowed_style = GetWindowLongW(hwnd, GWL_STYLE);
				if (!GetWindowPlacement(hwnd, &s_windowed_placement) ||
					!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
				{
					return;
				}
				SetWindowLongW(hwnd, GWL_STYLE, s_windowed_style & ~WS_OVERLAPPEDWINDOW);
				SetWindowPos(hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
					monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
					SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			}
			else
			{
				SetWindowLongW(hwnd, GWL_STYLE, s_windowed_style);
				SetWindowPlacement(hwnd, &s_windowed_placement);
				SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			}
			s_fullscreen.store(enabled);
		}

		// The notice's measures, in pixels at the game window's scaling.
		int Scaled(int pixels, float scale)
		{
			return static_cast<int>(std::lround(static_cast<float>(pixels) * scale));
		}

		// Its fonts: the title semi-bold above the text.
		HFONT NoticeFont(float scale, bool title)
		{
			return CreateFontW(-Scaled(title ? 17 : 15, scale), 0, 0, 0, title ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
		}

		// The notice sized to what it says, in the top left corner of the
		// picture, a margin in.
		void PlaceNotice(HWND owner)
		{
			if (!s_notice)
				return;
			const float scale = ScaleOf(owner);
			const HDC dc = GetDC(s_notice);
			const HFONT title_font = NoticeFont(scale, true);
			const HFONT text_font = NoticeFont(scale, false);
			RECT title = {}, text = {};
			const HGDIOBJ old = SelectObject(dc, title_font);
			DrawTextW(dc, s_notice_shown.title.c_str(), -1, &title, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, text_font);
			DrawTextW(dc, s_notice_shown.text.c_str(), -1, &text, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, old);
			DeleteObject(title_font);
			DeleteObject(text_font);
			ReleaseDC(s_notice, dc);

			const int width = Scaled(4 + 14 + 16, scale) + std::max(title.right, text.right);
			const int height = Scaled(12 + 4 + 12, scale) + title.bottom + text.bottom;
			POINT corner = {Scaled(16, scale), Scaled(16, scale)};
			ClientToScreen(owner, &corner);
			SetWindowPos(s_notice, nullptr, corner.x, corner.y, width, height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
		}

		LRESULT CALLBACK NoticeProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
				case WM_NCHITTEST:
					// Clicks go through to the game.
					return HTTRANSPARENT;

				case WM_ERASEBKGND:
					return 1;

				case WM_PAINT:
				{
					PAINTSTRUCT paint;
					const HDC dc = BeginPaint(hwnd, &paint);
					const HWND owner = GetWindow(hwnd, GW_OWNER);
					const float scale = ScaleOf(owner ? owner : hwnd);
					RECT client = {};
					GetClientRect(hwnd, &client);
					const HBRUSH navy = CreateSolidBrush(kNoticeNavy);
					const HBRUSH blue = CreateSolidBrush(kNoticeBlue);
					FillRect(dc, &client, navy);
					const RECT side = {0, 0, Scaled(4, scale), client.bottom};
					FillRect(dc, &side, blue);
					DeleteObject(navy);
					DeleteObject(blue);

					SetBkMode(dc, TRANSPARENT);
					SetTextColor(dc, kNoticeWhite);
					const HFONT title_font = NoticeFont(scale, true);
					const HFONT text_font = NoticeFont(scale, false);
					const HGDIOBJ old = SelectObject(dc, title_font);
					RECT line = {Scaled(4 + 14, scale), Scaled(12, scale), client.right, client.bottom};
					const int title_height = DrawTextW(dc, s_notice_shown.title.c_str(), -1, &line, DT_SINGLELINE | DT_NOPREFIX | DT_TOP);
					SelectObject(dc, text_font);
					line.top += title_height + Scaled(4, scale);
					DrawTextW(dc, s_notice_shown.text.c_str(), -1, &line, DT_SINGLELINE | DT_NOPREFIX | DT_TOP);
					SelectObject(dc, old);
					DeleteObject(title_font);
					DeleteObject(text_font);
					EndPaint(hwnd, &paint);
					return 0;
				}

				case WM_TIMER:
					// Its time is up.
					KillTimer(hwnd, wParam);
					ShowWindow(hwnd, SW_HIDE);
					return 0;

				default:
					break;
			}
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}

		void ShowNoticeNow(HWND owner, Notice notice)
		{
			if (!s_notice)
			{
				s_notice = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kNoticeClass, L"",
					WS_POPUP, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
				if (!s_notice)
					return;
				SetLayeredWindowAttributes(s_notice, 0, 235, LWA_ALPHA);
			}
			s_notice_shown = std::move(notice);
			PlaceNotice(owner);
			InvalidateRect(s_notice, nullptr, FALSE);
			ShowWindow(s_notice, SW_SHOWNOACTIVATE);
			SetTimer(s_notice, 1, s_notice_shown.seconds * 1000, nullptr);
		}

		LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
				case WM_CLOSE:
					// The runner stops the machine; the window goes when that is done.
					// The right to bring a window to the front goes with this one's,
					// for the window a game's folder compiles what the session met in
					// when it has ended (wizard/remake_main.cpp, docs/adr/0040), which
					// Windows would otherwise leave behind another.
					AllowSetForegroundWindow(ASFW_ANY);
					if (s_listener.close_requested)
						s_listener.close_requested();
					return 0;

				case WM_SIZE:
					if (wParam != SIZE_MINIMIZED && s_listener.resized)
						s_listener.resized(LOWORD(lParam), HIWORD(lParam), ScaleOf(hwnd));
					if (s_notice && IsWindowVisible(s_notice))
						PlaceNotice(hwnd);
					return 0;

				case WM_MOVE:
					if (s_notice && IsWindowVisible(s_notice))
						PlaceNotice(hwnd);
					return 0;

				case WM_DPICHANGED:
				{
					// Moved to a display with another scaling: take the size Windows suggests.
					const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
					SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
						suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
					return 0;
				}

				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
				{
					const bool repeat = (lParam & (1 << 30)) != 0;
					const bool alt = (lParam & (1 << 29)) != 0;
					if ((wParam == VK_RETURN && alt) || wParam == VK_F11)
					{
						if (!repeat)
							ApplyFullscreen(hwnd, !s_fullscreen.load());
						return 0;
					}
					if (!repeat && s_listener.key)
					{
						if (const std::optional<uint32_t> key = KeyFromVirtualKey(wParam))
							s_listener.key(*key, true);
					}
					// Alt+F4 and the like are Windows's to act on.
					return (message == WM_SYSKEYDOWN) ? DefWindowProcW(hwnd, message, wParam, lParam) : 0;
				}

				case WM_KEYUP:
				case WM_SYSKEYUP:
					if (s_listener.key)
					{
						if (const std::optional<uint32_t> key = KeyFromVirtualKey(wParam))
							s_listener.key(*key, false);
					}
					return (message == WM_SYSKEYUP) ? DefWindowProcW(hwnd, message, wParam, lParam) : 0;

				case WM_SYSCOMMAND:
					// Alt by itself would open the window's menu and hold the keyboard.
					if ((wParam & 0xFFF0) == SC_KEYMENU)
						return 0;
					break;

				case WM_KILLFOCUS:
					if (s_listener.focus_lost)
						s_listener.focus_lost();
					break;

				case WM_ERASEBKGND:
					// The renderer covers the whole window; erasing it first would flicker.
					return 1;

				case kMessageSetTitle:
				{
					std::wstring* title = reinterpret_cast<std::wstring*>(lParam);
					SetWindowTextW(hwnd, title->c_str());
					delete title;
					return 0;
				}

				case kMessageSetFullscreen:
					ApplyFullscreen(hwnd, wParam != 0);
					return 0;

				case kMessageShowNotice:
				{
					const std::unique_ptr<Notice> notice(reinterpret_cast<Notice*>(lParam));
					ShowNoticeNow(hwnd, std::move(*notice));
					return 0;
				}

				case kShotMessage:
					if (s_listener.shot_requested)
						s_listener.shot_requested(static_cast<uint32_t>(wParam));
					return 0;

				default:
					break;
			}
			return DefWindowProcW(hwnd, message, wParam, lParam);
		}
	} // namespace

	bool Available()
	{
		return true;
	}

	bool CreateHidden()
	{
		// The core's renderers on Windows need no window to draw nothing.
		return false;
	}

	void ServeUntil(const std::atomic<bool>& done)
	{
		// The runner serves the thread's messages itself (host/main.cpp).
	}

	bool Create(const std::string& title, uint32_t width, uint32_t height, const Listener& listener)
	{
		if (s_window.load())
			return false;

		// Without this Windows draws the window at 96 dpi and stretches it.
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

		const HINSTANCE instance = GetModuleHandleW(nullptr);

		// The icon is the executable's (assets/ps2n.rc, resource 1).
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(WNDCLASSEXW);
		wc.lpfnWndProc = WindowProcedure;
		wc.hInstance = instance;
		wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
			GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
			GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		wc.lpszClassName = kWindowClass;
		if (!RegisterClassExW(&wc))
			return false;

		// The notice's (ShowNotice).
		WNDCLASSEXW notice = {};
		notice.cbSize = sizeof(WNDCLASSEXW);
		notice.lpfnWndProc = NoticeProcedure;
		notice.hInstance = instance;
		notice.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		notice.lpszClassName = kNoticeClass;
		RegisterClassExW(&notice);

		s_listener = listener;

		const std::wstring wide_title = StringUtil::UTF8StringToWideString(title);
		const DWORD style = WS_OVERLAPPEDWINDOW;
		const HWND hwnd = CreateWindowExW(0, kWindowClass, wide_title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
		if (!hwnd)
		{
			UnregisterClassW(kWindowClass, instance);
			UnregisterClassW(kNoticeClass, instance);
			return false;
		}

		// The size asked for is that of the picture's area on a display at 100%:
		// scale it for the display the window came up on, and add the frame.
		const UINT dpi = GetDpiForWindow(hwnd);
		const int client_width = MulDiv(static_cast<int>(width), static_cast<int>(dpi), 96);
		const int client_height = MulDiv(static_cast<int>(height), static_cast<int>(dpi), 96);
		RECT rect = {0, 0, client_width, client_height};
		AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
		UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
		int x = 0, y = 0;

		// A window that would not fit on its screen covers the screen instead
		// (ADR 0025: a picture of 1920 by 1080 on a screen of that size). The
		// window Alt+Enter goes back to is the largest that fits, at the same
		// proportions, in the middle of the screen.
		bool cover = false;
		MONITORINFO monitor = {sizeof(MONITORINFO)};
		if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
		{
			const int work_width = monitor.rcWork.right - monitor.rcWork.left;
			const int work_height = monitor.rcWork.bottom - monitor.rcWork.top;
			if (rect.right - rect.left > work_width || rect.bottom - rect.top > work_height)
			{
				cover = true;
				RECT frame = {0, 0, 0, 0};
				AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
				const double scale = std::min(static_cast<double>(work_width - (frame.right - frame.left)) / client_width,
					static_cast<double>(work_height - (frame.bottom - frame.top)) / client_height);
				rect = {0, 0, std::max(1, static_cast<int>(client_width * scale)), std::max(1, static_cast<int>(client_height * scale))};
				AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
				x = monitor.rcWork.left + (work_width - (rect.right - rect.left)) / 2;
				y = monitor.rcWork.top + (work_height - (rect.bottom - rect.top)) / 2;
				flags &= ~SWP_NOMOVE;
			}
		}
		SetWindowPos(hwnd, nullptr, x, y, rect.right - rect.left, rect.bottom - rect.top, flags);

		s_window.store(hwnd);
		if (cover)
			ApplyFullscreen(hwnd, true);
		ShowWindow(hwnd, SW_SHOWNORMAL);
		SetForegroundWindow(hwnd);
		UpdateWindow(hwnd);
		return true;
	}

	void Destroy()
	{
		const HWND hwnd = s_window.exchange(nullptr);
		if (!hwnd)
			return;

		s_listener = {};
		if (s_notice)
		{
			DestroyWindow(s_notice);
			s_notice = nullptr;
		}
		DestroyWindow(hwnd);
		UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
		UnregisterClassW(kNoticeClass, GetModuleHandleW(nullptr));
		s_fullscreen.store(false);
	}

	WindowInfo GetInfo()
	{
		WindowInfo wi;
		const HWND hwnd = s_window.load();
		if (!hwnd)
			return wi; // surfaceless

		RECT client = {};
		GetClientRect(hwnd, &client);
		wi.type = WindowInfo::Type::Win32;
		wi.window_handle = hwnd;
		wi.surface_width = static_cast<u32>(client.right - client.left);
		wi.surface_height = static_cast<u32>(client.bottom - client.top);
		wi.surface_scale = ScaleOf(hwnd);
		return wi;
	}

	void SetTitle(const std::string& title)
	{
		// Posted, not sent: the main thread may be waiting for the thread that calls this.
		const HWND hwnd = s_window.load();
		if (!hwnd)
			return;

		std::wstring* wide = new std::wstring(StringUtil::UTF8StringToWideString(title));
		if (!PostMessageW(hwnd, kMessageSetTitle, 0, reinterpret_cast<LPARAM>(wide)))
			delete wide;
	}

	bool IsFullscreen()
	{
		return s_fullscreen.load();
	}

	void SetFullscreen(bool enabled)
	{
		if (const HWND hwnd = s_window.load())
			PostMessageW(hwnd, kMessageSetFullscreen, enabled ? 1 : 0, 0);
	}

	void ShowNotice(const std::string& title, const std::string& text, uint32_t seconds)
	{
		// Posted, as the title is: the window's thread makes and shows it.
		const HWND hwnd = s_window.load();
		if (!hwnd)
			return;

		Notice* notice = new Notice{StringUtil::UTF8StringToWideString(title), StringUtil::UTF8StringToWideString(text), seconds};
		if (!PostMessageW(hwnd, kMessageShowNotice, 0, reinterpret_cast<LPARAM>(notice)))
			delete notice;
	}
} // namespace ps2n::host::window

#elif defined(__linux__) && defined(X11_API)

// The X11 part: the same window, the same keys, the same notice. Xlib is
// used from two threads, this one and the GS thread's renderer, which draws
// into the window through the same display (XInitThreads).

#include <X11/X.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <vector>

namespace ps2n::host::window
{
	namespace
	{
		// The brand's colours (assets/branding/README.md), as on Windows.
		constexpr unsigned long kNoticeNavy = 0x0A1022;
		constexpr unsigned long kNoticeBlue = 0x0060FC;

		// Xft, for the notice's text, anti-aliased, in the desktop's sans-serif
		// font: loaded when the first notice is shown, from the system's
		// libXft.so.2, which every desktop Linux has; without it, X's own
		// "fixed" font. Its types as libXft has them (X11/Xft/Xft.h).
		struct XftFont
		{
			int ascent;
			int descent;
			int height;
			int max_advance_width;
			void* charset;
			void* pattern;
		};
		struct XftRenderColor
		{
			unsigned short red, green, blue, alpha;
		};
		struct XftColor
		{
			unsigned long pixel;
			XftRenderColor color;
		};
		struct XftGlyphInfo
		{
			unsigned short width, height;
			short x, y;
			short xOff, yOff;
		};
		struct XftDraw;
		struct XftLibrary
		{
			bool tried = false;
			void* handle = nullptr;
			XftFont* (*FontOpenName)(Display*, int, const char*) = nullptr;
			void (*FontClose)(Display*, XftFont*) = nullptr;
			XftDraw* (*DrawCreate)(Display*, Drawable, Visual*, Colormap) = nullptr;
			void (*DrawDestroy)(XftDraw*) = nullptr;
			int (*ColorAllocValue)(Display*, Visual*, Colormap, const XftRenderColor*, XftColor*) = nullptr;
			void (*ColorFree)(Display*, Visual*, Colormap, XftColor*) = nullptr;
			void (*DrawStringUtf8)(XftDraw*, const XftColor*, XftFont*, int, int, const unsigned char*, int) = nullptr;
			void (*TextExtentsUtf8)(Display*, XftFont*, const unsigned char*, int, XftGlyphInfo*) = nullptr;
		};
		XftLibrary s_xft;

		bool LoadXft()
		{
			if (s_xft.tried)
				return s_xft.handle != nullptr;
			s_xft.tried = true;
			void* handle = dlopen("libXft.so.2", RTLD_NOW | RTLD_LOCAL);
			if (!handle)
				return false;
			const auto get = [handle](auto& function, const char* name) {
				function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(handle, name));
				return function != nullptr;
			};
			if (!get(s_xft.FontOpenName, "XftFontOpenName") || !get(s_xft.FontClose, "XftFontClose") ||
				!get(s_xft.DrawCreate, "XftDrawCreate") || !get(s_xft.DrawDestroy, "XftDrawDestroy") ||
				!get(s_xft.ColorAllocValue, "XftColorAllocValue") || !get(s_xft.ColorFree, "XftColorFree") ||
				!get(s_xft.DrawStringUtf8, "XftDrawStringUtf8") || !get(s_xft.TextExtentsUtf8, "XftTextExtentsUtf8"))
			{
				dlclose(handle);
				return false;
			}
			s_xft.handle = handle;
			return true;
		}

		// A notice over the picture (ShowNotice).
		struct Notice
		{
			std::string title;
			std::string text;
			uint32_t seconds = 0;
		};

		// Made on the main thread; read by the core's threads (GetInfo).
		Display* s_display = nullptr;
		std::atomic<Window> s_window{0};
		std::atomic<uint32_t> s_width{0};
		std::atomic<uint32_t> s_height{0};
		std::atomic<bool> s_fullscreen{false};
		float s_scale = 1.0f;
		bool s_hidden = false;
		Listener s_listener{};

		// What other threads ask of the main thread, which a byte on a pipe wakes.
		std::mutex s_requests_mutex;
		std::deque<std::function<void()>> s_requests;
		int s_wake[2] = {-1, -1};

		// Main thread only.
		Atom s_wm_protocols = 0, s_wm_delete = 0, s_net_wm_state = 0, s_net_wm_state_fullscreen = 0, s_net_wm_name = 0,
			 s_utf8_string = 0, s_net_workarea = 0, s_shot = 0;
		std::set<uint32_t> s_down; // keys down, so that repeats are left out
		std::set<KeySym> s_toggles_down; // Alt+Enter and F11, likewise
		Window s_notice = 0;
		Notice s_notice_shown;
		bool s_notice_up = false;
		std::chrono::steady_clock::time_point s_notice_until;

		std::optional<uint32_t> KeyFromKeySym(KeySym sym)
		{
			if (sym >= XK_a && sym <= XK_z)
				return static_cast<uint32_t>('A' + (sym - XK_a));
			if (sym >= XK_A && sym <= XK_Z)
				return static_cast<uint32_t>(sym);
			if (sym >= XK_0 && sym <= XK_9)
				return static_cast<uint32_t>('0' + (sym - XK_0));
			if (sym >= XK_F1 && sym <= XK_F12)
				return static_cast<uint32_t>(KeyF1 + (sym - XK_F1));

			switch (sym)
			{
				case XK_Return: case XK_KP_Enter: return KeyReturn;
				case XK_BackSpace: return KeyBackspace;
				case XK_Escape: return KeyEscape;
				case XK_space: return KeySpace;
				case XK_Tab: case XK_ISO_Left_Tab: return KeyTab;
				case XK_Up: return KeyUp;
				case XK_Down: return KeyDown;
				case XK_Left: return KeyLeft;
				case XK_Right: return KeyRight;
				case XK_Shift_L: case XK_Shift_R: return KeyShift;
				case XK_Control_L: case XK_Control_R: return KeyControl;
				case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return KeyAlt;
				case XK_Insert: return KeyInsert;
				case XK_Delete: return KeyDelete;
				case XK_Home: return KeyHome;
				case XK_End: return KeyEnd;
				case XK_Prior: return KeyPageUp;
				case XK_Next: return KeyPageDown;
				default: return std::nullopt;
			}
		}

		// The display's scaling, as the desktop gives it to X programs (Xft.dpi;
		// 96 is 100%).
		float ScaleOf(Display* display)
		{
			const char* resources = XResourceManagerString(display);
			if (!resources)
				return 1.0f;
			XrmInitialize();
			const XrmDatabase database = XrmGetStringDatabase(resources);
			if (!database)
				return 1.0f;
			float scale = 1.0f;
			char* type = nullptr;
			XrmValue value = {};
			if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr)
			{
				const float dpi = std::strtof(value.addr, nullptr);
				if (dpi >= 48.0f && dpi <= 480.0f)
					scale = dpi / 96.0f;
			}
			XrmDestroyDatabase(database);
			return scale;
		}

		int Scaled(int pixels, float scale)
		{
			return static_cast<int>(std::lround(static_cast<float>(pixels) * scale));
		}

		// The area a window can take on the screen it comes up on: the primary
		// monitor's (or the first's), less what the desktop's panels take of it
		// (_NET_WORKAREA), as Windows' work area is.
		struct Area
		{
			int x = 0, y = 0, width = 0, height = 0;
		};
		Area WorkArea(Display* display)
		{
			const Window root = DefaultRootWindow(display);
			Area area{0, 0, DisplayWidth(display, DefaultScreen(display)), DisplayHeight(display, DefaultScreen(display))};
			int count = 0;
			if (XRRMonitorInfo* monitors = XRRGetMonitors(display, root, True, &count))
			{
				int chosen = 0;
				for (int i = 0; i < count; i++)
				{
					if (monitors[i].primary)
						chosen = i;
				}
				if (count > 0)
					area = {monitors[chosen].x, monitors[chosen].y, monitors[chosen].width, monitors[chosen].height};
				XRRFreeMonitors(monitors);
			}

			Atom type = 0;
			int format = 0;
			unsigned long items = 0, after = 0;
			unsigned char* data = nullptr;
			if (XGetWindowProperty(display, root, s_net_workarea, 0, 4, False, XA_CARDINAL, &type, &format, &items, &after, &data) ==
					Success &&
				data && format == 32 && items >= 4)
			{
				const long* work = reinterpret_cast<const long*>(data);
				const int left = std::max(area.x, static_cast<int>(work[0]));
				const int top = std::max(area.y, static_cast<int>(work[1]));
				const int right = std::min(area.x + area.width, static_cast<int>(work[0] + work[2]));
				const int bottom = std::min(area.y + area.height, static_cast<int>(work[1] + work[3]));
				if (right > left && bottom > top)
					area = {left, top, right - left, bottom - top};
			}
			if (data)
				XFree(data);
			return area;
		}

		void SetTitleNow(const std::string& title)
		{
			const Window window = s_window.load();
			XStoreName(s_display, window, title.c_str());
			XChangeProperty(s_display, window, s_net_wm_name, s_utf8_string, 8, PropModeReplace,
				reinterpret_cast<const unsigned char*>(title.data()), static_cast<int>(title.size()));
			XFlush(s_display);
		}

		// Covering the screen is the window manager's to do: asked through
		// _NET_WM_STATE, as every desktop of today has it.
		void ApplyFullscreen(bool enabled)
		{
			if (enabled == s_fullscreen.load())
				return;
			XEvent event = {};
			event.xclient.type = ClientMessage;
			event.xclient.window = s_window.load();
			event.xclient.message_type = s_net_wm_state;
			event.xclient.format = 32;
			event.xclient.data.l[0] = enabled ? 1 : 0; // _NET_WM_STATE_ADD, _NET_WM_STATE_REMOVE
			event.xclient.data.l[1] = static_cast<long>(s_net_wm_state_fullscreen);
			event.xclient.data.l[3] = 1; // asked by an application
			XSendEvent(s_display, DefaultRootWindow(s_display), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
			XFlush(s_display);
			s_fullscreen.store(enabled);
		}

		// Whether the window manager has the window covering the screen now,
		// whoever asked.
		void ReadFullscreen()
		{
			Atom type = 0;
			int format = 0;
			unsigned long items = 0, after = 0;
			unsigned char* data = nullptr;
			bool covering = false;
			if (XGetWindowProperty(s_display, s_window.load(), s_net_wm_state, 0, 64, False, XA_ATOM, &type, &format, &items, &after,
					&data) == Success &&
				data && format == 32)
			{
				const Atom* atoms = reinterpret_cast<const Atom*>(data);
				for (unsigned long i = 0; i < items; i++)
					covering = covering || atoms[i] == s_net_wm_state_fullscreen;
			}
			if (data)
				XFree(data);
			s_fullscreen.store(covering);
		}

		// ---- the notice ---------------------------------------------------------------

		// Its two lines' fonts, the title semi-bold, at the display's scaling.
		struct NoticeFonts
		{
			XftFont* title = nullptr;
			XftFont* text = nullptr;
			XFontStruct* fixed = nullptr; // without Xft
		};

		NoticeFonts OpenNoticeFonts()
		{
			NoticeFonts fonts;
			if (LoadXft())
			{
				const std::string title = "sans-serif:weight=demibold:pixelsize=" + std::to_string(Scaled(17, s_scale));
				const std::string text = "sans-serif:pixelsize=" + std::to_string(Scaled(15, s_scale));
				fonts.title = s_xft.FontOpenName(s_display, DefaultScreen(s_display), title.c_str());
				fonts.text = s_xft.FontOpenName(s_display, DefaultScreen(s_display), text.c_str());
				if (fonts.title && fonts.text)
					return fonts;
				if (fonts.title)
					s_xft.FontClose(s_display, fonts.title);
				if (fonts.text)
					s_xft.FontClose(s_display, fonts.text);
				fonts = {};
			}
			fonts.fixed = XLoadQueryFont(s_display, "fixed");
			return fonts;
		}

		void CloseNoticeFonts(const NoticeFonts& fonts)
		{
			if (fonts.title)
				s_xft.FontClose(s_display, fonts.title);
			if (fonts.text)
				s_xft.FontClose(s_display, fonts.text);
			if (fonts.fixed)
				XFreeFont(s_display, fonts.fixed);
		}

		// A line's width and height, and how far its baseline is below its top.
		struct Line
		{
			int width = 0, height = 0, ascent = 0;
		};
		Line Measure(const NoticeFonts& fonts, bool title, const std::string& text)
		{
			const unsigned char* utf8 = reinterpret_cast<const unsigned char*>(text.data());
			if (XftFont* font = title ? fonts.title : fonts.text)
			{
				XftGlyphInfo extents = {};
				s_xft.TextExtentsUtf8(s_display, font, utf8, static_cast<int>(text.size()), &extents);
				return {extents.xOff, font->ascent + font->descent, font->ascent};
			}
			if (fonts.fixed)
			{
				return {XTextWidth(fonts.fixed, text.data(), static_cast<int>(text.size())),
					fonts.fixed->ascent + fonts.fixed->descent, fonts.fixed->ascent};
			}
			return {};
		}

		void DrawNotice()
		{
			if (!s_notice)
				return;
			const NoticeFonts fonts = OpenNoticeFonts();
			const Line title = Measure(fonts, true, s_notice_shown.title);
			const Line text = Measure(fonts, false, s_notice_shown.text);
			XWindowAttributes attributes = {};
			XGetWindowAttributes(s_display, s_notice, &attributes);

			const GC gc = XCreateGC(s_display, s_notice, 0, nullptr);
			XSetForeground(s_display, gc, kNoticeNavy);
			XFillRectangle(s_display, s_notice, gc, 0, 0, attributes.width, attributes.height);
			XSetForeground(s_display, gc, kNoticeBlue);
			XFillRectangle(s_display, s_notice, gc, 0, 0, Scaled(4, s_scale), attributes.height);

			const int x = Scaled(4 + 14, s_scale);
			const int title_top = Scaled(12, s_scale);
			const int text_top = title_top + title.height + Scaled(4, s_scale);
			if (fonts.title && fonts.text)
			{
				Visual* visual = DefaultVisual(s_display, DefaultScreen(s_display));
				const Colormap colormap = DefaultColormap(s_display, DefaultScreen(s_display));
				if (XftDraw* draw = s_xft.DrawCreate(s_display, s_notice, visual, colormap))
				{
					const XftRenderColor white_value = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
					XftColor white = {};
					if (s_xft.ColorAllocValue(s_display, visual, colormap, &white_value, &white))
					{
						s_xft.DrawStringUtf8(draw, &white, fonts.title, x, title_top + title.ascent,
							reinterpret_cast<const unsigned char*>(s_notice_shown.title.data()),
							static_cast<int>(s_notice_shown.title.size()));
						s_xft.DrawStringUtf8(draw, &white, fonts.text, x, text_top + text.ascent,
							reinterpret_cast<const unsigned char*>(s_notice_shown.text.data()),
							static_cast<int>(s_notice_shown.text.size()));
						s_xft.ColorFree(s_display, visual, colormap, &white);
					}
					s_xft.DrawDestroy(draw);
				}
			}
			else if (fonts.fixed)
			{
				XSetForeground(s_display, gc, WhitePixel(s_display, DefaultScreen(s_display)));
				XSetFont(s_display, gc, fonts.fixed->fid);
				XDrawString(s_display, s_notice, gc, x, title_top + title.ascent, s_notice_shown.title.data(),
					static_cast<int>(s_notice_shown.title.size()));
				XDrawString(s_display, s_notice, gc, x, text_top + text.ascent, s_notice_shown.text.data(),
					static_cast<int>(s_notice_shown.text.size()));
			}
			XFreeGC(s_display, gc);
			CloseNoticeFonts(fonts);
			XFlush(s_display);
		}

		// The notice sized to what it says, in the top left corner of the
		// picture, a margin in: a window of its own inside the game's, which
		// moves with it and takes no input, so that clicks go to the game's.
		void ShowNoticeNow(Notice notice)
		{
			s_notice_shown = std::move(notice);
			const NoticeFonts fonts = OpenNoticeFonts();
			const Line title = Measure(fonts, true, s_notice_shown.title);
			const Line text = Measure(fonts, false, s_notice_shown.text);
			CloseNoticeFonts(fonts);
			const int width = Scaled(4 + 14 + 16, s_scale) + std::max(title.width, text.width);
			const int height = Scaled(12 + 4 + 12, s_scale) + title.height + text.height;
			const int margin = Scaled(16, s_scale);
			if (!s_notice)
			{
				s_notice = XCreateSimpleWindow(s_display, s_window.load(), margin, margin, std::max(1, width), std::max(1, height), 0, 0,
					kNoticeNavy);
				if (!s_notice)
					return;
				XSelectInput(s_display, s_notice, ExposureMask);
			}
			else
			{
				XMoveResizeWindow(s_display, s_notice, margin, margin, std::max(1, width), std::max(1, height));
			}
			XMapRaised(s_display, s_notice);
			s_notice_up = true;
			s_notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(s_notice_shown.seconds);
			DrawNotice();
		}

		// ---- the main thread's work --------------------------------------------------

		void Post(std::function<void()> request)
		{
			{
				std::lock_guard lock(s_requests_mutex);
				s_requests.push_back(std::move(request));
			}
			if (s_wake[1] >= 0)
			{
				const char byte = 1;
				[[maybe_unused]] const ssize_t written = write(s_wake[1], &byte, 1);
			}
		}

		void Handle(XEvent& event)
		{
			const Window window = s_window.load();
			switch (event.type)
			{
				case KeyPress:
				{
					const KeySym sym = XLookupKeysym(&event.xkey, 0);
					const bool alt = (event.xkey.state & Mod1Mask) != 0;
					if (((sym == XK_Return || sym == XK_KP_Enter) && alt) || sym == XK_F11)
					{
						if (s_toggles_down.insert(sym).second)
							ApplyFullscreen(!s_fullscreen.load());
						break;
					}
					if (const std::optional<uint32_t> key = KeyFromKeySym(sym))
					{
						if (s_down.insert(*key).second && s_listener.key)
							s_listener.key(*key, true);
					}
					break;
				}

				case KeyRelease:
				{
					const KeySym sym = XLookupKeysym(&event.xkey, 0);
					s_toggles_down.erase(sym);
					if (const std::optional<uint32_t> key = KeyFromKeySym(sym))
					{
						if (s_down.erase(*key) && s_listener.key)
							s_listener.key(*key, false);
					}
					break;
				}

				case FocusOut:
					if (event.xfocus.window == window && event.xfocus.mode == NotifyNormal)
					{
						s_down.clear();
						s_toggles_down.clear();
						if (s_listener.focus_lost)
							s_listener.focus_lost();
					}
					break;

				case ConfigureNotify:
					if (event.xconfigure.window == window)
					{
						const uint32_t width = static_cast<uint32_t>(std::max(1, event.xconfigure.width));
						const uint32_t height = static_cast<uint32_t>(std::max(1, event.xconfigure.height));
						if (width != s_width.load() || height != s_height.load())
						{
							s_width.store(width);
							s_height.store(height);
							if (s_listener.resized)
								s_listener.resized(width, height, s_scale);
						}
					}
					break;

				case PropertyNotify:
					if (event.xproperty.window == window && event.xproperty.atom == s_net_wm_state)
						ReadFullscreen();
					break;

				case Expose:
					if (event.xexpose.window == s_notice && event.xexpose.count == 0)
						DrawNotice();
					break;

				case ClientMessage:
					if (event.xclient.message_type == s_wm_protocols &&
						static_cast<Atom>(event.xclient.data.l[0]) == s_wm_delete)
					{
						// The runner stops the machine; the window goes when that is done.
						if (s_listener.close_requested)
							s_listener.close_requested();
					}
					else if (event.xclient.message_type == s_shot)
					{
						if (s_listener.shot_requested)
							s_listener.shot_requested(static_cast<uint32_t>(event.xclient.data.l[0]));
					}
					break;

				default:
					break;
			}
		}

		bool OpenDisplay()
		{
			if (s_display)
				return true;
			// The renderer draws into the window from the GS thread through the
			// same display.
			XInitThreads();
			s_display = XOpenDisplay(nullptr);
			if (!s_display)
			{
				std::fputs("ps2n-host: cannot open the X display (DISPLAY is not set, or no X server answers)\n", stderr);
				return false;
			}
			s_wm_protocols = XInternAtom(s_display, "WM_PROTOCOLS", False);
			s_wm_delete = XInternAtom(s_display, "WM_DELETE_WINDOW", False);
			s_net_wm_state = XInternAtom(s_display, "_NET_WM_STATE", False);
			s_net_wm_state_fullscreen = XInternAtom(s_display, "_NET_WM_STATE_FULLSCREEN", False);
			s_net_wm_name = XInternAtom(s_display, "_NET_WM_NAME", False);
			s_utf8_string = XInternAtom(s_display, "UTF8_STRING", False);
			s_net_workarea = XInternAtom(s_display, "_NET_WORKAREA", False);
			s_shot = XInternAtom(s_display, kShotAtom, False);
			s_scale = ScaleOf(s_display);
			return true;
		}
	} // namespace

	bool Available()
	{
		return true;
	}

	bool CreateHidden()
	{
		if (s_window.load() || !OpenDisplay())
			return false;
		XSetWindowAttributes attributes = {};
		attributes.background_pixel = BlackPixel(s_display, DefaultScreen(s_display));
		const Window window = XCreateWindow(s_display, DefaultRootWindow(s_display), 0, 0, 640, 448, 0, CopyFromParent, InputOutput,
			CopyFromParent, CWBackPixel, &attributes);
		if (!window)
			return false;
		XFlush(s_display);
		s_hidden = true;
		s_width.store(640);
		s_height.store(448);
		s_window.store(window);
		return true;
	}

	bool Create(const std::string& title, uint32_t width, uint32_t height, const Listener& listener)
	{
		if (s_window.load() || !OpenDisplay())
			return false;
		if (pipe2(s_wake, O_CLOEXEC | O_NONBLOCK) != 0)
		{
			s_wake[0] = s_wake[1] = -1;
			return false;
		}
		s_listener = listener;

		// The size asked for is that of the picture's area on a display at 100%:
		// scaled for this one. A window that would not fit on its screen covers
		// the screen instead (ADR 0025); the window Alt+Enter goes back to is the
		// largest that fits, at the same proportions, in the middle of the
		// screen. The window manager's frame is not known before the window is
		// shown: a title bar of about 40 pixels at 100% is allowed for.
		const Area work = WorkArea(s_display);
		int client_width = Scaled(static_cast<int>(width), s_scale);
		int client_height = Scaled(static_cast<int>(height), s_scale);
		const int frame_height = Scaled(40, s_scale);
		bool cover = false;
		if (client_width > work.width || client_height + frame_height > work.height)
		{
			cover = true;
			const double scale = std::min(static_cast<double>(work.width) / client_width,
				static_cast<double>(work.height - frame_height) / client_height);
			client_width = std::max(1, static_cast<int>(client_width * scale));
			client_height = std::max(1, static_cast<int>(client_height * scale));
		}
		const int x = work.x + std::max(0, (work.width - client_width) / 2);
		const int y = work.y + std::max(0, (work.height - frame_height - client_height) / 2);

		XSetWindowAttributes attributes = {};
		attributes.background_pixel = BlackPixel(s_display, DefaultScreen(s_display));
		attributes.event_mask = KeyPressMask | KeyReleaseMask | FocusChangeMask | StructureNotifyMask | PropertyChangeMask;
		const Window window = XCreateWindow(s_display, DefaultRootWindow(s_display), x, y, static_cast<unsigned>(client_width),
			static_cast<unsigned>(client_height), 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &attributes);
		if (!window)
			return false;
		s_window.store(window);
		s_width.store(static_cast<uint32_t>(client_width));
		s_height.store(static_cast<uint32_t>(client_height));

		// Its class, by which the desktop groups it and a check finds it; its
		// title; where it asks to be put; that closing it is asked, not done.
		XClassHint class_hint = {};
		char name[] = "ps2n-host";
		char class_name[] = "PS2Native";
		class_hint.res_name = name;
		class_hint.res_class = class_name;
		XSetClassHint(s_display, window, &class_hint);
		XSizeHints* size_hints = XAllocSizeHints();
		if (size_hints)
		{
			size_hints->flags = PPosition | PSize;
			size_hints->x = x;
			size_hints->y = y;
			size_hints->width = client_width;
			size_hints->height = client_height;
			XSetWMNormalHints(s_display, window, size_hints);
			XFree(size_hints);
		}
		XSetWMProtocols(s_display, window, &s_wm_delete, 1);
		SetTitleNow(title);
		if (cover)
		{
			// Asked before the window is shown: it comes up covering the screen.
			XChangeProperty(s_display, window, s_net_wm_state, XA_ATOM, 32, PropModeReplace,
				reinterpret_cast<const unsigned char*>(&s_net_wm_state_fullscreen), 1);
			s_fullscreen.store(true);
		}

		// A key held down sends its presses again, but not its releases.
		XkbSetDetectableAutoRepeat(s_display, True, nullptr);

		XMapRaised(s_display, window);
		XFlush(s_display);

		// The renderer is made for the window as it is shown: wait for it, a
		// little.
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		bool mapped = false;
		while (!mapped && std::chrono::steady_clock::now() < until)
		{
			while (XPending(s_display))
			{
				XEvent event;
				XNextEvent(s_display, &event);
				if (event.type == MapNotify && event.xmap.window == window)
					mapped = true;
				Handle(event);
			}
			if (!mapped)
			{
				pollfd fd = {ConnectionNumber(s_display), POLLIN, 0};
				poll(&fd, 1, 20);
			}
		}
		XWindowAttributes shown = {};
		if (XGetWindowAttributes(s_display, window, &shown) && shown.width > 0 && shown.height > 0)
		{
			s_width.store(static_cast<uint32_t>(shown.width));
			s_height.store(static_cast<uint32_t>(shown.height));
		}
		return true;
	}

	void ServeUntil(const std::atomic<bool>& done)
	{
		if (!s_display || s_hidden || !s_window.load())
			return;
		while (!done.load())
		{
			int timeout = 100; // to look at `done`
			if (s_notice_up)
			{
				const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(s_notice_until - std::chrono::steady_clock::now());
				timeout = static_cast<int>(std::clamp<long long>(left.count(), 0, timeout));
			}
			pollfd fds[2] = {{ConnectionNumber(s_display), POLLIN, 0}, {s_wake[0], POLLIN, 0}};
			poll(fds, 2, timeout);

			char bytes[64];
			while (read(s_wake[0], bytes, sizeof(bytes)) > 0)
			{
			}
			std::deque<std::function<void()>> requests;
			{
				std::lock_guard lock(s_requests_mutex);
				requests.swap(s_requests);
			}
			for (const std::function<void()>& request : requests)
				request();

			while (XPending(s_display))
			{
				XEvent event;
				XNextEvent(s_display, &event);
				Handle(event);
			}

			if (s_notice_up && std::chrono::steady_clock::now() >= s_notice_until)
			{
				// Its time is up.
				XUnmapWindow(s_display, s_notice);
				XFlush(s_display);
				s_notice_up = false;
			}
		}
	}

	void Destroy()
	{
		const Window window = s_window.exchange(0);
		if (s_display)
		{
			if (s_notice)
			{
				XDestroyWindow(s_display, s_notice);
				s_notice = 0;
			}
			if (window)
				XDestroyWindow(s_display, window);
			XCloseDisplay(s_display);
			s_display = nullptr;
		}
		for (int& fd : s_wake)
		{
			if (fd >= 0)
				close(fd);
			fd = -1;
		}
		{
			std::lock_guard lock(s_requests_mutex);
			s_requests.clear();
		}
		s_listener = {};
		s_hidden = false;
		s_notice_up = false;
		s_down.clear();
		s_toggles_down.clear();
		s_fullscreen.store(false);
	}

	WindowInfo GetInfo()
	{
		WindowInfo wi;
		const Window window = s_window.load();
		if (!window)
			return wi; // surfaceless

		wi.type = WindowInfo::Type::X11;
		wi.display_connection = s_display;
		wi.window_handle = reinterpret_cast<void*>(static_cast<uintptr_t>(window));
		wi.surface_width = s_width.load();
		wi.surface_height = s_height.load();
		wi.surface_scale = s_scale;
		return wi;
	}

	void SetTitle(const std::string& title)
	{
		if (s_window.load() && !s_hidden)
			Post([title]() { SetTitleNow(title); });
	}

	bool IsFullscreen()
	{
		return s_fullscreen.load();
	}

	void SetFullscreen(bool enabled)
	{
		if (s_window.load() && !s_hidden)
			Post([enabled]() { ApplyFullscreen(enabled); });
	}

	void ShowNotice(const std::string& title, const std::string& text, uint32_t seconds)
	{
		if (s_window.load() && !s_hidden)
			Post([notice = Notice{title, text, seconds}]() mutable { ShowNoticeNow(std::move(notice)); });
	}
} // namespace ps2n::host::window

#else // no window code for this platform yet

namespace ps2n::host::window
{
	bool Available()
	{
		return false;
	}

	bool CreateHidden()
	{
		return false;
	}

	void ServeUntil(const std::atomic<bool>& done)
	{
	}

	bool Create(const std::string& title, uint32_t width, uint32_t height, const Listener& listener)
	{
		return false;
	}

	void Destroy()
	{
	}

	WindowInfo GetInfo()
	{
		return WindowInfo(); // surfaceless
	}

	void SetTitle(const std::string& title)
	{
	}

	bool IsFullscreen()
	{
		return false;
	}

	void SetFullscreen(bool enabled)
	{
	}

	void ShowNotice(const std::string& title, const std::string& text, uint32_t seconds)
	{
	}
} // namespace ps2n::host::window

#endif
