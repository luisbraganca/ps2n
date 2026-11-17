# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps2n-wizard: the application a game is made with (docs/phase4-proposal.md,
# step P4.5). Built when PS2N_WIZARD is on (scripts/windows/build.ps1 -Wizard
# on), with Qt 6 from PCSX2's dependency package.
#
# Its settings step is PCSX2's own settings pages (docs/adr/0032), compiled
# from the submodule unchanged. Around them, PS2Native's own: the window that
# holds them (settings_window.cpp: PCSX2's SettingsWindow class with the pages
# a game has), what they call of the rest of PCSX2's frontend, where no machine
# runs (stand_ins.cpp; and wizard/pcsx2-qt/MainWindow.h, found before PCSX2's
# own), and what PCSX2's core asks of its frontend.
#
# Included by cmake/ps2n-inject.cmake at the end of PCSX2's top-level
# CMakeLists.txt, so CMAKE_SOURCE_DIR is the PCSX2 submodule.

set(PS2N_WIZARD OFF CACHE BOOL "Build ps2n-wizard, the application a game is made with (docs/phase4-proposal.md, step P4.5)")
if(NOT PS2N_WIZARD)
	return()
endif()

find_package(Qt6 6.10 COMPONENTS CoreTools Core GuiTools Gui WidgetsTools Widgets REQUIRED)

set(ps2n_qt "${CMAKE_SOURCE_DIR}/pcsx2-qt")

# PCSX2's pages a game has, and what they need beside them.
set(ps2n_wizard_pcsx2_files "")
foreach(name IN ITEMS
		AdvancedSettingsWidget AudioSettingsWidget DEV9SettingsWidget EmulationSettingsWidget GameCheatSettingsWidget
		GameFixSettingsWidget GamePatchSettingsWidget GameSummaryWidget GraphicsSettingsWidget MemoryCardSettingsWidget
		SettingsWidget DEV9UiCommon DEV9DnsHostDialog HddCreateQt MemoryCardConvertDialog MemoryCardConvertWorker
		MemoryCardCreateDialog)
	foreach(extension IN ITEMS cpp h ui)
		if(EXISTS "${ps2n_qt}/Settings/${name}.${extension}")
			list(APPEND ps2n_wizard_pcsx2_files "${ps2n_qt}/Settings/${name}.${extension}")
		endif()
	endforeach()
endforeach()
list(APPEND ps2n_wizard_pcsx2_files
	"${ps2n_qt}/Settings/SettingsWindow.h" "${ps2n_qt}/Settings/SettingsWindow.ui"
	"${ps2n_qt}/QtProgressCallback.cpp" "${ps2n_qt}/QtProgressCallback.h"
	"${ps2n_qt}/QtUtils.cpp" "${ps2n_qt}/QtUtils.h"
	"${ps2n_qt}/SettingWidgetBinder.h")

# The icons of PCSX2's pages: its two icon themes ("black" for a light window,
# "white" for a dark one), compiled into the wizard from where they are in the
# submodule, and nothing else of PCSX2's resources (its logo stays out, ADR 0012).
file(GLOB_RECURSE ps2n_wizard_icons RELATIVE "${ps2n_qt}/resources"
	"${ps2n_qt}/resources/icons/black/*" "${ps2n_qt}/resources/icons/white/*")
set(ps2n_wizard_qrc "<RCC>\n<qresource prefix=\"/\">\n")
foreach(icon IN LISTS ps2n_wizard_icons)
	string(APPEND ps2n_wizard_qrc "<file alias=\"${icon}\">${ps2n_qt}/resources/${icon}</file>\n")
endforeach()
string(APPEND ps2n_wizard_qrc "</qresource>\n</RCC>\n")
set(ps2n_wizard_qrc_file "${CMAKE_BINARY_DIR}/ps2n-wizard-icons.qrc")
if(EXISTS "${ps2n_wizard_qrc_file}")
	file(READ "${ps2n_wizard_qrc_file}" ps2n_wizard_qrc_old)
endif()
if(NOT "${ps2n_wizard_qrc}" STREQUAL "${ps2n_wizard_qrc_old}")
	file(WRITE "${ps2n_wizard_qrc_file}" "${ps2n_wizard_qrc}")
endif()

add_executable(ps2n-wizard WIN32
	"${ps2n_wizard_qrc_file}"
	"${PS2N_ROOT}/host/game_settings.cpp"
	"${PS2N_ROOT}/host/game_settings.h"
	"${PS2N_ROOT}/assets/ps2n.rc"
	"${CMAKE_CURRENT_LIST_DIR}/host_interface.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/main.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/package.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/package.h"
	"${CMAKE_CURRENT_LIST_DIR}/pipeline.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/pipeline.h"
	"${CMAKE_CURRENT_LIST_DIR}/settings_window.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/stand_ins.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/wizard_window.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/wizard_window.h"
	"${CMAKE_CURRENT_LIST_DIR}/pcsx2-qt/MainWindow.h"
	${ps2n_wizard_pcsx2_files}
)
set_target_properties(ps2n-wizard PROPERTIES
	AUTOMOC ON
	AUTOUIC ON
	AUTORCC ON
	AUTOUIC_SEARCH_PATHS "${ps2n_qt}/Settings;${ps2n_qt}"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ps2n/bin"
)
# The stand-ins first: a page's #include "MainWindow.h" finds the wizard's.
target_include_directories(ps2n-wizard PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/pcsx2-qt"
	"${PS2N_ROOT}/host"
	"${ps2n_qt}"
	"${ps2n_qt}/Settings"
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}/pcsx2"
)
target_compile_definitions(ps2n-wizard PRIVATE QT_NO_EXCEPTIONS)
target_link_libraries(ps2n-wizard PRIVATE PCSX2_FLAGS PCSX2 Qt6::Core Qt6::Gui Qt6::Widgets)
