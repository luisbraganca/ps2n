# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps2n-host: a console frontend for the PCSX2 core.
#
# Included by cmake/ps2n-inject.cmake at the end of PCSX2's top-level
# CMakeLists.txt, so CMAKE_SOURCE_DIR is the PCSX2 submodule and this file's
# own directory is CMAKE_CURRENT_LIST_DIR.

add_executable(ps2n-host
	"${CMAKE_CURRENT_LIST_DIR}/disc.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/disc.h"
	"${CMAKE_CURRENT_LIST_DIR}/game_settings.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/game_settings.h"
	"${CMAKE_CURRENT_LIST_DIR}/host_interface.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/host_state.h"
	"${CMAKE_CURRENT_LIST_DIR}/iop_report.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/iop_report.h"
	"${CMAKE_CURRENT_LIST_DIR}/main.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/profile.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/profile.h"
	"${CMAKE_CURRENT_LIST_DIR}/selftest.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/selftest.h"
	"${CMAKE_CURRENT_LIST_DIR}/selftest_iop.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/selftest_vu.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/session_record.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/session_record.h"
	"${CMAKE_CURRENT_LIST_DIR}/shm_names.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/sync_trace.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/sync_trace.h"
	"${CMAKE_CURRENT_LIST_DIR}/vu_report.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/vu_report.h"
	"${CMAKE_CURRENT_LIST_DIR}/window.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/window.h"
)

# The same include directories pcsx2-gsrunner uses.
target_include_directories(ps2n-host PRIVATE
	"${CMAKE_BINARY_DIR}/common/include"
	"${CMAKE_SOURCE_DIR}/pcsx2"
)

target_link_libraries(ps2n-host PRIVATE
	ps2n-runtime
	PCSX2_FLAGS
	PCSX2
)

set_target_properties(ps2n-host PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ps2n/bin"
)

# PS2Native's version, which -help shows (ps2n_version.h, cmake/ps2n-inject.cmake).
target_include_directories(ps2n-host PRIVATE "${PS2N_VERSION_DIR}")

# The game's window on Linux (host/window.cpp): X11, and XRandR for the screen
# it comes up on, which PCSX2's own build finds and links; Xft, for the
# notice's text, is loaded when the first notice is shown, and so are the
# modules of compiled code (dlopen).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(ps2n-host PRIVATE X11::X11 X11::Xrandr ${CMAKE_DL_LIBS})
endif()

# The profiler tells PS2Native's own code from PCSX2's by where its source
# files are (host/profile.cpp).
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/profile.cpp" PROPERTIES
	COMPILE_DEFINITIONS "PS2N_SOURCE_ROOT=\"${PS2N_ROOT}\"")

# The PS2Native icon (assets/branding/README.md). PCSX2's own icon and file
# description are not here to replace: its build attaches them only to its
# Qt frontend (setup_main_executable in pcsx2/CMakeLists.txt).
if(WIN32)
	target_sources(ps2n-host PRIVATE "${PS2N_ROOT}/assets/ps2n.rc")
	set_source_files_properties("${PS2N_ROOT}/assets/ps2n.rc" PROPERTIES
		OBJECT_DEPENDS "${PS2N_ROOT}/assets/branding/icons/favicon.ico")
endif()

# A game's launcher (host/launcher.cpp; docs/phase4-proposal.md, step P4.4):
# a Windows program without a console, copied into a game's folder under the
# game's name, which starts the runtime beside it. Microsoft's C runtime is
# built into it, so that it needs nothing but Windows to start and to say
# what is missing.
if(WIN32)
	add_executable(ps2n-launcher WIN32 "${CMAKE_CURRENT_LIST_DIR}/launcher.cpp" "${PS2N_ROOT}/assets/ps2n.rc")
	set_property(TARGET ps2n-launcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
	target_link_libraries(ps2n-launcher PRIVATE shell32 user32)
	set_target_properties(ps2n-launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ps2n/bin")
	add_dependencies(ps2n-host ps2n-launcher)
endif()

# The core looks for its resources (shaders, fonts, the game database) in a
# "resources" directory beside the executable.
#
# PCSX2's logo is left out of the copy
# (docs/adr/0012-ps2native-branding-not-pcsx2s.md). Only PCSX2's own
# fullscreen interface draws these three files, and the host never opens it.
# When the submodule is updated, check this list against bin/resources.
set(ps2n_pcsx2_logo_files
	icons/AppBanner.svg
	icons/AppIconLarge.ico
	icons/AppIconLarge.png
)
list(TRANSFORM ps2n_pcsx2_logo_files PREPEND "$<TARGET_FILE_DIR:ps2n-host>/resources/")
add_custom_command(TARGET ps2n-host POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
		"${CMAKE_SOURCE_DIR}/bin/resources"
		"$<TARGET_FILE_DIR:ps2n-host>/resources"
	COMMAND "${CMAKE_COMMAND}" -E rm -f ${ps2n_pcsx2_logo_files}
	COMMENT "Copying PCSX2 resources beside ps2n-host, without PCSX2's logo"
)
