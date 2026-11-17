// SPDX-License-Identifier: GPL-3.0-or-later
//
// What PCSX2's settings pages use of its main window (pcsx2-qt/MainWindow.h),
// for the wizard, which has none of PCSX2's frontend (docs/adr/0032). This
// directory is searched before PCSX2's (wizard/targets.cmake), so that the
// pages, compiled unchanged, find this header and not PCSX2's, which brings
// its debugger, its input recording viewer and its controller windows with it.

#pragma once

#include <string>

class MainWindow
{
public:
	// The file dialog's filter for disc images, as PCSX2's main window has it.
	static const char* DISC_IMAGE_FILTER;

	// The game summary's "rescan" of its disc image: the wizard keeps no game
	// list, and the page reads the image again itself.
	void rescanFile(const std::string& path);
};

extern MainWindow* g_main_window;
