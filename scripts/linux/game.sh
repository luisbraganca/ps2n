#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a game from its disc image on ps2n-host for a number of frames, under
# one CPU configuration, and prints what the host reports about the run. The
# counterpart of scripts/windows/game.ps1, which says more.
#
# NOT YET RUN ON ANY LINUX MACHINE (see build.sh).
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/game.sh <disc image> [options]
#
#   --config <name>    stock-jit, stock-interp, ps2n-ee, ps2n-static (default),
#                      lockstep (ps2n-ee, then ps2n-static compared with it at
#                      every event test) or same-as-stock (stock-jit, then
#                      ps2n-static compared with it at every sync point and at
#                      every frame fingerprinted)
#   --frames <n>       stop after this many frames (default 900)
#   --renderer <name>  null, sw (default), vulkan or opengl
#   --frame-every <n>  also fingerprint and save the frame on display every n
#                      frames (default: only the last)
#   --pad <file>       a script of controller input for port 1 (tests/games)
#   --seconds <n>      wall-clock limit of each run (default 1800)
#   --name <name>      names the run's folder (default game)
#   --capture <file>   add what the run reports for the recompiler to this file,
#                      which scripts/linux/build.sh --game-capture takes
#
# The disc image is read where it is. The run's files go to
# build/linux-clang/ps2n/tests/game/<name>/<config>, which git ignores. They
# are derived from the game: keep them out of the repository.

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$root/build/linux-clang"
host="$build/ps2n/bin/ps2n-host"

if [ "$#" -lt 1 ]; then
	echo "Usage: PS2N_BIOS=<bios> scripts/linux/game.sh <disc image> [--config <name>] [--frames <n>] ..." >&2
	exit 1
fi
image="$(realpath "$1")"
shift

config=ps2n-static
frames=900
renderer=sw
frame_every=0
seconds=1800
name=game
capture=""
pad=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--pad) pad="$(realpath "$2")" ;;
		--config) config="$2" ;;
		--frames) frames="$2" ;;
		--renderer) renderer="$2" ;;
		--frame-every) frame_every="$2" ;;
		--seconds) seconds="$2" ;;
		--name) name="$2" ;;
		--capture) capture="$2" ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
	shift 2
done

if [ ! -x "$host" ]; then
	echo "Build first: scripts/linux/build.sh" >&2
	exit 1
fi
if [ -z "${PS2N_BIOS:-}" ]; then
	echo "A BIOS image is needed: set PS2N_BIOS." >&2
	exit 1
fi

case "$config" in
	stock-jit) ee=jit; iop=jit; vu=jit; static=on ;;
	stock-interp) ee=interp; iop=interp; vu=interp; static=on ;;
	ps2n-ee) ee=ps2n; iop=jit; vu=jit; static=off ;;
	ps2n-static|lockstep|same-as-stock) ee=ps2n; iop=jit; vu=jit; static=on ;;
	*) echo "Unknown configuration: $config" >&2; exit 1 ;;
esac

work="$build/ps2n/tests/game/$name/$config"

# A game prints no last line to stop at: the run ends at the frame limit.
arguments=(
	"-DHOST=$host" "-DWORK_DIR=$work" "-DELF=$image"
	"-DEE=$ee" "-DIOP=$iop" "-DVU=$vu" "-DSTATIC=$static" -DIMAGE=game
	"-DFRAMES=$frames" "-DSECONDS=$seconds" "-DUNTIL=@@ no such line @@" -DEXPECT_EXIT=2 -DWHOLE=1 -DTIMES=1
)
if [ "$renderer" != "null" ]; then
	arguments+=("-DRENDERER=$renderer" "-DFRAME_EVERY=$frame_every")
fi
if [ -n "$pad" ]; then
	arguments+=("-DPAD=$pad")
fi
if [ "$config" = "lockstep" ]; then
	arguments+=(-DLOCKSTEP=1)
elif [ "$config" = "same-as-stock" ]; then
	arguments+=(-DSAME_AS_STOCK=1)
elif [ "$ee" = "ps2n" ]; then
	arguments+=(-DCAPTURE=1)
fi

output="$(cmake "${arguments[@]}" -P "$root/tests/guest_test.cmake" 2>&1)"
code=$?
# The lines for the frames fingerprinted along the way are left out when there
# are very many of them (--frame-every 1); the driver compares them.
frame_lines="$(printf '%s\n' "$output" | grep -cE '^ps2n-host: frame [0-9]+:')"
printf '%s\n' "$output" | grep -E '^(ps2n-host:|---|The compiled run|First difference|  recorded:|  this run:|  stock:|  compiled:|CMake Error|  )' |
	if [ "$frame_lines" -gt 60 ]; then grep -vE '^ps2n-host: frame [0-9]+:'; else cat; fi
echo "$config: files in $work"

if ! printf '%s\n' "$output" | grep -q 'stopped at frame limit' ||
	{ { [ "$config" = "lockstep" ] || [ "$config" = "same-as-stock" ]; } && [ "$code" -ne 0 ]; }; then
	echo "The run did not end as expected (driver exit code $code)."
	exit 1
fi

if [ -n "$capture" ] && [ "$ee" = "ps2n" ] && [ -f "$work/capture.toml" ]; then
	"$root/build/tools-linux/bin/ps2n-recomp" capture --out "$capture" "$capture" "$work/capture.toml"
fi
exit 0
