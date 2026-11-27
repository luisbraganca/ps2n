#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Brings a clone of the repository inside WSL to where the Windows tree is.
#
# Linux is built and tested in a clone inside WSL, never on /mnt/c: Git for
# Windows gives a working tree there CRLF line endings, which PCSX2's shell
# scripts do not run with. scripts/windows/wsl.ps1 runs this before one of
# scripts/linux, from the Windows tree's own copy, so that it is the newest.
#
#   --mode commit   (default) the clone's git moved to the Windows tree's
#                   commit. Where the clone's files differ from its own commit
#                   they must be that commit's already (a tree sync that was
#                   committed since): otherwise it is left as it was, and this
#                   says which files differ. Nothing of the clone is discarded
#   --mode tree     the Windows working tree as it is, changes not committed
#                   and files not yet in git too: its commit and a patch of
#                   its changes are put together in a staging clone beside the
#                   clone, and the files whose content differs copied over, so
#                   that files that did not change keep their times and
#                   nothing is built again for nothing. build, deps, .git and
#                   third_party are left alone, whether folders, files or links (in a
#                   worktree .git is a file, and deps may be a link)
#
# Where the tree names another commit of PCSX2 than the clone's submodule
# holds, that commit is fetched into it (from --pcsx2, a clone that has it, or
# from PCSX2's own repository) and checked out.
#
# Usage:
#   bash /mnt/c/<the Windows tree>/scripts/linux/sync.sh --from /mnt/c/<the Windows tree> [options]
#   --clone <dir>     the clone inside WSL (default ~/ps2n)
#   --mode <mode>     commit or tree
#   --staging <dir>   for --mode tree: where wsl.ps1 put tree.patch and untracked.txt
#   --files <dir>     for --mode tree: where the files not yet in git are read from (default --from: a
#                     worktree made on Windows names its repository by a Windows path, which git here
#                     cannot follow, so its commit is taken from elsewhere and its files from it)
#   --pcsx2 <dir>     a clone of PCSX2 that has the commit the tree names

set -euo pipefail

from=""
clone="$HOME/ps2n"
mode=commit
staging=""
files=""
pcsx2=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--from) from="$2"; shift 2 ;;
		--clone) clone="${2/#\~/$HOME}"; shift 2 ;;
		--mode) mode="$2"; shift 2 ;;
		--staging) staging="$2"; shift 2 ;;
		--files) files="$2"; shift 2 ;;
		--pcsx2) pcsx2="$2"; shift 2 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
done
if ! git -C "$from" rev-parse --git-dir > /dev/null 2>&1; then echo "--from is not a git tree git can read here: $from" >&2; exit 1; fi
[ -n "$files" ] || files="$from"
if [ ! -e "$clone/.git" ]; then echo "No clone at $clone." >&2; exit 1; fi

# Files new to the clone come with the modes Windows' drive gives them: put
# back to what git has, their times untouched.
fix_modes() {
	git diff --summary | sed -nE 's/^ mode change ([0-7]+) => [0-7]+ (.*)$/\1 \2/p' | while IFS=' ' read -r was file; do
		chmod "${was: -3}" "$file"
	done
}

case "$mode" in
	commit)
		cd "$clone"
		git fetch -q "$from" HEAD
		want="$(git rev-parse FETCH_HEAD)"
		if [ -z "$(git status --porcelain)" ]; then
			if [ "$(git rev-parse HEAD)" != "$want" ]; then
				git merge -q --ff-only FETCH_HEAD || { echo "The clone cannot be moved forward to $want: it has commits of its own." >&2; exit 1; }
			fi
		else
			was="$(git rev-parse HEAD)"
			git reset -q --mixed FETCH_HEAD
			fix_modes
			if [ -n "$(git status --porcelain)" ]; then
				echo "The clone has changes that are not $want's: left as it was. They are:" >&2
				git status --short | head -20 >&2
				git reset -q --mixed "$was"
				exit 1
			fi
		fi
		;;
	tree)
		if [ ! -f "$staging/tree.patch" ] || [ ! -f "$staging/untracked.txt" ]; then echo "--mode tree needs --staging, with tree.patch and untracked.txt in it." >&2; exit 1; fi
		stage="$clone-stage"
		[ -d "$stage/.git" ] || git clone -q "$from" "$stage"
		cd "$stage"
		git fetch -q "$from" HEAD
		git reset -q --hard FETCH_HEAD
		git clean -fdq
		if [ -s "$staging/tree.patch" ]; then git apply --whitespace=nowarn "$staging/tree.patch"; fi
		while IFS= read -r f; do
			[ -n "$f" ] || continue
			mkdir -p "$(dirname "$f")"
			cp "$files/$f" "$f"
			# A text file not yet in git has Windows' line endings.
			if grep -Iq . "$f"; then sed -i 's/\r$//' "$f"; fi
		done < "$staging/untracked.txt"
		changed="$(rsync -rci --delete --exclude=/build --exclude=/deps --exclude=/.git --exclude=/third_party "$stage/" "$clone/" | grep -c '^[<>c*]' || true)"
		cd "$clone"
		echo "the working tree's changes carried over: $changed files written"
		;;
	*) echo "Unknown mode: $mode" >&2; exit 1 ;;
esac

# PCSX2's commit, where the tree names another than the submodule holds.
cd "$clone"
named="$(git ls-files -s third_party/pcsx2 | awk '{print $2}')"
if [ "$mode" = tree ]; then named="$(git -C "$from" ls-files -s third_party/pcsx2 | awk '{print $2}')"; fi
held="$(git -C third_party/pcsx2 rev-parse HEAD 2>/dev/null || true)"
if [ -n "$named" ] && [ "$named" != "$held" ]; then
	echo "PCSX2: the tree names $named, the clone holds ${held:-nothing}: fetching it"
	if [ -n "$pcsx2" ]; then
		# By its name where that clone lets it be asked for so, by its tags otherwise.
		git -C third_party/pcsx2 fetch -q --depth 1 "file://$pcsx2" "$named" 2>/dev/null ||
			git -C third_party/pcsx2 fetch -q --depth 1 "file://$pcsx2" 'refs/tags/*:refs/tags/*'
	else
		git -C third_party/pcsx2 fetch -q --depth 1 origin "$named"
	fi
	git -C third_party/pcsx2 checkout -q --detach "$named"
fi

echo "the clone is at $(git rev-parse --short HEAD) ($(git log -1 --format=%s | cut -c1-70)); files differing from it: $(git status --porcelain | wc -l); PCSX2 $(git -C third_party/pcsx2 describe --tags 2>/dev/null || git -C third_party/pcsx2 rev-parse --short HEAD)"
