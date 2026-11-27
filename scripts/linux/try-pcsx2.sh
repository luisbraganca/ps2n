#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tries PS2Native on a newer PCSX2 inside WSL: the Linux half of
# scripts/windows/try-pcsx2.ps1, which starts it (its 'linux' part) and says
# more. It works in a worktree beside the clone, on a branch of its own, and
# moves nothing in the clone.
#
#   setup    the worktree (<clone>-pcsx2-<n>, the branch pcsx2-<version>), PCSX2
#            at the tag in it, cloned from the copy the Windows side fetched
#            (--pcsx2), and its deps: the clone's own where PCSX2's script for
#            its Linux dependencies did not change, built again there otherwise
#   sync     the Windows worktree's changes carried into it (--from, --staging):
#            PS2Native following PCSX2 is written once, on Windows
#   build    the development tree and the tree a release is made from, with the
#            clone's settings
#   test     every test of both, counted
#   games    a PS2Native folder made from the worktree; a copy of each folder of
#            --timed brought up to date with it and compiled; each session run
#            again whole, by stock PCSX2 and by the compiled game, on the
#            version followed and on the one tried. One run of each
#   remove   the worktree and its branch taken away
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/try-pcsx2.sh --tag <tag> [options]
#   --tag <tag>        PCSX2's version to try, as v2.9.198
#   --parts <list>     default setup,sync,build,test,games
#   --from <dir>       the Windows worktree, as WSL sees it
#   --staging <dir>    where the Windows side put that worktree's changes (tree.patch, untracked.txt)
#   --pcsx2 <dir>      a clone of PCSX2 that has the tag, as WSL sees it
#   --timed <file>     for 'games': lines "<name> | <disc image> | <folder> | <session>" (default build/timed.txt)

set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

tag=""
parts="setup,sync,build,test,games"
from=""
staging=""
pcsx2=""
timed="$root/build/timed.txt"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--tag) tag="$2"; shift 2 ;;
		--parts) parts="$2"; shift 2 ;;
		--from) from="$2"; shift 2 ;;
		--staging) staging="$2"; shift 2 ;;
		--pcsx2) pcsx2="$2"; shift 2 ;;
		--timed) timed="$(realpath -m "$2")"; shift 2 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
[[ "$tag" =~ ^v[0-9.]+$ ]] || { echo "A tag of PCSX2's is needed, as --tag v2.9.198." >&2; exit 1; }
has() { [[ ",$parts," == *",$1,"* ]]; }
number="${tag#v}"
branch="pcsx2-$number"
w="$root-pcsx2-${number##*.}"
pinned="$(git -C "$root/third_party/pcsx2" describe --tags 2> /dev/null || git -C "$root/third_party/pcsx2" rev-parse --short HEAD)"
cd "$root" || exit 1
stamp "try-pcsx2 ($parts): the clone follows PCSX2 $pinned; trying $tag in $w, on the branch $branch"

if has remove; then
	# Its deps may be a link to the clone's: rm takes a link away, never what it points to.
	[ ! -L "$w/deps" ] || rm "$w/deps"
	rm -rf "$w" "$w-stage"
	git worktree prune
	git branch -D "$branch" 2>/dev/null
	stamp "the worktree and its branch removed; the clone's deps still there: $([ -d "$root/deps/linux-x64" ] && echo yes || echo NO)"
	exit 0
fi

