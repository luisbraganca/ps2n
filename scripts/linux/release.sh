#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes a release's Linux download from the committed and pushed tree, and
# tries it: the counterpart of scripts/windows/release.ps1, which says more.
# docs/development.md, "A release", has the whole order.
#
#   first     the tree a release is made from built as it stands, and a first
#             PS2Native folder made from it (<release>/first-kit), for its wizard
#   training  the folders the profile is trained on (the third part of each
#             line of --training) brought up to date with that wizard and
#             compiled by their own ps2n-remake --before-play
#   profile   the runtime's profile of play made again (runtime-profile.sh),
#             the one before kept beside it
#   build     the tree built with link-time optimization and that profile; the
#             development tree built too
#   test      every test in both. One that fails, or is skipped, stops here
#   kit       the folder for others with its archive (kit.sh --out
#             <release>/PS2Native --archive), PS2Native-<version>-linux-x64.tar.xz,
#             copied to --to where given; its SHA-256
#   unpacked  the archive unpacked elsewhere and tried with nothing but the
#             system on PATH: its runtime reads a disc (--disc), its wizard
#             makes a game from its disc image with every default (--image),
#             then finds that folder up to date, and a session of that game
#             (--session) is run again whole from it, its pace written down
#   earlier   a copy of that game's folder as the release before made it
#             (--earlier) brought up to date by this archive's wizard, compiled,
#             the start of the session run again, the wizard asked again
#
# Only when asked for by name:
#
#   theirs    the maintainer's own Linux PS2Native folder (--kit, default
#             build/PS2Native) made again from the same tree, its game folders
#             as they are, which is checked
#   stock     stock PCSX2's tree built as PCSX2's releases are (link-time
#             optimization, no profile, none of PS2Native's changes to PCSX2's
#             code), for the timing
#   time      each line of --timed, the session whole and nothing drawn, in
#             turns, --rounds rounds: stock PCSX2 with its VU thread on, and
#             the compiled game with VU1 on PCSX2's VU thread, both with the
#             modules of the folder named. Alone on the computer
#
# <release> is build/release/<version>. Half the processors. It stops where
# something of the maintainer's PS2Native folder runs, and, for the archive,
# where the tree has changes not committed or does not say --version.
# --rehearsal goes through it on any tree, into build/release/rehearsal.
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/release.sh [options]
#   --version <v>          the version released (default: what version.txt says)
#   --parts <list>         default first,training,profile,build,test,kit,unpacked,earlier
#   --training <file>      the runs the profile is trained on (default build/pgo/training.txt)
#   --patch-archive <zip>  PCSX2's archive of patches, for the training runs that need it
#   --image <disc image>   what the unpacked archive's wizard makes a game from
#   --session <dir>        a session of that game
#   --disc <disc>          a disc in another format, read by the unpacked runtime
#   --earlier <dir>        that game's folder as the release before's archive made it
#   --to <dir>             where the archive is copied to (the Windows side's build/release/<version>)
#   --kit <dir>            the maintainer's own PS2Native folder, for 'theirs' (default build/PS2Native)
#   --timed <file>         for 'time': lines "<name> | <disc image> | <folder> | <session>" (default build/timed.txt)
#   --rounds <n>           for 'time' (default 2)
#   --rehearsal

set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

version=""
parts="first,training,profile,build,test,kit,unpacked,earlier"
training="$root/build/pgo/training.txt"
patch_archive=""
image=""
session=""
disc=""
earlier=""
to=""
theirs="$root/build/PS2Native"
timed="$root/build/timed.txt"
rounds=2
rehearsal=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--version) version="$2"; shift 2 ;;
		--parts) parts="$2"; shift 2 ;;
		--training) training="$(realpath -m "$2")"; shift 2 ;;
		--patch-archive) patch_archive="$2"; shift 2 ;;
		--image) image="$2"; shift 2 ;;
		--session) session="$2"; shift 2 ;;
		--disc) disc="$2"; shift 2 ;;
		--earlier) earlier="$2"; shift 2 ;;
		--to) to="$2"; shift 2 ;;
		--kit) theirs="$(realpath -m "$2")"; shift 2 ;;
		--timed) timed="$(realpath -m "$2")"; shift 2 ;;
		--rounds) rounds="$2"; shift 2 ;;
		--rehearsal) rehearsal=1; shift ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
