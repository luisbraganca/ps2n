#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gives the game folders of a PS2Native folder this tree's programs, and checks
# each: the counterpart of scripts/windows/update-games.ps1, which says more.
#
#   build    the tree the folder is made from, built (nothing to do, usually)
#   kit      the PS2Native folder made again from it (kit.sh keeps its games)
#   saves    each game folder's memory cards copied aside first
#   update   <kit>/runtime/ps2n-wizard --update <folder>, without a window
#   remake   the folder's own ps2n-remake --before-play, its modules put in
#            place as the launcher puts them
#   check    the start of one of the folder's sessions run again with the new
#            programs: it must be the session at every fingerprint compared. A
#            session played with VU1 on PCSX2's VU thread is run again so
#   again    the wizard asked again; the memory cards compared
#
# Which session: the folder's newest of --frames frames or more; where that one
# parts from itself, the one before, three at most; where none is that long,
# the longest there is, whole. A list says otherwise (--sessions): a line
# "<folder's name> | <session's name or path>", or "<folder's name> | -".
#
# It stops where something of the PS2Native folder runs. Half the processors.
# Make the folder only from a tree with nothing left to commit: its licences
# name the commit.
#
# Usage:
#   PS2N_BIOS=/path/to/bios.bin scripts/linux/update-games.sh [options]
#   --kit <dir>        the PS2Native folder (default build/PS2Native)
#   --games <dir>      a folder of game folders other than <kit>/games: copies, for a trial
#   --parts <list>     default build,kit,saves,update,remake,check,again
#   --only <names>     only the folders whose names hold one of these (comma-separated)
#   --except <names>   not the folders with these names (comma-separated)
#   --sessions <file>  the list of sessions to check with (default build/update-sessions.txt)
#   --frames <n>       how much of a session is run again (default 600)
#   --build-dir <dir>  the tree the folder is made from (default build/linux-clang-module)

set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

kit="$root/build/PS2Native"
games=""
parts="build,kit,saves,update,remake,check,again"
only=""
except=""
sessions="$root/build/update-sessions.txt"
frames=600
tree="$root/build/linux-clang-module"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--kit) kit="$(realpath -m "$2")"; shift 2 ;;
		--games) games="$(realpath -m "$2")"; shift 2 ;;
		--parts) parts="$2"; shift 2 ;;
		--only) only="$2"; shift 2 ;;
		--except) except="$2"; shift 2 ;;
		--sessions) sessions="$(realpath -m "$2")"; shift 2 ;;
		--frames) frames="$2"; shift 2 ;;
		--build-dir) tree="$(realpath -m "$2")"; shift 2 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
[ -n "$games" ] || games="$kit/games"
has() { [[ ",$parts," == *",$1,"* ]]; }
if has check && [ -z "${PS2N_BIOS:-}" ]; then echo "A BIOS image is needed for the check: set PS2N_BIOS." >&2; exit 1; fi
work="$root/build/update-games"
mkdir -p "$work"
cd "$root" || exit 1

# wanted <folder's name>: by --only and --except.
wanted() {
	local name="$1" one
	IFS=',' read -ra list <<< "$except"
	for one in "${list[@]}"; do [ "$(echo "$one" | xargs)" != "$name" ] || return 1; done
	[ -n "$only" ] || return 0
	IFS=',' read -ra list <<< "$only"
	for one in "${list[@]}"; do
		one="$(echo "$one" | xargs)"
		[ -z "$one" ] || [[ "$name" != *"$one"* ]] || return 0
	done
	return 1
}

