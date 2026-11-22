#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes the profile of play a release's runtime is built with (docs/adr/0047):
# the counterpart of scripts/windows/runtime-profile.ps1, which says more.
#
# 1. builds the runtime to count what it runs, in a build tree of its own
#    (--count-dir: scripts/linux/build.sh --module on --runtime-pgo generate);
# 2. runs each training run with it, the game's code from the game's own folder
#    (ps2n-host -modules), each run writing its counts;
# 3. puts the counts together without the functions no run ran (llvm-profdata
#    merge -sparse) into --out.
#
# The runtime is then built with them:
#
#   scripts/linux/build.sh --build-dir build/linux-clang-module --runtime-lto on \
#       --runtime-pgo use --runtime-pgo-data build/pgo/runtime-linux.profdata
#
# The training runs are named in a text file (--training), which names the
# user's own games and so stays in the build tree. One run to a line, its
# parts separated by "|"; a line that begins with # is a comment:
#
#   <name> | <disc image> | <game folder> | <session folder, or -> | <frames, or 0>
#
# <game folder> is a folder that plays the game, made with this tree's programs
# on this system: the run takes the modules of compiled code from it and writes
# nothing into it. <session folder> is a session written down, run again; "-"
# runs the game from boot. <frames> is how far the run goes; 0 is the session
# whole. A run of a session must be the session at every fingerprint compared,
# or the script stops. A session whose time is measured against stock PCSX2 is
# never among them.
#
# A sixth part, "vu-thread", has the run carry VU1 out on PCSX2's own VU thread
# (docs/adr/0051), as a game is played where that thread is on: the runtime is
# then trained on what it does there too. Such a run of a session need not be
# the session at every fingerprint: the script says so and goes on.
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/runtime-profile.sh [options]
#
#   --training <file>     the runs (default build/pgo/training.txt)
#   --count-dir <dir>     the tree the runtime is built to count in (default build/linux-clang-count)
#   --out <file>          the profile (default build/pgo/runtime-linux.profdata)
#   --renderer <name>     what the runs draw with: opengl (default), vulkan, sw or null
#   --patch-archive <zip> PCSX2's archive of patches, for a session that was not played from a game's folder
#   --resume              keep the counts of the runs already made, and make only the others
#   --skip-build          the runtime to count with is there already

set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
training="$root/build/pgo/training.txt"
count="$root/build/linux-clang-count"
out="$root/build/pgo/runtime-linux.profdata"
renderer=opengl
patch_archive=""
resume=0
skip_build=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--training) training="$(realpath "$2")"; shift 2 ;;
		--count-dir) count="$(realpath -m "$2")"; shift 2 ;;
		--out) out="$(realpath -m "$2")"; shift 2 ;;
		--renderer) renderer="$2"; shift 2 ;;
		--patch-archive) patch_archive="$(realpath "$2")"; shift 2 ;;
		--resume) resume=1; shift ;;
		--skip-build) skip_build=1; shift ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