if has setup; then
	[ -d "$pcsx2/.git" ] || { echo "A clone of PCSX2 that has $tag is needed: --pcsx2 <dir>." >&2; exit 1; }
	if [ ! -d "$w" ]; then
		if git show-ref --verify --quiet "refs/heads/$branch"; then git worktree add "$w" "$branch" 2>&1 | tail -1; else git worktree add "$w" -b "$branch" 2>&1 | tail -1; fi
	fi
	cd "$w" || exit 1
	sha="$(git -C "$pcsx2" rev-parse "$tag^{commit}")"
	git update-index --cacheinfo "160000,$sha,third_party/pcsx2"
	if [ ! -f third_party/pcsx2/CMakeLists.txt ]; then
		rmdir third_party/pcsx2 2>/dev/null
		git clone --quiet --depth 1 --branch "$tag" "file://$pcsx2" third_party/pcsx2 2>&1 | grep -v 'detached HEAD\|advice\|git switch\|^$' | head -3
	fi
	# The console's test programs, from the clone's own copy at the commit the tree names.
	tests_sha="$(git ls-files -s third_party/ps2autotests | awk '{print $2}')"
	if [ ! -e third_party/ps2autotests/.git ]; then
		rmdir third_party/ps2autotests 2>/dev/null
		git clone -q "file://$root/third_party/ps2autotests" third_party/ps2autotests && git -C third_party/ps2autotests checkout -q --detach "$tests_sha"
	fi
	stamp "PCSX2 in the worktree: $(git -C third_party/pcsx2 describe --tags) $(git -C third_party/pcsx2 rev-parse --short HEAD); CR in its CMakeLists: $(tr -cd '\r' < third_party/pcsx2/CMakeLists.txt | wc -c)"
	if [ ! -e deps ]; then
		scripts_of() { (cd "$1/.github/workflows/scripts" && cat linux/build-dependencies-qt.sh common/* 2>/dev/null | sha256sum | cut -d' ' -f1); }
		if [ "$(scripts_of "$root/third_party/pcsx2")" = "$(scripts_of "$w/third_party/pcsx2")" ]; then
			ln -s "$root/deps" deps
			echo "  its deps are the clone's (a link): PCSX2's script for its Linux dependencies did not change"
		else
			# Everything but PCSX2's own dependencies is the clone's; those, build.sh builds with PCSX2's script the first time.
			mkdir deps
			for entry in "$root"/deps/*; do
				case "$(basename "$entry")" in linux-x64|deps-build) ;; *) ln -s "$entry" "deps/$(basename "$entry")" ;; esac
			done
			echo "  PCSX2's script for its Linux dependencies CHANGED: they are built again in the worktree, by the first build"
		fi
	fi
	cd "$root" || exit 1
fi
[ -d "$w" ] || { stamp "no worktree at $w (the 'setup' part makes it): stopping"; exit 1; }

if has sync; then
	[ -n "$from" ] && [ -n "$staging" ] || { echo "'sync' needs --from and --staging." >&2; exit 1; }
	# The commit is the worktree's own here; the files not yet in git are read from the Windows worktree.
	bash "$root/scripts/linux/sync.sh" --from "$w" --files "$from" --clone "$w" --mode tree --staging "$staging" ${pcsx2:+--pcsx2 "$pcsx2"} || { stamp "the changes were not carried over: stopping"; exit 1; }
	git -C "$w" status --short --ignore-submodules=all | head -40 | sed 's/^/  /'
fi

# tree_settings <a build tree of the clone>: the settings it remembers, as build.sh's arguments, one to a line.
tree_settings() {
	local cache="$1/CMakeCache.txt"
	[ -f "$cache" ] || { echo "Build $1 in the clone first: the worktree's trees take its settings." >&2; return 1; }
	value() { sed -nE "s/^$1:[A-Z]+=(.*)$/\1/p" "$cache" | head -1; }
	local name option v
	while read -r name option; do
		v="$(value "$name")"
		echo "$option"; echo "${v:-none}"
	done <<-'EOF'
		PS2N_GAME_ELF --game
		PS2N_GAME_CAPTURE --game-capture
		PS2N_BIOS_IMAGE --bios
		PS2N_BIOS_CAPTURE --bios-capture
		PS2N_VU_CAPTURE --vu-capture
		PS2N_VU_SELFTEST_CAPTURE --vu-selftest-capture
		PS2N_IOP_CAPTURE --iop-capture
		PS2N_IOP_SELFTEST_CAPTURE --iop-selftest-capture
	EOF
	v="$(value PS2N_GAME_MORE)"
	echo "--game-more"
	if [ -n "$v" ]; then echo "${v//;/|}"; else echo none; fi
	if [ "$(value PS2N_PGO)" = use ] && [ -n "$(value PS2N_PGO_DATA)" ]; then printf '%s\n' --pgo use --pgo-data "$(value PS2N_PGO_DATA)"; else printf '%s\n' --pgo off; fi
	v="$(value PS2N_LTO)"; printf '%s\n' --lto "${v:-off}"
	[ "$(value PS2N_RUNTIME_LTO)" = ON ] && printf '%s\n' --runtime-lto on || printf '%s\n' --runtime-lto off
	if [ "$(value PS2N_RUNTIME_PGO)" = use ] && [ -n "$(value PS2N_RUNTIME_PGO_DATA)" ]; then printf '%s\n' --runtime-pgo use --runtime-pgo-data "$(value PS2N_RUNTIME_PGO_DATA)"; else printf '%s\n' --runtime-pgo off; fi
	if [ "$(value PS2N_GAME_IMPORTS)" = ON ]; then printf '%s\n' --module imports; elif [ "$(value PS2N_GAME_MODULE)" = ON ]; then printf '%s\n' --module on; else printf '%s\n' --module off; fi
	[ "$(value PS2N_WIZARD)" = ON ] && printf '%s\n' --wizard on || printf '%s\n' --wizard off
	[ "$(value PS2N_PCSX2_PATCHES)" = OFF ] && printf '%s\n' --pcsx2-patches off || printf '%s\n' --pcsx2-patches on
}

logs="$w/build/try-logs"
mkdir -p "$logs"

if has build; then
	for name in linux-clang linux-clang-module; do
		mapfile -t settings < <(tree_settings "$root/build/$name")
		[ "${#settings[@]}" -gt 0 ] || { stamp "no settings for build/$name: stopping"; exit 1; }
		start=$(date +%s)
		(cd "$w" && nice -n 10 bash scripts/linux/build.sh --build-dir "build/$name" --reconfigure "${settings[@]}" > "$logs/build-$name.log" 2>&1)
		code=$?
		stamp "build/$name in the worktree: exit $code in $(( $(date +%s) - start )) s"
		grep -E "PS2Native: PCSX2's code" "$logs/build-$name.log" | tail -1 | cut -c1-220 | sed 's/^/  /'
		if [ "$code" != 0 ]; then
			echo "  it does not build. What the build said first:"
			grep -E 'error|FAILED|static assertion|static_assert|does not apply' "$logs/build-$name.log" | head -25 | cut -c1-260 | sed 's/^/  | /'
			stamp "stopping (the whole log: $logs/build-$name.log)"
			exit 1
		fi
	done
fi

if has test; then
	[ -n "${PS2N_BIOS:-}" ] || { echo "A BIOS image is needed: set PS2N_BIOS." >&2; exit 1; }
	for name in linux-clang linux-clang-module; do
		start=$(date +%s)
		(cd "$w" && bash scripts/linux/test.sh --build-dir "build/$name" --jobs "$half" > "$logs/test-$name.log" 2>&1)
		code=$?
		log="$logs/test-$name.log"
		failed=$(grep -E '^[0-9]+% tests passed' "$log" | tail -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\1/')
		total=$(grep -E '^[0-9]+% tests passed' "$log" | tail -1 | sed -E 's/.*, ([0-9]+) tests failed out of ([0-9]+).*/\2/')
		# ctest may write a test's labels after its status: nothing is required after it.
		skipped=$(grep -cE '^[[:space:]]*[0-9]+ - .+ \(Skipped\)' "$log")
		disabled=$(grep -cE '^[[:space:]]*[0-9]+ - .+ \(Disabled\)' "$log")
		stamp "build/$name, every test: exit $code in $(( $(date +%s) - start )) s: $(( ${total:-0} - ${failed:-0} - skipped )) passed, ${failed:-0} failed, $skipped skipped, $disabled disabled"
		grep -E '^[[:space:]]*[0-9]+ - .+ \((Failed|Timeout|Not Run|SEGFAULT)\)' "$log" | head -40 | cut -c1-160 | sed 's/^/  /'
	done
fi

# play <runtime's folder> <modules' folder> <stock|compiled> <disc image> <session> <name>: the session whole, VU1 on the main thread.
play() {
	local folder="$1" modules="$2" config="$3" image="$4" session="$5" name="$6"
	local frames work how=() start out
	frames="$(session_end "$session")"
	work="$w/build/try-runs/$name"
	rm -rf "$work"; mkdir -p "$work"
	if [ "$config" = stock ]; then
		how=(-DEE=jit -DIOP=jit -DVU=jit -DSTATIC=on "-DMODULES=$modules")
	else
		how=(-DEE=ps2n -DIOP=ps2n -DVU=jit -DSTATIC=on -DVU_PS2N=both "-DVU_CAPTURE=$work/vu-capture.toml" "-DIOP_CAPTURE=$work/iop-capture.toml" -DCAPTURE=1)
	fi
	start=$(date +%s)
	out="$("${quiet[@]}" cmake "-DHOST=$folder/ps2n-host" "-DWORK_DIR=$work" "-DELF=$image" "${how[@]}" -DIMAGE=game -DSECONDS=1800 \
		"-DUNTIL=@@ no such line @@" -DEXPECT_EXIT=2 -DWHOLE=1 -DTIMES=1 "-DINPUT=$session/input.txt" "-DMEMCARDS_FROM=$session" "-DNVRAM_FROM=$session" \
		"-DFRAMES=$frames" -P "$w/tests/guest_test.cmake" 2>&1 < /dev/null)"
	echo "  $name ($config): $frames frames in $(( $(date +%s) - start )) s: $(grep -oE 'the run is the session at .*|parts from it.*' <<< "$out" | tail -1 | cut -c1-150)"
	[ "$config" = stock ] || grep -E 'EE cycles' <<< "$out" | tail -1 | sed 's/^ps2n-host: //' | cut -c1-170 | sed 's/^/    /'
	grep -q 'stopped at frame limit' <<< "$out" || { echo "    THE RUN DID NOT END AS EXPECTED"; tail -6 <<< "$out" | cut -c1-220 | sed 's/^/    | /'; }
}

if has games; then
	[ -n "${PS2N_BIOS:-}" ] || { echo "A BIOS image is needed: set PS2N_BIOS." >&2; exit 1; }
	[ -f "$timed" ] || { stamp "no $timed: stopping"; exit 1; }
	kit="$w/build/kit-try"
	start=$(date +%s)
	(cd "$w" && nice -n 10 bash scripts/linux/kit.sh --out "$kit" > "$logs/kit.log" 2>&1)
	code=$?
	stamp "a PS2Native folder made from the worktree: exit $code in $(( $(date +%s) - start )) s"
	[ "$code" = 0 ] || { tail -8 "$logs/kit.log" | sed 's/^/  | /'; stamp stopping; exit 1; }
	mapfile -t rows < <(read_list "$timed")
	for row in "${rows[@]}"; do
		IFS=$'\t' read -r name image folder session <<< "$row"
		[ -n "$name" ] || continue
		copy="$kit/games/$name"
		rm -rf "$copy"; mkdir -p "$kit/games"
		cp -a "$folder" "$copy"
		rm -rf "$copy/sessions"
		stamp "=== $name: a copy of $folder"
		update_folder "$kit" "$copy"
		# It is played with the counts it has.
		rm -f "$copy/work/count-again.txt"
		remake "$copy" --before-play
		for config in stock compiled; do
			play "$folder" "$folder" "$config" "$image" "$session" "$name-$pinned-$config"
			play "$copy" "$copy" "$config" "$image" "$session" "$name-$tag-$config"
		done
	done
fi

stamp "try-pcsx2 done ($parts)"
