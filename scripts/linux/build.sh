#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Configures and builds PS2Native on Linux x64: the counterpart of
# scripts/windows/build.ps1, which says more about each option.
#
# It drives the CMake project of the pinned PCSX2 submodule, unmodified, and
# adds PS2Native's targets through cmake/ps2n-inject.cmake. First run on
# Ubuntu 22.04 under WSL 2 (docs/adr/0038), the system PCSX2 makes its own
# Linux releases on.
#
# Needs:
#   - CMake 3.25 or later (Qt 6.12, which PCSX2's dependencies include, asks
#     for it; Ubuntu 22.04's is 3.22: Kitware's own release will do), Ninja,
#     git, curl, and the development packages PCSX2's Linux build installs
#     (.github/workflows/linux_build_qt.yml in the submodule; docs/app-log.md,
#     "P4.7", has the list as installed here);
#   - clang and lld: LLVM's own 22.1.3 release, the version the Windows
#     builds and the wizard's toolchain use (docs/adr/0030), unpacked into
#     deps/llvm-22.1.3-linux, is taken when it is there; otherwise CC and CXX,
#     or clang and clang++ on PATH;
#   - PCSX2's dependencies (SDL3, Qt, FFmpeg, shaderc and the rest), built
#     from source into deps/linux-x64 by PCSX2's own script, as its CI builds
#     them (BUILD_FFMPEG=1), the first time. That downloads their sources and
#     takes about ten minutes on 20 processors.
#
# Usage:
#   scripts/linux/build.sh [options] [target ...]
#
#   --build-dir <dir>          the build tree (default build/linux-clang)
#   --reconfigure              run CMake's configure step again
#   --tools-only               only the build-time tools and their unit tests
#   --game <elf|none>          a game's boot executable, compiled into the host
#   --game-more <a|b|none>     the executables it loads itself (docs/adr/0026)
#   --game-capture <file|none> what earlier runs reported (ps2n-host -capture)
#   --bios <image|none>        a BIOS dump whose EE code is compiled in (docs/adr/0017)
#   --bios-capture <file|none>
#   --vu-capture <file|none>   --vu-selftest-capture <file|none>
#   --iop-capture <file|none>  --iop-selftest-capture <file|none>
#   --pgo <generate|use|off>   --pgo-data <file>
#   --lto <thin|off>
#   --runtime-lto <on|off>     the runtime itself built with link-time optimization, as PCSX2's
#                              own releases are
#   --runtime-pgo <generate|use|off>   --runtime-pgo-data <file>
#                              the runtime itself built with a profile of play (docs/adr/0046)
#   --module <on|off|imports>  the compiled code in modules of its own (ps2n-game.so and the
#                              others beside the host; docs/phase4-proposal.md, P4.1 and P4.2)
#   --wizard <on|off>          the wizard and ps2n-remake (P4.5, P4.6)
#   --pcsx2-source <dir>       another PCSX2 source tree in the place of the pinned submodule, in
#                              a build tree of its own (--build-dir): to try a newer PCSX2, or a
#                              change to PCSX2, against the tests here
#
# The default target is ps2n-host (with the modules, and the wizard, where the
# tree builds them). Each game, BIOS and capture option is remembered by the
# build tree until it is given again; "none" takes it out. What is generated
# from a game or a BIOS stays in the build tree.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pcsx2="$root/third_party/pcsx2"
deps="$root/deps/linux-x64"
build="$root/build/linux-clang"
llvm="$root/deps/llvm-22.1.3-linux/bin"

reconfigure=0
tools_only=0
other_pcsx2=0
targets=()
settings=()