cd "$root" || exit 1
has() { [[ ",$parts," == *",$1,"* ]]; }
says="$(head -1 version.txt | tr -d '[:space:]')"
[ -n "$version" ] || version="$says"
label="$version"
[ -z "$rehearsal" ] || label=rehearsal
rel="$root/build/release/$label"
tree="$root/build/linux-clang-module"
archive="PS2Native-$label-linux-x64.tar.xz"
logs="$rel/logs"
mkdir -p "$logs"

if (has profile || has test || has unpacked || has earlier || has time) && [ -z "${PS2N_BIOS:-}" ]; then echo "A BIOS image is needed: set PS2N_BIOS." >&2; exit 1; fi
if has unpacked && { [ ! -f "$image" ] || [ ! -d "$session" ]; }; then echo "The unpacked archive is tried with a game: --image <disc image> and --session <a session of it>." >&2; exit 1; fi
if [ -n "$rehearsal" ] && has theirs; then echo "A rehearsal leaves the maintainer's PS2Native folder alone." >&2; exit 1; fi

differing="$(git status --porcelain | wc -l)"
stamp "release $label ($parts) at $(git rev-parse --short HEAD); files differing from it: $differing; origin $(git remote get-url origin); version.txt says $says"
not_playing "$theirs"
if [ -z "$rehearsal" ] && (has kit || has theirs); then
	[ "$differing" = 0 ] || { echo "The tree has changes not committed: a release is made from a committed tree (or --rehearsal)." >&2; exit 1; }
	[ "$says" = "$version" ] || { echo "version.txt says $says, not $version." >&2; exit 1; }
	if git fetch -q origin main 2>/dev/null; then
		[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "The commit is not the one pushed: a release is made from a pushed tree (or --rehearsal)." >&2; exit 1; }
	else
		echo "  (origin could not be asked whether the commit is pushed)"
	fi
fi
bad=()

if has first; then
	build_tree "the tree a release is made from, built as it stands" "$logs/first-build.log" --build-dir "$tree"
	start=$(date +%s)
	nice -n 10 bash scripts/linux/kit.sh --build-dir "$tree" --out "$rel/first-kit" > "$logs/first-kit.log" 2>&1
	code=$?
	stamp "a first PS2Native folder made from it, for its wizard: exit $code in $(( $(date +%s) - start )) s"
	[ "$code" = 0 ] || { tail -8 "$logs/first-kit.log" | sed 's/^/  | /'; stamp stopping; exit 1; }
fi

if has training; then
	[ -x "$rel/first-kit/runtime/ps2n-wizard" ] || { stamp "no wizard at $rel/first-kit (the 'first' part makes it): stopping"; exit 1; }
	[ -f "$training" ] || { stamp "no $training: stopping"; exit 1; }
	mapfile -t folders < <(read_list "$training" | cut -f3 | awk '!seen[$0]++')
	for folder in "${folders[@]}"; do
		[ -n "$folder" ] || continue
		stamp "=== $folder"
		describe "$folder" | sed -n '3,3p'
		update_folder "$rel/first-kit" "$folder"
		# A training folder's counts stay what they are: it is not asked to count again.
		rm -f "$folder/work/count-again.txt"
		remake "$folder" --before-play || { stamp "a training folder did not compile: stopping"; exit 1; }
		describe "$folder" | head -3
	done
fi

if has profile; then
	[ -f "$training" ] || { stamp "no $training: stopping"; exit 1; }
	kept="build/pgo/runtime-linux-before-$label.profdata"
	if [ -f build/pgo/runtime-linux.profdata ] && [ ! -f "$kept" ]; then cp -p build/pgo/runtime-linux.profdata "$kept"; fi
	extra=()
	[ -z "$patch_archive" ] || extra=(--patch-archive "$patch_archive")
	start=$(date +%s)
	"${quiet[@]}" bash scripts/linux/runtime-profile.sh --training "$training" --out build/pgo/runtime-linux.profdata "${extra[@]}" > "$logs/runtime-profile.log" 2>&1
	code=$?
	stamp "the runtime's profile of play made again: exit $code in $(( $(date +%s) - start )) s (the one before kept as $kept)"
	grep -E '^[a-z0-9-]+: |the session at|counts:|The profile|Total functions|Maximum|not trained|did not|could not|rror|not the session' "$logs/runtime-profile.log" | cut -c1-230 | sed 's/^/  /'
	[ "$code" = 0 ] || { tail -10 "$logs/runtime-profile.log" | sed 's/^/  | /'; stamp stopping; exit 1; }
