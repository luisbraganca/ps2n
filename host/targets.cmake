# SPDX-License-Identifier: GPL-3.0-or-later
#
# ps2n-host: a console frontend for the PCSX2 core.
#
# Included by cmake/ps2n-inject.cmake at the end of PCSX2's top-level
# CMakeLists.txt, so CMAKE_SOURCE_DIR is the PCSX2 submodule and this file's
# own directory is CMAKE_CURRENT_LIST_DIR.

add_executable(ps2n-host
	"${CMAKE_CURRENT_LIST_DIR}/host_interface.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/host_state.h"
	"${CMAKE_CURRENT_LIST_DIR}/main.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/selftest.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/selftest.h"
	"${CMAKE_CURRENT_LIST_DIR}/sync_trace.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/sync_trace.h"
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

# The PS2Native icon (assets/branding/README.md). PCSX2's own icon and file
# description are not here to replace: its build attaches them only to its
# Qt frontend (setup_main_executable in pcsx2/CMakeLists.txt).
if(WIN32)
	target_sources(ps2n-host PRIVATE "${PS2N_ROOT}/assets/ps2n.rc")
	set_source_files_properties("${PS2N_ROOT}/assets/ps2n.rc" PROPERTIES
		OBJECT_DEPENDS "${PS2N_ROOT}/assets/branding/icons/favicon.ico")
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