# A path given for an option, absolute, or empty for "none".
path_of() {
	if [ "$1" = "none" ]; then
		echo ""
	else
		realpath "$1"
	fi
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--reconfigure) reconfigure=1 ;;
		--tools-only) tools_only=1 ;;
		--build-dir|--game|--game-more|--game-capture|--bios|--bios-capture|--vu-capture|--vu-selftest-capture|\
		--iop-capture|--iop-selftest-capture|--pgo|--pgo-data|--lto|--module|--wizard|\
		--runtime-lto|--runtime-pgo|--runtime-pgo-data|--pcsx2-source)
			if [ "$#" -lt 2 ]; then
				echo "$1 needs a value" >&2
				exit 1
			fi
			case "$1" in
				--build-dir) build="$(realpath -m "$2")" ;;
				--pcsx2-source)
					pcsx2="$(realpath "$2")"
					other_pcsx2=1
					if [ ! -f "$pcsx2/pcsx2/CMakeLists.txt" ]; then
						echo "$pcsx2 is not a PCSX2 source tree" >&2
						exit 1
					fi
					;;
				--game) settings+=("-DPS2N_GAME_ELF=$(path_of "$2")") ;;
				--game-more)
					more=""
					if [ "$2" != "none" ]; then
						IFS='|' read -r -a paths <<< "$2"
						for p in "${paths[@]}"; do
							[ -z "$p" ] && continue
							more="${more:+$more;}$(realpath "$p")"
						done
					fi
					settings+=("-DPS2N_GAME_MORE=$more")
					;;
				--game-capture) settings+=("-DPS2N_GAME_CAPTURE=$(path_of "$2")") ;;
				--bios) settings+=("-DPS2N_BIOS_IMAGE=$(path_of "$2")") ;;
				--bios-capture) settings+=("-DPS2N_BIOS_CAPTURE=$(path_of "$2")") ;;
				--vu-capture) settings+=("-DPS2N_VU_CAPTURE=$(path_of "$2")") ;;
				--vu-selftest-capture) settings+=("-DPS2N_VU_SELFTEST_CAPTURE=$(path_of "$2")") ;;
				--iop-capture) settings+=("-DPS2N_IOP_CAPTURE=$(path_of "$2")") ;;
				--iop-selftest-capture) settings+=("-DPS2N_IOP_SELFTEST_CAPTURE=$(path_of "$2")") ;;
				--pgo)
					case "$2" in
						generate|use) settings+=("-DPS2N_PGO=$2") ;;
						off) settings+=("-DPS2N_PGO=") ;;
						*) echo "--pgo is generate, use or off" >&2; exit 1 ;;
					esac
					;;
				--pgo-data) settings+=("-DPS2N_PGO_DATA=$(realpath "$2")") ;;
				--lto)
					case "$2" in
						thin) settings+=("-DPS2N_LTO=thin") ;;
						off) settings+=("-DPS2N_LTO=") ;;
						*) echo "--lto is thin or off" >&2; exit 1 ;;
					esac
					;;
				--runtime-lto)
					case "$2" in
						on) settings+=("-DPS2N_RUNTIME_LTO=ON") ;;
						off) settings+=("-DPS2N_RUNTIME_LTO=OFF") ;;
						*) echo "--runtime-lto is on or off" >&2; exit 1 ;;
					esac
					;;
				--runtime-pgo)
					case "$2" in
						generate|use) settings+=("-DPS2N_RUNTIME_PGO=$2") ;;
						off) settings+=("-DPS2N_RUNTIME_PGO=") ;;
						*) echo "--runtime-pgo is generate, use or off" >&2; exit 1 ;;
					esac
					;;
				--runtime-pgo-data) settings+=("-DPS2N_RUNTIME_PGO_DATA=$(realpath "$2")") ;;
				--module)
					case "$2" in
						on) settings+=("-DPS2N_GAME_MODULE=ON" "-DPS2N_GAME_IMPORTS=OFF") ;;
						off) settings+=("-DPS2N_GAME_MODULE=OFF" "-DPS2N_GAME_IMPORTS=OFF") ;;
						imports) settings+=("-DPS2N_GAME_MODULE=OFF" "-DPS2N_GAME_IMPORTS=ON") ;;
						*) echo "--module is on, off or imports" >&2; exit 1 ;;
					esac
					;;
				--wizard)
					case "$2" in
						on) settings+=("-DPS2N_WIZARD=ON") ;;
						off) settings+=("-DPS2N_WIZARD=OFF") ;;
						*) echo "--wizard is on or off" >&2; exit 1 ;;
					esac
					;;
			esac
			shift
			;;
		-*) echo "Unknown option: $1" >&2; exit 1 ;;
		*) targets+=("$1") ;;
	esac
	shift
