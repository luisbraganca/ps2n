#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs PS2Native's tests on Linux x64, in the build trees that
# scripts/linux/build.sh made:
#
#   1. the unit tests of the build-time tools, which need nothing else;
#   2. the tests that boot PS2 programs on the PCSX2 core. These need a BIOS
#      image: set PS2N_BIOS to its path. Without one they are reported as
#      skipped. The image is not copied, and nothing is written beside it.
#
# Run so far in WSL 2 with Ubuntu 22.04 on the development computer (step
# P4.7, docs/adr/0038); Linux is reported as working only after the
# maintainer's run on their own machine (docs/adr/0004).
#
# The second group uses no window, but the core still opens a graphics device,
# so it needs a working Vulkan or OpenGL driver (and, for OpenGL, a display:
# the host makes a window it never shows).
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/test.sh [--build-dir <dir>] [--jobs <n>] [ctest arguments for the
#       second group, e.g. -R ps2autotests/stock-jit]
#   --build-dir   the build tree to test (default build/linux-clang; build/linux-clang-module for the
#                 compiled code in modules of its own), as test.ps1 -BuildDir
#   --jobs        tests at once (default half the processors, so that a game played on the same computer
#                 keeps its pace)

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tools="$root/build/tools-linux"
build="$root/build/linux-clang"
jobs="$(( ($(getconf _NPROCESSORS_ONLN) + 1) / 2 ))"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--build-dir) build="$(realpath -m "$2")"; shift 2 ;;
		--jobs) jobs="$2"; shift 2 ;;
		*) break ;;
	esac
done

if [ ! -f "$tools/CTestTestfile.cmake" ] || [ ! -f "$build/CTestTestfile.cmake" ]; then
	echo "Build trees are missing. Run scripts/linux/build.sh first." >&2
	exit 1
fi

failed=0

ctest --test-dir "$tools" --output-on-failure || failed=$?

if [ -z "${PS2N_BIOS:-}" ]; then
	echo "PS2N_BIOS is not set: the tests that boot PS2 programs will be reported as skipped."
fi

ctest --test-dir "$build" -j "$jobs" --output-on-failure "$@" || failed=$?

exit "$failed"
