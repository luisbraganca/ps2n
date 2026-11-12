// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window a game is played in. Windows only so far: on other platforms
// Available() says no and the host stays without a window.
//
// The Windows part follows GSRunner::CreatePlatformWindow and its message pump
// in pcsx2-gsrunner/Main.cpp of PCSX2 v2.9.170, Copyright 2002-2026 PCSX2 Dev
// Team, GPL-3.0+. Changes for PS2Native: the window is PS2Native's (its class,
// its icon, its title), it follows the display's scaling, it can cover the
// screen, and it passes on the keyboard, its size and its closing.

#include "window.h"

#include <atomic>
#include <cstring>
#include <iterator>

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
		constexpr UINT kMessageSetTitle = WM_APP + 1; // lParam: a std::wstring made with new, which the window deletes
		constexpr UINT kMessageSetFullscreen = WM_APP + 2; // wParam: whether to cover the screen

		// Written on the main thread before the machine starts and after it has
		// stopped; read by the core's threads in between.
		std::atomic<HWND> s_window{nullptr};
		std::atomic<bool> s_fullscreen{false};
		Listener s_listener{};

		// Main thread only: where the window was before it covered the screen.
		LONG s_windowed_style = 0;
		WINDOWPLACEMENT s_windowed_placement = {sizeof(WINDOWPLACEMENT)};

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

		LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
				case WM_CLOSE:
					// The runner stops the machine; the window goes when that is done.
					if (s_listener.close_requested)
						s_listener.close_requested();
					return 0;

				case WM_SIZE:
					if (wParam != SIZE_MINIMIZED && s_listener.resized)
						s_listener.resized(LOWORD(lParam), HIWORD(lParam), ScaleOf(hwnd));
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

		s_listener = listener;

		const std::wstring wide_title = StringUtil::UTF8StringToWideString(title);
		const DWORD style = WS_OVERLAPPEDWINDOW;
		const HWND hwnd = CreateWindowExW(0, kWindowClass, wide_title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
		if (!hwnd)
		{
			UnregisterClassW(kWindowClass, instance);
			return false;
		}

		// The size asked for is that of the picture's area on a display at 100%:
		// scale it for the display the window came up on, and add the frame.
		const UINT dpi = GetDpiForWindow(hwnd);
		RECT rect = {0, 0, MulDiv(static_cast<int>(width), static_cast<int>(dpi), 96),
			MulDiv(static_cast<int>(height), static_cast<int>(dpi), 96)};
		AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
		SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

		s_window.store(hwnd);
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
		DestroyWindow(hwnd);
		UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
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
} // namespace ps2n::host::window

#else // no window code for this platform yet

namespace ps2n::host::window
{
	bool Available()
	{
		return false;
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
} // namespace ps2n::host::window

#endif