fi

if has build; then
	build_tree "the tree built, its runtime as releases have it" "$logs/build.log" --build-dir "$tree" --runtime-lto on --runtime-pgo use --runtime-pgo-data build/pgo/runtime-linux.profdata
	ls -la --time-style=+%m-%d_%H:%M "$tree"/ps2n/bin/ps2n-host "$tree"/ps2n/bin/ps2n-wizard "$tree"/ps2n/bin/ps2n-remake | awk '{print "  ", $5, $6, $7}'
	build_tree "the development tree" "$logs/build-development.log" --build-dir "$root/build/linux-clang"
fi

if has test; then
	for t in "$tree" "$root/build/linux-clang"; do
		test_tree "$t" "$logs/test-$(basename "$t").log" || { stamp "a test failed or was skipped, which is not passed: no archive is made"; exit 1; }
	done
fi

if has kit; then
	rm -rf "$rel/PS2Native" "$rel/PS2Native.tar.xz" "$rel/$archive"
	start=$(date +%s)
	nice -n 10 bash scripts/linux/kit.sh --build-dir "$tree" --out "$rel/PS2Native" --archive > "$logs/kit.log" 2>&1
	code=$?
	stamp "the folder for others, and its archive: exit $code in $(( $(date +%s) - start )) s"
	grep -E "Its runtime|PCSX2's core|Warning|not a release|not committed" "$logs/kit.log" | cut -c1-220 | sed 's/^/  | /'
	[ "$code" = 0 ] || { tail -12 "$logs/kit.log" | sed 's/^/  | /'; stamp stopping; exit 1; }
	echo "  the folder: $(find "$rel/PS2Native" -type f | wc -l) files, $(du -sb "$rel/PS2Native" | cut -f1) bytes; games in it: $([ -d "$rel/PS2Native/games" ] && echo THERE || echo none); patches in it: $([ -d "$rel/PS2Native/patches" ] && echo THERE || echo none)"
	ls -la --time-style=+%m-%d_%H:%M "$rel/PS2Native/runtime/ps2n-host" "$rel/PS2Native/runtime/ps2n-wizard" "$rel/PS2Native/runtime/ps2n-remake" | awk '{print "  ", $5, $6, $7}'
	grep -E '^PS2Native [0-9]|commit [0-9a-f]{40}|not committed|changed it' "$rel/PS2Native/licences/README.txt" | head -5 | cut -c1-200 | sed 's/^/  | /'
	echo "  licences/PCSX2-changes: $(ls "$rel/PS2Native/licences/PCSX2-changes" 2>/dev/null | tr '\n' ' ')"
	# Nothing of this computer may be in what others get.
	echo "  paths of this computer in its texts: $(grep -c "/mnt/c\|$HOME" "$rel/PS2Native/licences/README.txt" "$rel/PS2Native/README.txt" | tr '\n' ' ')"
	if [ -d "$rel/PS2Native/games" ] || [ -d "$rel/PS2Native/patches" ]; then stamp "the folder for others holds games or patches: stopping"; exit 1; fi
	mv "$rel/PS2Native.tar.xz" "$rel/$archive"
	echo "  $archive: $(stat -c %s "$rel/$archive") bytes"
	echo "  SHA-256 $(sha256sum "$rel/$archive" | cut -d' ' -f1)"
	if [ -n "$to" ]; then mkdir -p "$to" && cp "$rel/$archive" "$to/$archive" && echo "  copied to $to"; fi
fi

