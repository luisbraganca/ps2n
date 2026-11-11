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
# NOT YET RUN ON ANY LINUX MACHINE (see build.sh).
#
# The second group uses no window, but the core still opens a graphics device,
# so it needs a working Vulkan or OpenGL driver.
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/test.sh [ctest arguments for the second group, e.g. -R ps2autotests/stock-jit]

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tools="$root/build/tools-linux"
build="$root/build/linux-clang"

if [ ! -f "$tools/CTestTestfile.cmake" ] || [ ! -f "$build/CTestTestfile.cmake" ]; then
	echo "Build trees are missing. Run scripts/linux/build.sh first." >&2
	exit 1
fi

failed=0

ctest --test-dir "$tools" --output-on-failure || failed=$?

if [ -z "${PS2N_BIOS:-}" ]; then
	echo "PS2N_BIOS is not set: the tests that boot PS2 programs will be reported as skipped."
fi

ctest --test-dir "$build" -j "$(getconf _NPROCESSORS_ONLN)" --output-on-failure "$@" || failed=$?

exit "$failed"
