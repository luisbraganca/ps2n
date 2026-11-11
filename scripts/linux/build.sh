#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Configures and builds PS2Native's proof of concept on Linux x64.
#
# NOT YET RUN ON ANY LINUX MACHINE. It was written on Windows from the
# commands PCSX2's own Linux CI uses at v2.9.170
# (.github/workflows/linux_build_qt.yml in the submodule). Treat the first run
# as a test of this script, and report what it prints.
#
# It drives the CMake project of the pinned PCSX2 submodule, unmodified, and
# adds PS2Native's targets through cmake/ps2n-inject.cmake.
#
# Needs: clang and lld (PCSX2 supports only Clang on Linux), cmake, ninja,
# git, and the development packages PCSX2 needs. PCSX2's CI installs these on
# Ubuntu; other distributions have equivalents:
#
#   build-essential clang lld cmake ninja-build pkg-config git curl nasm
#   extra-cmake-modules libasound2-dev libaio-dev libcurl4-openssl-dev
#   libdbus-1-dev libdecor-0-dev libegl-dev libevdev-dev libfontconfig-dev
#   libgudev-1.0-dev libinput-dev libopengl-dev libpcap-dev
#   libpipewire-0.3-dev libpulse-dev libssl-dev libudev-dev libwayland-dev
#   libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxkbcommon-x11-dev
#   zlib1g-dev
#   FFmpeg 7.1 or later development files (libavcodec-dev libavformat-dev
#   libavutil-dev libswresample-dev libswscale-dev)
#
# The other libraries PCSX2 needs (SDL3, shaderc, FreeType, libpng, libwebp,
# lz4, zstd, plutovg, plutosvg, rapidyaml, libbacktrace) are built from source
# into deps/linux-x64 by PCSX2's own script on the first run. That takes a
# while and downloads their sources.
#
# Usage:
#   scripts/linux/build.sh [--reconfigure] [target ...]     (default target: ps2n-host)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pcsx2="$root/third_party/pcsx2"
deps="$root/deps/linux-x64"
build="$root/build/linux-clang"

reconfigure=0
targets=()
for arg in "$@"; do
	case "$arg" in
		--reconfigure) reconfigure=1 ;;
		*) targets+=("$arg") ;;
	esac
done
if [ "${#targets[@]}" -eq 0 ]; then
	targets=(ps2n-host)
fi

if [ ! -f "$pcsx2/CMakeLists.txt" ]; then
	echo "The PCSX2 submodule is missing. Run: git submodule update --init --depth 1" >&2
	exit 1
fi

cc="${CC:-clang}"
cxx="${CXX:-clang++}"
for tool in "$cc" "$cxx" cmake ninja; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Missing tool: $tool" >&2
		exit 1
	fi
done

# 1. The build-time tools and their unit tests (core/, recompiler/,
#    tests/unit/). They do not depend on PCSX2.
tools="$root/build/tools-linux"
if [ "$reconfigure" -eq 1 ] || [ ! -f "$tools/build.ninja" ]; then
	cmake -S "$root" -B "$tools" -G Ninja \
		-DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$tools"

# 2. The run-time side (runtime/, host/), inside PCSX2's own CMake project.
#    Its build runs the recompiler built in step 1 on the test programs.
if [ ! -d "$deps/lib" ] && [ ! -d "$deps/lib64" ]; then
	echo "Building PCSX2's dependencies into $deps with PCSX2's own script..."
	mkdir -p "$root/deps"
	# The script builds in ./deps-build under the current directory.
	(cd "$root/deps" && "$pcsx2/.github/workflows/scripts/linux/build-dependencies-runner.sh" "$deps")
fi

if [ "$reconfigure" -eq 1 ] || [ ! -f "$build/build.ninja" ]; then
	# The flags PCSX2's own CI uses for Linux, minus the Qt interface,
	# link-time optimization and ccache.
	cmake -S "$pcsx2" -B "$build" -G Ninja \
		-DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_MODULE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH="$deps" \
		-DDISABLE_ADVANCE_SIMD=ON \
		-DENABLE_SETCAP=OFF \
		-DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF \
		-DCMAKE_PROJECT_Pcsx2_INCLUDE="$root/cmake/ps2n-inject.cmake" \
		-DPS2N_ROOT="$root" \
		-DPS2N_RECOMP="$tools/bin/ps2n-recomp"
fi

cmake --build "$build" --target "${targets[@]}"