if has unpacked; then
	[ -f "$rel/$archive" ] || { stamp "no archive at $rel/$archive (the 'kit' part makes it): stopping"; exit 1; }
	check="$rel/check"
	rm -rf "$check"; mkdir -p "$check"
	tar -xf "$rel/$archive" -C "$check"
	there="$check/PS2Native"
	stamp "the archive unpacked into $check: $(find "$there" -type f | wc -l) files where the folder has $(find "$rel/PS2Native" -type f | wc -l)"
	if [ -n "$disc" ]; then
		echo "with nothing but the system on PATH: its runtime reads a disc"
		(cd "$there" && env -i PATH=/usr/bin:/bin ./runtime/ps2n-host -disc-executables "$disc" 2>&1 | head -4 | cut -c1-200 | sed 's/^/  | /')
	fi
	before="$(ls "$there/games" 2>/dev/null)"
	echo "its wizard makes a game from its disc image, every default accepted"
	start=$(date +%s)
	(cd "$there/runtime" && "${clean[@]}" "${quiet[@]}" ./ps2n-wizard --image "$image" --bios "$PS2N_BIOS" --auto > "$logs/wizard.log" 2>&1)
	echo "  the wizard: exit $? in $(( $(date +%s) - start )) s"
	game=""
	for made in "$there"/games/*/; do
		made="${made%/}"
		[ -f "$made/make.txt" ] || continue
		grep -qxF "$(basename "$made")" <<< "$before" || game="$made"
	done
	[ -n "$game" ] || { echo "  NO GAME was made"; tail -8 "$logs/wizard.log" | cut -c1-200 | sed 's/^/  | /'; stamp stopping; exit 1; }
	echo "  $(basename "$game")"
	describe "$game"
	echo "the wizard asked to bring that folder up to date"
	update_folder "$there" "$game"
	echo "the session $(basename "$session") run again whole from it, its pace written down"
	replay "$tree" "$game" "$session" 0 "release-$label-check" "" "$rel/check-pace.txt" || bad+=("the session run again from the unpacked archive was not the session")
fi

if has earlier && [ -z "$earlier" ]; then stamp "no folder of the release before given (--earlier): the update from it is not tried"; fi
if has earlier && [ -n "$earlier" ]; then
	[ -x "$rel/check/PS2Native/runtime/ps2n-wizard" ] || { stamp "no unpacked archive at $rel/check (the 'unpacked' part makes it): stopping"; exit 1; }
	copy="$rel/from-earlier/$(basename "$earlier")"
	rm -rf "$rel/from-earlier"; mkdir -p "$rel/from-earlier"
	cp -a "$earlier" "$copy"
	stamp "a copy of the folder the release before made, brought up to date by this archive's wizard"
	describe "$copy" | sed -n '3,3p'
	update_folder "$rel/check/PS2Native" "$copy"
	describe "$copy" | sed -n '3,4p'
	remake "$copy" --before-play || bad+=("the earlier folder did not compile")
	replay "$tree" "$copy" "$session" 600 "release-$label-earlier" || bad+=("the session run again from the earlier folder was not the session")
	echo "  asked again:"
	update_folder "$rel/check/PS2Native" "$copy"
fi

# What a game folder is: its programs, its modules and its memory cards, by fingerprint.
folders_now() {
	local f
	for f in "$theirs"/games/*/; do
		f="${f%/}"
		[ -x "$f/ps2n-host" ] || continue
		echo "$(basename "$f"): $(grep -v '^#' "$f/programs.txt" 2>/dev/null | tr '\n' ' ')modules $(cat "$f"/ps2n-*.so | sha256sum | cut -c1-16); runtime $(sha256sum "$f/ps2n-host" | cut -c1-16); cards $(cards "$f" | sha256sum | cut -c1-16); sessions $(ls "$f/sessions" 2>/dev/null | wc -l)"
	done
}
if has theirs; then
	not_playing "$theirs"
	before="$(folders_now)"
	start=$(date +%s)
	nice -n 10 bash scripts/linux/kit.sh --build-dir "$tree" --out "$theirs" > "$logs/theirs.log" 2>&1
	code=$?
	stamp "the maintainer's own PS2Native folder made again from the same tree: exit $code in $(( $(date +%s) - start )) s"
	grep -E "Its runtime|PCSX2's core|Warning|not a release" "$logs/theirs.log" | cut -c1-200 | sed 's/^/  | /'
	after="$(folders_now)"
	echo "  its game folders, as they were: $([ "$before" = "$after" ] && echo yes || echo NO)"
	sed 's/^/  | /' <<< "$after"
	[ "$code" = 0 ] && [ "$before" = "$after" ] || bad+=("the maintainer's PS2Native folder: see above")
fi