if [ -z "${PS2N_BIOS:-}" ]; then echo "A BIOS image is needed: set PS2N_BIOS." >&2; exit 1; fi
if [ ! -f "$training" ]; then echo "No training runs: $training (this script's first lines say what it holds)." >&2; exit 1; fi
profdata="$root/deps/llvm-22.1.3-linux/bin/llvm-profdata"
[ -x "$profdata" ] || profdata="$(command -v llvm-profdata || true)"
if [ -z "$profdata" ]; then echo "llvm-profdata is neither in deps/llvm-22.1.3-linux/bin nor on PATH." >&2; exit 1; fi
trim() { sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//' <<< "$1"; }

# 1. The runtime built to count.
if [ "$skip_build" = 0 ]; then
	echo "The runtime built to count what it runs, in $count"
	bash "$root/scripts/linux/build.sh" --build-dir "$count" --module on --wizard off --runtime-lto off --runtime-pgo generate ||
		{ echo "The runtime could not be built to count." >&2; exit 1; }
fi
host="$count/ps2n/bin/ps2n-host"
if [ ! -x "$host" ]; then echo "No runtime in $count: leave --skip-build out." >&2; exit 1; fi

# 2. The training runs.
raw="$count/counts"
mkdir -p "$raw"
[ "$resume" = 1 ] || rm -f "$raw"/*.profraw
runs=0
while IFS= read -r line || [ -n "$line" ]; do
	line="$(trim "${line%$'\r'}")"
	case "$line" in ''|'#'*) continue ;; esac
	IFS='|' read -r name image folder session frames extra <<< "$line"
	name="$(trim "$name")"; image="$(trim "$image")"; folder="$(trim "$folder")"; session="$(trim "$session")"; frames="$(trim "$frames")"
	extra="$(trim "${extra:-}")"
	if { [ -n "$extra" ] && [ "$extra" != "vu-thread" ]; } || [ -z "$frames" ]; then
		echo "$training: not a training run (five parts separated by |, and vu-thread for a sixth or none): $line" >&2; exit 1
	fi
	[ "$session" = "-" ] && session=""
	needed=("$image" "$folder")
	[ -z "$session" ] || needed+=("$session/input.txt")
	for path in "${needed[@]}"; do
		[ -e "$path" ] || { echo "$training, $name: $path is not there." >&2; exit 1; }
	done
	runs=$((runs + 1))
	if [ "$resume" = 1 ] && compgen -G "$raw/$name-*.profraw" > /dev/null; then echo "$name: its counts are there"; continue; fi
	rm -f "$raw/$name"-*.profraw
	work="$count/ps2n/tests/game/train-$name/ps2n-static"
	arguments=(
		"-DHOST=$host" "-DWORK_DIR=$work" "-DELF=$image" "-DMODULES=$folder"
		-DEE=ps2n -DIOP=ps2n -DVU=jit -DSTATIC=on -DVU_PS2N=both -DIMAGE=game
		"-DVU_CAPTURE=$work/vu-capture.toml" "-DIOP_CAPTURE=$work/iop-capture.toml" -DCAPTURE=1
		-DSECONDS=3600 "-DUNTIL=@@ no such line @@" -DEXPECT_EXIT=2 -DWHOLE=1 -DTIMES=1
	)
	[ "$renderer" = "null" ] || arguments+=("-DRENDERER=$renderer" -DUPSCALE=1 -DFRAME_EVERY=0)
	if [ -n "$session" ]; then
		arguments+=("-DINPUT=$session/input.txt" "-DMEMCARDS_FROM=$session" "-DNVRAM_FROM=$session")
		[ -z "$patch_archive" ] || arguments+=("-DPATCH_ARCHIVE=$patch_archive")
		if [ "$frames" = 0 ]; then frames="$(grep -E '^end [0-9]+' "$session/input.txt" | tail -1 | cut -d' ' -f2)"; fi
		[ -n "$frames" ] || { echo "$name: the session in $session did not end (no end line in input.txt)." >&2; exit 1; }
	fi
	[ "$frames" -gt 0 ] 2>/dev/null || { echo "$training, $name: a run from boot needs its frames." >&2; exit 1; }
	arguments+=("-DFRAMES=$frames")
	[ -z "$extra" ] || arguments+=(-DPCSX2_VU_THREAD=1)
	echo "$name: $([ -n "$session" ] && echo "the session $(basename "$session")" || echo "from boot"), $frames frames, the modules of $folder$([ -z "$extra" ] || echo ", VU1 on PCSX2's VU thread")"
	# (%p: the process's number, so that no run writes over another's counts.)
	output="$(LLVM_PROFILE_FILE="$raw/$name-%p.profraw" cmake "${arguments[@]}" -P "$root/tests/guest_test.cmake" 2>&1)"
	if ! grep -q 'stopped at frame limit' <<< "$output"; then
		tail -15 <<< "$output" | sed 's/^/  | /'
		echo "$name: the run did not end as expected." >&2; exit 1
	fi
	said="$(grep -oE 'the run is the session at .*' <<< "$output" | tail -1)"
	if [ -n "$session" ] && ! grep -q 'every one of' <<< "$said"; then
		if [ -z "$extra" ]; then echo "$name: the run is not the session ($said): not trained on." >&2; exit 1; fi
		echo "  with VU1 on PCSX2's VU thread the run is not the session at every fingerprint ($said): trained on all the same"
	fi
	compgen -G "$raw/$name-*.profraw" > /dev/null || { echo "$name: the run wrote no counts. Was the runtime in $count built to count?" >&2; exit 1; }
	echo "  ${said:-ran}; counts: $(cat "$raw/$name"-*.profraw | wc -c) bytes"
done < "$training"
if [ "$runs" = 0 ]; then echo "$training names no training run." >&2; exit 1; fi

# 3. The counts put together, without the functions no run ran.
mkdir -p "$(dirname "$out")"
"$profdata" merge -sparse -o "$out" "$raw"/*.profraw || { echo "llvm-profdata could not put the counts together." >&2; exit 1; }
echo "The profile: $out ($(wc -c < "$out") bytes, from $runs runs)"
"$profdata" show "$out" | head -8 | sed 's/^/  /'
echo "Build the runtime with it:"
echo "  scripts/linux/build.sh --build-dir build/linux-clang-module --runtime-lto on --runtime-pgo use --runtime-pgo-data $out"
