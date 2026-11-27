#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# What PS2Native's longer Linux scripts share: source it.
#
#   . "$(dirname "${BASH_SOURCE[0]}")/common.sh"
#
# release.sh, update-games.sh and try-pcsx2.sh run for minutes or hours on a
# computer whose owner may be playing a game on it (inside WSL 2, on Windows
# beside it). The counterpart of scripts/windows/common.ps1, which says more:
#
#   root, half        the repository, and half the processors: builds, tests
#                     and runs keep to those, at a lower priority (quiet)
#   clean             an environment with nothing of this computer's in it but
#                     the system, and Qt's offscreen platform: a folder's own
#                     programs are tried in it, and no window opens
#   stamp             a line with the time
#   not_playing       stops the script where something of a PS2Native folder
#                     runs: nothing is changed under a game being played
#   describe          a game folder's programs, modules and what it has left
#                     to do
#   build_tree        build.sh with a log, the script stopped where it fails
#   test_tree         test.sh with a log, and what it said: passed, failed,
#                     skipped, disabled. A skipped test is not a passed one
#   update_folder     a game's folder brought up to date by a wizard
#   remake            a game's folder compiling what is left, as its launcher
#                     has it do, its new modules put in place as the launcher
#                     puts them
#   replay            a session run again from a game folder's own runtime and
#                     modules, through the tests' driver: nothing is written
#                     into the folder
#   folder_image, session_end, cards
#
# Nothing here knows a game, a disc or a session: the scripts are given those.

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
half="$(( ($(getconf _NPROCESSORS_ONLN) + 1) / 2 ))"
quiet=(taskset -c "0-$((half - 1))" nice -n 10)
export CMAKE_BUILD_PARALLEL_LEVEL="$half"
clean=(env -i HOME="$HOME" DISPLAY="${DISPLAY:-:0}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" PATH=/usr/bin:/bin QT_QPA_PLATFORM=offscreen
	QT_QPA_PLATFORM_PLUGIN_PATH="$root/deps/linux-x64/plugins/platforms")

stamp() { echo "[$(date -u +%H:%M:%S)] $*"; }

# not_playing <PS2Native folder>
not_playing() {
	if pgrep -f "$1" > /dev/null; then
		echo "something of $1 is running: stopping"
		pgrep -af "$1"
		exit 1
	fi
}

# describe <game folder>
describe() {
	local p
	for p in ps2n-host ps2n-remake; do [ ! -f "$1/$p" ] || echo "  $p: $(stat -c '%s bytes, %y' "$1/$p" | cut -c1-35)"; done
	echo "  toolchain: $(find "$1/toolchain" -type f 2>/dev/null | wc -l) files; modules: $(ls -la --time-style=+%m-%d_%H:%M "$1"/ps2n-*.so 2>/dev/null | awk '{print $6}' | tr '\n' ' ')"
	local f
	for f in programs.txt work/brought-up-to-date.txt work/count-again.txt; do
		echo "  $f: $([ -f "$1/$f" ] && grep -v '^#' "$1/$f" | tr '\n' ';' || echo none)"
	done
}

# build_tree <what it is> <log> <build.sh's arguments...>
build_tree() {
	local what="$1" log="$2"; shift 2
	local start code
	start=$(date +%s)
	mkdir -p "$(dirname "$log")"
	nice -n 10 bash "$root/scripts/linux/build.sh" "$@" > "$log" 2>&1
	code=$?
	stamp "$what: exit $code in $(( $(date +%s) - start )) s"
	grep -E "PS2Native: the runtime is built|PS2Native: PCSX2's code" "$log" | tail -2 | cut -c1-220 | sed 's/^/  /'
	if [ "$code" != 0 ]; then
		grep -E 'error|FAILED' "$log" | head -20 | cut -c1-300 | sed 's/^/  | /'
		stamp stopping
		exit 1
	fi
}

