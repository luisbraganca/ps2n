#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the blocks of the self-test's random IOP programs into the host,
# compiled ahead of time: the counterpart of scripts/windows/iop-selftest.ps1,
# which says more.
#
#   1. It runs the self-test's IOP programs (as many as the test
#      selftest/iop-compiled runs: PS2N_IOP_SELFTEST_PROGRAMS in
#      tests/tests.cmake) with every block interpreted, and has the host write
#      those blocks to build/iop/selftest-capture.toml.
#   2. It builds the host with that capture (scripts/linux/build.sh
#      --iop-selftest-capture), which the build tree then remembers.
#
# After it, selftest/iop-compiled runs those programs on the compiled blocks:
# without the capture in the build that test is skipped, not passed. Run it
# again whenever the test says its blocks had to be interpreted.
#
# Needs a BIOS image, as every test that starts the machine: PS2N_BIOS.
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/iop-selftest.sh [--build-dir <dir>] [--programs <n>]
#   --build-dir   the build tree (default build/linux-clang); give each tree its turn
#   --programs    as many of the programs as selftest/iop-compiled runs (default 1500)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$root/build/linux-clang"
programs=1500
while [ "$#" -gt 0 ]; do
	case "$1" in
		--build-dir) build="$(realpath -m "$2")"; shift 2 ;;
		--programs) programs="$2"; shift 2 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
host="$build/ps2n/bin/ps2n-host"
if [ ! -x "$host" ]; then echo "Build first: scripts/linux/build.sh --build-dir $build" >&2; exit 1; fi
if [ -z "${PS2N_BIOS:-}" ]; then echo "A BIOS image is needed: set PS2N_BIOS." >&2; exit 1; fi

# 1. The programs' blocks, written down by a run that interprets them all.
capture="$root/build/iop/selftest-capture.toml"
mkdir -p "$(dirname "$capture")"
work="$build/ps2n/tests/selftest/iop-capture"
code=0
output="$(cmake "-DHOST=$host" "-DWORK_DIR=$work" -DSELFTEST=1 "-DSELFTEST_IOP=$programs" -DIOP_STATIC=off "-DIOP_CAPTURE=$capture" \
	-DSECONDS=600 -P "$root/tests/guest_test.cmake" 2>&1)" || code=$?
grep -E '^(selftest:|ps2n-host:|CMake Error|  )' <<< "$output" || true
if [ "$code" -ne 0 ] || [ ! -s "$capture" ]; then
	echo "The self-test's IOP programs did not run to the end (driver exit code $code): nothing built." >&2
	exit 1
fi

# 2. The host built with them.
exec "$root/scripts/linux/build.sh" --build-dir "$build" --iop-selftest-capture "$capture"