# check_sessions <folder> <name>: the sessions to check with, the first to try first, one to a line.
check_sessions() {
	local folder="$1" name="$2" chosen s end
	chosen="$(read_list "$sessions" | awk -F'\t' -v n="$name" '$1 == n { print $2 }' | tail -1)"
	if [ -n "$chosen" ]; then
		[ "$chosen" != - ] || return 0
		[[ "$chosen" == /* ]] || chosen="$folder/sessions/$chosen"
		echo "$chosen"
		return 0
	fi
	local long=()
	while IFS= read -r s; do
		end="$(session_end "$s")"
		[ -n "$end" ] && [ "$end" -ge "$frames" ] && long+=("$s")
	done < <(ls -d "$folder"/sessions/*/ 2>/dev/null | sed 's:/$::' | sort -r)
	if [ "${#long[@]}" -gt 0 ]; then
		printf '%s\n' "${long[@]}" | head -3
		return 0
	fi
	# None is that long: the longest there is, whole.
	for s in "$folder"/sessions/*/; do
		[ -f "$s/input.txt" ] || continue
		echo "$(session_end "${s%/}") ${s%/}"
	done | sort -n | tail -1 | cut -d' ' -f2-
}

not_playing "$kit"
stamp "update-games ($parts) at $(git rev-parse --short HEAD), files differing from it: $(git status --porcelain | wc -l)"

if has build; then
	build_tree "the tree the folder is made from, built" "$work/build.log" --build-dir "$tree"
fi
if has kit; then
	start=$(date +%s)
	nice -n 10 bash scripts/linux/kit.sh --build-dir "$tree" --out "$kit" > "$work/kit.log" 2>&1
	code=$?
	stamp "the PS2Native folder made again, its games kept: exit $code in $(( $(date +%s) - start )) s"
	grep -E "Its runtime|PCSX2's core|Warning|not a release" "$work/kit.log" | cut -c1-200 | sed 's/^/  | /'
	[ "$code" = 0 ] || { tail -8 "$work/kit.log" | sed 's/^/  | /'; stamp stopping; exit 1; }
fi
[ -x "$kit/runtime/ps2n-wizard" ] || { echo "No wizard at $kit/runtime/ps2n-wizard: make the PS2Native folder first (the 'kit' part)." >&2; exit 1; }
echo "  its licences say: $(grep -E '^commit [0-9a-f]{40}|changes not committed' "$kit/licences/README.txt" 2>/dev/null | head -2 | tr '\n' ' ')"

bad=()
summary=()
for folder in "$games"/*/; do
	folder="${folder%/}"
	name="$(basename "$folder")"
	[ -x "$folder/ps2n-host" ] || continue
	wanted "$name" || continue
	not_playing "$kit"
	stamp "=== $name"
	describe "$folder"
	compiled=""
	checked=""
	cards_before="$(cards "$folder")"
	sessions_before="$(ls "$folder/sessions" 2>/dev/null | wc -l)"
	echo "  sessions: $sessions_before; its counts: $([ -f "$folder/captures/profile.profdata" ] && stat -c %s "$folder/captures/profile.profdata" || echo none)"
	if has saves; then
		rm -rf "$work/saves/$name"
		while IFS= read -r card; do
			[ -n "$card" ] || continue
			mkdir -p "$work/saves/$name/$(dirname "${card#"$folder"/}")"
			cp -p "$card" "$work/saves/$name/${card#"$folder"/}"
		done < <(find "$folder" -name '*.ps2' -not -path '*/sessions/*' -not -path '*/work/*')
		echo "  its memory cards copied aside: $(grep -c . <<< "$cards_before")"
	fi
	if has update; then
		update_folder "$kit" "$folder"
		describe "$folder"
	fi
	if has remake; then
		start=$(date +%s)
		if remake "$folder" --before-play; then compiled="$(( $(date +%s) - start )) s"; else compiled="FAILED"; bad+=("$name: its compile failed"); fi
		describe "$folder"
	fi
	if has check; then
		passed=""
		tried=""
		mapfile -t candidates < <(check_sessions "$folder" "$name")
		for session in "${candidates[@]}"; do
			[ -n "$session" ] || continue
			tried=1
			end="$(session_end "$session")"
			[ -n "$end" ] || { echo "  $session has no input.txt"; continue; }
			count="$frames"
			[ "$end" -ge "$frames" ] || count="$end"
			thread=""
			grep -q '^vu-thread on' "$session/input.txt" && thread=thread
			if replay "$tree" "$folder" "$session" "$count" "update-${name//[^A-Za-z0-9]/}" "$thread"; then
				passed=1
				checked="$count frames of $(basename "$session"): ${replay_said#the run is the session at }"
				break
			fi
			echo "  NOT THE SESSION: the one before is tried"
		done
		[ -n "$tried" ] || { echo "  no session to check with"; checked="no session"; }
		if [ -n "$tried" ] && [ -z "$passed" ]; then checked="NOT THE SESSION"; bad+=("$name: no session ran again as itself"); fi
	fi
	if has again; then
		echo "  asked again:"
		update_folder "$kit" "$folder"
		cards_after="$(cards "$folder")"
		same=yes
		[ "$cards_before" = "$cards_after" ] || { same=NO; bad+=("$name: its memory cards differ"); }
		echo "  its memory cards: $(grep -c . <<< "$cards_after"), the same as before: $same; sessions: $(ls "$folder/sessions" 2>/dev/null | wc -l) where $sessions_before"
		echo "  left to compile or count: next-* $(ls -d "$folder"/work/next-* 2>/dev/null | wc -l); brought-up-to-date.txt $([ -f "$folder/work/brought-up-to-date.txt" ] && echo there || echo none); count-again.txt $([ -f "$folder/work/count-again.txt" ] && echo there || echo none)"
	fi
	summary+=("$(printf '%-42s compiled: %-10s checked: %s' "$name" "$compiled" "$checked")")
done

stamp "in short:"
printf '  %s\n' "${summary[@]:-none}"
if [ "${#bad[@]}" -gt 0 ]; then
	stamp "NOT AS IT SHOULD BE:"
	printf '  %s\n' "${bad[@]}"
	stamp "update-games done ($parts), with ${#bad[@]} to look at"
	exit 1
fi
stamp "update-games done ($parts)"