if has stock; then
	build_tree "stock PCSX2's tree, built as its releases are" "$logs/build-stock.log" --build-dir "$root/build/linux-clang-stock" --module on --runtime-lto on --runtime-pgo off --pcsx2-patches off
	ls -la --time-style=+%m-%d_%H:%M "$root/build/linux-clang-stock/ps2n/bin/ps2n-host" | awk '{print "  ", $5, $6, $7}'
fi

# one <name> <disc image> <folder> <session> <which> <round>: the session whole, nothing drawn.
one() {
	local name="$1" disc_image="$2" modules="$3" from="$4" which="$5" round="$6"
	local work="$tree/ps2n/tests/game/release-$label-$name-$which/run" frames a out marks seconds said
	frames="$(session_end "$from")"
	a=("-DWORK_DIR=$work" "-DELF=$disc_image" -DIMAGE=game -DSECONDS=3600 "-DUNTIL=@@ no such line @@" -DEXPECT_EXIT=2 -DWHOLE=1 -DTIMES=1 -DMARKS=1
		"-DINPUT=$from/input.txt" "-DMEMCARDS_FROM=$from" "-DNVRAM_FROM=$from" "-DFRAMES=$frames" "-DMODULES=$modules" -DPCSX2_VU_THREAD=1)
	case "$which" in
		stock) a+=("-DHOST=$root/build/linux-clang-stock/ps2n/bin/ps2n-host" -DEE=jit -DIOP=jit -DVU=jit -DSTATIC=on) ;;
		compiled) a+=("-DHOST=$tree/ps2n/bin/ps2n-host" -DEE=ps2n -DIOP=ps2n -DVU=jit -DSTATIC=on -DVU_PS2N=both
			"-DVU_CAPTURE=$work/vu-capture.toml" "-DIOP_CAPTURE=$work/iop-capture.toml" -DCAPTURE=1) ;;
	esac
	out="$("${quiet[@]}" cmake "${a[@]}" -P "$root/tests/guest_test.cmake" 2>&1)"
	marks="$(grep 'wall-clock time since the host started' <<< "$out" | tail -1)"
	seconds="$(sed -nE 's/.*frame 1 at ([0-9.]+) s.*frame [0-9]+ at ([0-9.]+) s$/\1 \2/p' <<< "$marks" | awk '{printf "%.2f", $2 - $1}')"
	said="$(grep -oE 'the run is the session at .*|parts from it.*' <<< "$out" | tail -1 | cut -c1-90)"
	echo "$(date +%H:%M:%S) release-$label $name $which round $round: ${seconds:-?} s; $said" | tee -a "$root/build/linux-times.txt"
	grep -q 'stopped at frame limit' <<< "$out" || { echo "  THE RUN DID NOT END AS EXPECTED"; tail -8 <<< "$out" | cut -c1-220 | sed 's/^/  | /'; }
}
if has time; then
	[ -f "$timed" ] || { stamp "no $timed: stopping"; exit 1; }
	echo "--- $(date +%F) release-$label ---" >> "$root/build/linux-times.txt"
	mapfile -t rows < <(read_list "$timed")
	for row in "${rows[@]}"; do
		IFS=$'\t' read -r name disc_image modules from <<< "$row"
		[ -n "$name" ] || continue
		for round in $(seq 1 "$rounds"); do
			order=(stock compiled)
			[ $((round % 2)) = 0 ] && order=(compiled stock)
			for which in "${order[@]}"; do one "$name" "$disc_image" "$modules" "$from" "$which" "$round"; done
		done
		echo "Against stock PCSX2 $(git -C third_party/pcsx2 describe --tags 2>/dev/null) built as its releases are, with its VU thread on, the compiled game with VU1 on PCSX2's VU thread; $rounds rounds in turns:"
		for which in stock compiled; do
			grep -E " release-$label $name $which round [0-9]+:" "$root/build/linux-times.txt" | tail -"$rounds" | sed -E 's/.*: ([0-9.]+) s;.*/\1/' |
				awk -v w="$which" -v n="$name" '{ s += $1; c++; t = t " " $1 } END { if (c) printf "  %s %s: mean %.2f s (%s )\n", n, w, s / c, t }'
		done
	done
fi

if [ "${#bad[@]}" -gt 0 ]; then
	stamp "NOT AS IT SHOULD BE:"
	printf '  %s\n' "${bad[@]}"
	stamp "release $label done ($parts), with ${#bad[@]} to look at"
	exit 1
fi
stamp "release $label done ($parts)"