# test_tree <build tree> <log>: every test of it. test.sh runs ctest twice: the
# tools' unit tests, then the tree's own tests, which are the count the logs
# give for a tree. Sets tests_said, and returns 0 only where none failed and
# none was skipped.
test_tree() {
	local tree="$1" log="$2"
	local start code failed total skipped disabled tools tools_failed
	start=$(date +%s)
	mkdir -p "$(dirname "$log")"
	bash "$root/scripts/linux/test.sh" --build-dir "$tree" --jobs "$half" > "$log" 2>&1
	code=$?
	failed=$(grep -E '^[0-9]+% tests passed' "$log" | tail -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\1/')
	total=$(grep -E '^[0-9]+% tests passed' "$log" | tail -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\2/')
	tools_failed=$(grep -E '^[0-9]+% tests passed' "$log" | head -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\1/')
	tools=$(grep -E '^[0-9]+% tests passed' "$log" | head -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\2/')
	failed="${failed:-0}"; total="${total:-0}"; tools_failed="${tools_failed:-0}"; tools="${tools:-0}"
	# ctest may write a test's labels after its status: nothing is required after it.
	skipped=$(grep -cE '^[[:space:]]*[0-9]+ - .+ \(Skipped\)' "$log")
	disabled=$(grep -cE '^[[:space:]]*[0-9]+ - .+ \(Disabled\)' "$log")
	tests_said="$(( total - failed - skipped )) passed, $failed failed, $skipped skipped, $disabled disabled; the tools' unit tests: $(( tools - tools_failed )) passed, $tools_failed failed"
	stamp "$tree, every test: exit $code in $(( $(date +%s) - start )) s: $tests_said"
	grep -E '^[[:space:]]*[0-9]+ - .+ \((Failed|Timeout|Not Run|SEGFAULT)\)' "$log" | head -20 | cut -c1-160 | sed 's/^/  /'
	[ "$code" = 0 ] && [ "$failed" = 0 ] && [ "$skipped" = 0 ]
}

# update_folder <PS2Native folder> <game folder>: the wizard's own way, without a window.
update_folder() {
	(cd "$1/runtime" && "${clean[@]}" ./ps2n-wizard --update "$2" 2>&1 | grep 'ps2n-wizard:' | cut -c1-200 | sed 's/^/  | /')
}

# remake <game folder> [ps2n-remake's arguments]: returns its exit code.
remake() {
	local folder="$1"; shift
	local mark start code next
	mark=0
	[ ! -f "$folder/work/remake.log" ] || mark=$(wc -l < "$folder/work/remake.log")
	start=$(date +%s)
	(cd "$folder" && "${clean[@]}" "${quiet[@]}" ./ps2n-remake "$@" > /dev/null 2>&1)
	code=$?
	echo "  ps2n-remake $*: exit $code in $(( $(date +%s) - start )) s"
	tail -n +$((mark + 1)) "$folder/work/remake.log" 2>/dev/null | grep -E 'brought up to date|nothing new|new code|Compiling|ready for|not compiled|failed' | cut -c1-200 | sed 's/^/  | /'
	next=$(ls -d "$folder"/work/next-* 2>/dev/null | tail -1)
	if [ -n "$next" ] && [ -f "$next/ready" ]; then
		cp -f "$next"/ps2n-*.so "$folder/" && rm -rf "$folder"/work/next-* && echo "  the new modules put in place"
	else
		echo "  no new modules: nothing was left to compile"
	fi
	return $code
}

# The disc image a game's folder plays: the last line of its settings.txt.
folder_image() { grep -v '^#' "$1/settings.txt" | sed '/^$/d' | tail -1; }

# How many frames a session written down has.
session_end() { grep -E '^end [0-9]+' "$1/input.txt" 2>/dev/null | tail -1 | cut -d' ' -f2; }

# The memory cards of a game's folder, each with its SHA-256. Those of its
# sessions and of its compile's runs are not its own.
cards() { find "$1" -name '*.ps2' -not -path '*/sessions/*' -not -path '*/work/*' -exec sha256sum {} + 2>/dev/null | sort; }

# replay <build tree> <game folder> <session> <frames, or 0 for all> <name> [thread] [pace file]
# The run's files go into the build tree. "thread" has VU1 on PCSX2's VU
# thread; a pace file has the run write down the pace it kept (-pace). Sets
# replay_said, and returns 0 where the run was the session at every
# fingerprint compared.
replay() {
	local tree="$1" folder="$2" from="$3" frames="$4" tag="$5" thread="${6:-}" pace="${7:-}"
	[ "$frames" != 0 ] || frames="$(session_end "$from")"
	local image work extra=() label="" start out
	image="$(folder_image "$folder")"
	work="$tree/ps2n/tests/game/$tag/ps2n-static"
	[ -z "$thread" ] || { extra=(-DPCSX2_VU_THREAD=1); label=" with VU1 on PCSX2's VU thread"; }
	[ -z "$pace" ] || { rm -f "$pace"; extra+=("-DPACE=$pace"); label="$label, its pace written down"; }
	start=$(date +%s)
	out="$("${quiet[@]}" cmake "-DHOST=$folder/ps2n-host" "-DWORK_DIR=$work" "-DELF=$image" -DEE=ps2n -DIOP=ps2n -DVU=jit -DSTATIC=on -DVU_PS2N=both \
		-DIMAGE=game "-DVU_CAPTURE=$work/vu-capture.toml" "-DIOP_CAPTURE=$work/iop-capture.toml" -DCAPTURE=1 -DSECONDS=1800 "-DUNTIL=@@ no such line @@" \
		-DEXPECT_EXIT=2 -DWHOLE=1 -DTIMES=1 "-DINPUT=$from/input.txt" "-DMEMCARDS_FROM=$from" "-DNVRAM_FROM=$from" "-DFRAMES=$frames" "${extra[@]}" \
		-P "$root/tests/guest_test.cmake" 2>&1)"
	replay_said="$(grep -oE 'the run is the session at .*|parts from it.*' <<< "$out" | tail -1 | cut -c1-150)"
	echo "  $frames frames of $(basename "$from") run again$label in $(( $(date +%s) - start )) s: $replay_said"
	grep -E 'stopped at' <<< "$out" | tail -1 | sed 's/^ps2n-host: //' | cut -c1-170 | sed 's/^/  /'
	grep -E 'runs were on PCSX2' <<< "$out" | tail -1 | sed 's/^ps2n-host: //' | cut -c1-170 | sed 's/^/  /'
	grep -E 'EE cycles' <<< "$out" | tail -1 | sed 's/^ps2n-host: //' | cut -c1-170 | sed 's/^/  /'
	if [ -n "$pace" ]; then
		if [ -f "$pace" ]; then
			echo "  $(basename "$pace"): $(grep -vc '^#' "$pace") lines of stretches; the first and the last:"
			grep -v '^#' "$pace" | sed -n '1p;$p' | sed 's/^/  | /'
		else
			echo "  NO PACE FILE WRITTEN"
		fi
	fi
	grep -q 'stopped at frame limit' <<< "$out" || { echo "  THE RUN DID NOT END AS EXPECTED"; tail -6 <<< "$out" | cut -c1-220 | sed 's/^/  | /'; }
	grep -q 'the run is the session at' <<< "$replay_said"
}

# The lines of a list in the build tree, parts separated by |, comments left
# out, each part trimmed: one line to a row, parts separated by a tab.
read_list() {
	[ -f "$1" ] || return 0
	grep -v '^[[:space:]]*#' "$1" | sed '/^[[:space:]]*$/d' | sed -E 's/[[:space:]]*\|[[:space:]]*/\t/g; s/^[[:space:]]+//; s/[[:space:]]+$//'
}