done
if [ "${#targets[@]}" -eq 0 ]; then
	targets=(ps2n-host)
fi

if [ "$other_pcsx2" -eq 1 ] && [ "$build" = "$root/build/linux-clang" ]; then
	echo "Give another PCSX2 source tree a build tree of its own: --build-dir build/<name>" >&2
	exit 1
fi
if [ ! -f "$pcsx2/CMakeLists.txt" ]; then
	echo "The PCSX2 submodule is missing. Run: git submodule update --init --depth 1" >&2
	exit 1
fi

if [ -x "$llvm/clang" ]; then
	cc="${CC:-$llvm/clang}"
	cxx="${CXX:-$llvm/clang++}"
else
	cc="${CC:-clang}"
	cxx="${CXX:-clang++}"
fi
for tool in "$cc" "$cxx" cmake ninja; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Missing tool: $tool" >&2
		exit 1
	fi
done
cmake_version="$(cmake --version | head -1 | sed 's/[^0-9.]*//')"
if [ "$(printf '%s\n3.25\n' "$cmake_version" | sort -V | head -1)" != "3.25" ]; then
	echo "CMake $cmake_version is older than 3.25, which Qt 6.12 needs (see the top of this script)." >&2
	exit 1
fi

# 1. The build-time tools and their unit tests (core/, recompiler/,
#    tests/unit/). They do not depend on PCSX2.
tools="$root/build/tools-linux"
if [ "$reconfigure" -eq 1 ] || [ ! -f "$tools/build.ninja" ]; then
	cmake -S "$root" -B "$tools" -G Ninja \
		-DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$tools"
if [ "$tools_only" -eq 1 ]; then
	exit 0
fi

# 2. The run-time side (runtime/, host/, wizard/), inside PCSX2's own CMake
#    project. Its build runs the recompiler built in step 1 on the test
#    programs. PCSX2's dependencies come from its own script, as its CI makes
#    them.
if [ ! -f "$deps/lib/libSDL3.so" ]; then
	echo "Building PCSX2's dependencies into $deps with PCSX2's own script..."
	mkdir -p "$root/deps"
	# The script builds in ./deps-build under the current directory.
	(cd "$root/deps" && BUILD_FFMPEG=1 "$pcsx2/.github/workflows/scripts/linux/build-dependencies-qt.sh" "$deps")
fi

if [ "$reconfigure" -eq 1 ] || [ "${#settings[@]}" -gt 0 ] || [ ! -f "$build/build.ninja" ]; then
	# The flags PCSX2's own CI uses for Linux, minus its Qt interface, link-time
	# optimization, ccache and FFmpeg linked in (PCSX2 then loads it only to
	# record video, as on Windows).
	cmake -S "$pcsx2" -B "$build" -G Ninja \
		-DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_MODULE_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_SHARED_LINKER_FLAGS_INIT="-fuse-ld=lld" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH="$deps" \
		-DDISABLE_ADVANCE_SIMD=ON \
		-DENABLE_SETCAP=OFF \
		-DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF \
		-DCMAKE_PROJECT_Pcsx2_INCLUDE="$root/cmake/ps2n-inject.cmake" \
		-DPS2N_ROOT="$root" \
		-DPS2N_RECOMP="$tools/bin/ps2n-recomp" \
		${settings[@]+"${settings[@]}"}
fi

# With the compiled code in modules of its own, the host does not run without
# them; and the wizard, where the tree builds it, with ps2n-remake (P4.6).
cache="$build/CMakeCache.txt"
for t in "${targets[@]}"; do
	if [ "$t" = "ps2n-host" ] && [ -f "$cache" ]; then
		if grep -q '^PS2N_GAME_MODULE:BOOL=ON$' "$cache"; then
			targets+=(ps2n-modules)
		fi
		if grep -q '^PS2N_WIZARD:BOOL=ON$' "$cache"; then
			targets+=(ps2n-wizard ps2n-remake)
		fi
		break
	fi
done

cmake --build "$build" --target "${targets[@]}"
