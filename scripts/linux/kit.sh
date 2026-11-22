#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Puts together, on Linux, the folder a computer with nothing else of
# PS2Native's makes games with and plays them from (Phase 4, step P4.7): the
# counterpart of scripts/windows/kit.ps1. By default build/kit-linux:
#
#   PS2Native    the launcher (wizard/launcher_main.cpp), which starts the wizard
#   runtime/     ps2n-host, ps2n-wizard, ps2n-remake and ps2n-launcher, the
#                libraries they need, Qt's plugins (qt/plugins, which qt.conf
#                names) and PCSX2's resources. A game's folder gets all of it but
#                the wizard (runtime/wizard-files.txt), and a copy of the toolchain.
#   toolchain/   what compiles a game's modules: LLVM's own clang and ld.lld
#                (22.1.3, docs/adr/0030; their symbols stripped) with the
#                compiler's own headers and the libraries ld.lld needs,
#                ps2n-recomp, PS2Native's headers the generated code includes,
#                and a module's entry (runtime/module_entry.cpp); and what a
#                game's folder counts what of its code runs with (docs/adr/0045):
#                LLVM's llvm-profdata and profile runtime, and
#                runtime/module_counts.cpp
#   licences/    the licence of everything the folder carries
#
# The libraries: PCSX2's dependencies (built by PCSX2's own script into
# deps/linux-x64), and those of this system's that a desktop Linux may not
# have. What the folder leaves to the system is what every AppImage leaves to
# it, the list linuxdeploy follows, with which PCSX2 makes its own Linux
# release from the same Ubuntu 22.04: glibc 2.35 or later, libstdc++, the
# graphics driver, X11, fonts, ALSA. Every program and library finds the others
# beside it ($ORIGIN).
#
# Usage: scripts/linux/kit.sh [--build-dir <dir>] [--out <dir>] [--archive]
#   --build-dir   the build tree the runtime comes from (default build/linux-clang-module,
#                 built with --module on --wizard on)
#   --archive     also a .tar.xz of the folder beside it, which keeps what a copy onto
#                 another file system may not (who may run a program)

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$root/build/linux-clang-module"
out="$root/build/kit-linux"
archive=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--build-dir) build="$(realpath -m "$2")"; shift ;;
		--out) out="$(realpath -m "$2")"; shift ;;
		--archive) archive=1 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

bin="$build/ps2n/bin"
deps="$root/deps/linux-x64"
llvm_version=22.1.3
llvm="$root/deps/llvm-$llvm_version-linux"
recomp="$root/build/tools-linux/bin/ps2n-recomp"
pcsx2="$root/third_party/pcsx2"
deps_script="$pcsx2/.github/workflows/scripts/linux/build-dependencies-qt.sh"
for file in "$bin/ps2n-host" "$bin/ps2n-wizard" "$bin/ps2n-remake" "$bin/ps2n-launcher" "$recomp"; do
	if [ ! -x "$file" ]; then
		echo "Build first: $file is missing (scripts/linux/build.sh --build-dir $build --module on --wizard on)." >&2
		exit 1
	fi
done
if [ ! -x "$llvm/bin/clang" ] || [ ! -x "$llvm/bin/ld.lld" ]; then
	echo "LLVM's own $llvm_version is not in $llvm (scripts/linux/build.sh says where it comes from)." >&2
	exit 1
fi
# How the tree's runtime was built: a release's has link-time optimization and a profile of play (docs/adr/0047).
runtime_lto="$(grep -cE '^PS2N_RUNTIME_LTO:BOOL=ON$' "$build/CMakeCache.txt" || true)"
runtime_pgo="$(sed -nE 's/^PS2N_RUNTIME_PGO:[A-Z]+=(.*)$/\1/p' "$build/CMakeCache.txt" | head -1)"
if [ "$runtime_pgo" = "generate" ]; then
	echo "$build has a runtime built to count what it runs (--runtime-pgo generate): that is for training runs, not for a folder." >&2
	exit 1
fi
# PS2Native's changes to PCSX2's code that the tree's runtime is built with (docs/adr/0050), by name, as
# cmake/ps2n-pcsx2-patches.cmake wrote them down: the licences say that PCSX2 is changed, and carry the changes.
pcsx2_changes=()
if [ -f "$build/ps2n-pcsx2/changes.txt" ]; then
	IFS=',' read -r -a pcsx2_changes_read < <(head -1 "$build/ps2n-pcsx2/changes.txt" | tr -d '\r')
	for change in ${pcsx2_changes_read[@]+"${pcsx2_changes_read[@]}"}; do
		change="${change# }"
		[ -n "$change" ] && pcsx2_changes+=("$change")
	done
fi
pcsx2_changes_text="$(printf '%s, ' ${pcsx2_changes[@]+"${pcsx2_changes[@]}"} | sed 's/, $//')"
pcsx2_patches_text="$(for p in "$root"/patches/pcsx2/*.patch; do if [ -f "$p" ]; then basename "$p" .patch; fi; done | sort | paste -sd, - | sed 's/,/, /g')"
for tool in patchelf ldd readelf strip dpkg-query curl sha256sum xz; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "Missing tool: $tool" >&2
		exit 1
	fi
done

rm -rf "$out/runtime" "$out/toolchain" "$out/licences" "$out/PS2Native" "$out/qt.conf" "$out/README.txt"
mkdir -p "$out/runtime" "$out/toolchain" "$out/licences"
runtime="$out/runtime"
toolchain="$out/toolchain"
licences="$out/licences"

# ---- the libraries ----------------------------------------------------------------------------------
# What every desktop Linux has, left to the system: pkg2appimage's excludelist
# (https://github.com/AppImageCommunity/pkg2appimage, file excludelist), which linuxdeploy follows.
system_libs=" ld-linux.so.2 ld-linux-x86-64.so.2 libanl.so.1 libBrokenLocale.so.1 libcidn.so.1 libc.so.6 libdl.so.2
	libm.so.6 libmvec.so.1 libnss_compat.so.2 libnss_dns.so.2 libnss_files.so.2 libnss_hesiod.so.2 libnss_nisplus.so.2
	libnss_nis.so.2 libpthread.so.0 libresolv.so.2 librt.so.1 libthread_db.so.1 libutil.so.1 libstdc++.so.6 libGL.so.1
	libEGL.so.1 libGLdispatch.so.0 libGLX.so.0 libOpenGL.so.0 libdrm.so.2 libglapi.so.0 libgbm.so.1 libxcb.so.1
	libX11.so.6 libX11-xcb.so.1 libwayland-client.so.0 libasound.so.2 libfontconfig.so.1 libfreetype.so.6
	libharfbuzz.so.0 libcom_err.so.2 libexpat.so.1 libgcc_s.so.1 libgpg-error.so.0 libICE.so.6 libSM.so.6
	libusb-1.0.so.0 libuuid.so.1 libz.so.1 libjack.so.0 libpipewire-0.3.so.0 libxcb-dri3.so.0 libxcb-dri2.so.0
	libfribidi.so.0 libgmp.so.10 "
system_libs="$(echo $system_libs)"
# The libraries of this system's taken into the folder, by name: where they are, for their licences.
declare -A from_system=()
# Where the loader finds each library for the file being walked (ldd's answer, theirs too).
declare -A found=()

# copy_needed <folder> <file>...: the libraries the files need, and theirs, copied into the folder under
# the names they are asked for by, but those left to the system, whose own needs are the system's.
copy_needed() {
	local to="$1" file name lib
	shift
	for file in "$@"; do
		if LD_LIBRARY_PATH="$deps/lib" ldd "$file" | grep -q 'not found'; then
			echo "$file needs a library this computer does not have:" >&2
			LD_LIBRARY_PATH="$deps/lib" ldd "$file" | grep 'not found' >&2
			exit 1
		fi
		found=()
		while read -r name lib; do
			found["$name"]="$lib"
		done < <(LD_LIBRARY_PATH="$deps/lib" ldd "$file" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }')
		walk_needed "$to" "$file"
	done
}
walk_needed() {
	local to="$1" name lib
	for name in $(readelf -d "$2" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p'); do
		case " $system_libs " in *" $name "*) continue ;; esac
		[ -e "$to/$name" ] && continue
		lib="${found[$name]:-}"
		if [ -z "$lib" ]; then
			echo "ldd does not say where $name, which $2 needs, is." >&2
			exit 1
		fi
		cp -L "$lib" "$to/$name"
		chmod 644 "$to/$name"
		case "$lib" in
			"$deps"/*) ;;
			*) from_system["$name"]="$(readlink -f "$lib")" ;;
		esac
		walk_needed "$to" "$lib"
	done
}

is_elf() {
	[ "$(head -c 4 "$1" | od -An -c | tr -d ' ')" = '177ELF' ]
}

# ---- the runtime ------------------------------------------------------------------------------------
programs=(ps2n-host ps2n-wizard ps2n-remake ps2n-launcher)
for program in "${programs[@]}"; do
	cp "$bin/$program" "$runtime/"
done
cp -r "$bin/resources" "$runtime/"
copy_needed "$runtime" "${programs[@]/#/$bin/}"
# The Vulkan renderer's shader compiler, which PCSX2 loads while it runs.
cp -L "$deps/lib/libshaderc_shared.so.1" "$runtime/"
copy_needed "$runtime" "$deps/lib/libshaderc_shared.so.1"

# Qt's plugins: its windows on X11 (on Wayland too, through XWayland), the compose key, SVG pictures and
# icons. The game's window is PS2Native's own, on X11 (host/window.cpp).
qt_plugins=(platforms/libqxcb.so platforminputcontexts/libcomposeplatforminputcontextplugin.so
	imageformats/libqsvg.so iconengines/libqsvgicon.so)
for plugin in "${qt_plugins[@]}"; do
	mkdir -p "$runtime/qt/plugins/$(dirname "$plugin")"
	cp "$deps/plugins/$plugin" "$runtime/qt/plugins/$plugin"
	copy_needed "$runtime" "$deps/plugins/$plugin"
done
printf '[Paths]\nPlugins = qt/plugins\n' > "$runtime/qt.conf"
# What of the runtime folder only the wizard needs, for a game's folder to leave out (wizard/package.cpp).
printf 'wizard-files.txt\nps2n-wizard\n' > "$runtime/wizard-files.txt"

# Where each finds the others: beside it; Qt's plugins three folders down. The launcher also in runtime/,
# for the folder's own at its root, PS2Native, which has a qt.conf of its own.
for file in "$runtime"/*; do
	if [ -f "$file" ] && is_elf "$file"; then
		patchelf --set-rpath '$ORIGIN' "$file"
	fi
done
for plugin in "${qt_plugins[@]}"; do
	patchelf --set-rpath '$ORIGIN/../../..' "$runtime/qt/plugins/$plugin"
done
patchelf --set-rpath '$ORIGIN:$ORIGIN/runtime' "$runtime/ps2n-launcher"
cp "$runtime/ps2n-launcher" "$out/PS2Native"
printf '[Paths]\nPlugins = runtime/qt/plugins\n' > "$out/qt.conf"

# ---- the toolchain ----------------------------------------------------------------------------------
mkdir -p "$toolchain/bin" "$toolchain/lib/clang" "$toolchain/include/ps2n" "$toolchain/include/runtime" "$toolchain/src"
# LLVM's release as it is but for its symbol tables, which nothing here reads (100 MB of the 470).
strip -o "$toolchain/bin/clang" "$(readlink -f "$llvm/bin/clang")"
strip -o "$toolchain/bin/ld.lld" "$(readlink -f "$llvm/bin/ld.lld")"
strip -o "$toolchain/bin/llvm-profdata" "$(readlink -f "$llvm/bin/llvm-profdata")"
chmod 755 "$toolchain/bin/clang" "$toolchain/bin/ld.lld" "$toolchain/bin/llvm-profdata"
copy_needed "$toolchain/lib" "$toolchain/bin/clang" "$toolchain/bin/ld.lld" "$toolchain/bin/llvm-profdata"
for file in "$toolchain"/lib/*.so*; do
	patchelf --set-rpath '$ORIGIN' "$file"
done
for program in clang ld.lld llvm-profdata; do
	patchelf --set-rpath '$ORIGIN/../lib' "$toolchain/bin/$program"
done
# Of the compiler's own files, its headers, and its profile runtime, which a
# module made to count is linked with (docs/adr/0045).
clang_major="${llvm_version%%.*}"
mkdir -p "$toolchain/lib/clang/$clang_major"
cp -r "$llvm/lib/clang/$clang_major/include" "$toolchain/lib/clang/$clang_major/"
cp "$llvm/lib/clang/$clang_major/lib/x86_64-unknown-linux-gnu/libclang_rt.profile.a" "$toolchain/lib/"
cp "$recomp" "$toolchain/bin/"
cp "$root"/runtime/include/ps2n/*.h "$toolchain/include/ps2n/"
for header in r3000a.h vu_step.h vu_steps.inc; do
	cp "$root/core/include/ps2n/$header" "$toolchain/include/ps2n/"
done
cp "$root/runtime/game_module.h" "$toolchain/include/runtime/"
cp "$root/runtime/module_entry.cpp" "$root/runtime/module_counts.cpp" "$toolchain/src/"

# ---- the licences -----------------------------------------------------------------------------------
cp "$root/LICENSE" "$licences/PS2Native-LICENSE.txt"
cp "$pcsx2/COPYING.GPLv3" "$licences/PCSX2-COPYING.GPLv3.txt"
cp "$pcsx2/bin/docs/ThirdPartyLicenses.html" "$licences/PCSX2-ThirdPartyLicenses.html"
# PS2Native's changes to PCSX2's code, where the runtime is built with any: the patches themselves, each of which
# says what it changes, why and when, and what the folder of them says.
if [ "${#pcsx2_changes[@]}" -gt 0 ]; then
	mkdir -p "$licences/PCSX2-changes"
	for change in "${pcsx2_changes[@]}"; do
		cp "$root/patches/pcsx2/$change.patch" "$licences/PCSX2-changes/"
	done
	cp "$root/patches/pcsx2/README.md" "$licences/PCSX2-changes/README.txt"
fi
cp "$deps/share/licenses/SDL3/LICENSE.txt" "$licences/SDL3-LICENSE.txt"
llvm_licence="$llvm/LICENSE.TXT"
if [ ! -f "$llvm_licence" ]; then
	curl -fsSL -o "$llvm_licence" "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-$llvm_version/llvm/LICENSE.TXT"
fi
cp "$llvm_licence" "$licences/LLVM-LICENSE.TXT"

# Qt: its release and its source, as PCSX2's script for its dependencies names them; the source for the
# licence texts in it, its SHA-256 checked.
qt="$(sed -n 's/^QT=//p' "$deps_script" | head -1)"
qtbase="qtbase-everywhere-src-$qt"
qtbase_url="https://download.qt.io/official_releases/qt/${qt%.*}/$qt/submodules/$qtbase.tar.xz"
qtsvg_url="https://download.qt.io/official_releases/qt/${qt%.*}/$qt/submodules/qtsvg-everywhere-src-$qt.tar.xz"
qtbase_sha256="$(grep -E "^[0-9a-f]{64}  qtbase-everywhere-src-\\\$QT\\.tar\\.xz" "$deps_script" | cut -d' ' -f1)"
if [ -z "$qtbase_sha256" ]; then
	echo "PCSX2's script names no SHA-256 of qtbase's source." >&2
	exit 1
fi
qt_licences="$root/deps/qt-$qt-licences-linux"
mkdir -p "$qt_licences"
if [ ! -f "$qt_licences/$qtbase.tar.xz" ]; then
	echo "Fetching $qtbase_url"
	curl -fsSL -o "$qt_licences/$qtbase.tar.xz.part" "$qtbase_url"
	mv "$qt_licences/$qtbase.tar.xz.part" "$qt_licences/$qtbase.tar.xz"
fi
if [ "$(sha256sum "$qt_licences/$qtbase.tar.xz" | cut -d' ' -f1)" != "$qtbase_sha256" ]; then
	echo "$qt_licences/$qtbase.tar.xz is not Qt's source (its SHA-256 is not $qtbase_sha256)." >&2
	exit 1
fi
if [ ! -d "$qt_licences/$qtbase" ]; then
	tar -xJf "$qt_licences/$qtbase.tar.xz" -C "$qt_licences" --wildcards '*LICEN[CS]E*' '*COPYING*'
fi

# Qt's files in the folder, and what Qt's build calls each: its SBOM, the module or plugin.
qt_files="libQt6Core.so.6|qtbase|Core;libQt6Gui.so.6|qtbase|Gui;libQt6Widgets.so.6|qtbase|Widgets"
qt_files+=";libQt6DBus.so.6|qtbase|DBus;libQt6XcbQpa.so.6|qtbase|XcbQpaPrivate;libQt6Svg.so.6|qtsvg|Svg"
qt_files+=";qt/plugins/platforms/libqxcb.so|qtbase|QXcbIntegrationPlugin"
qt_files+=";qt/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so|qtbase|QComposePlatformInputContextPlugin"
qt_files+=";qt/plugins/imageformats/libqsvg.so|qtsvg|QSvgPlugin;qt/plugins/iconengines/libqsvgicon.so|qtsvg|QSvgIconPlugin"
for file in "$runtime"/libQt6*.so* $(cd "$runtime" && find qt -name '*.so'); do
	relative="${file#"$runtime"/}"
	case ";$qt_files" in *";$relative|"*) ;; *)
		echo "Qt's $relative is in the runtime, but not in the licences' list of Qt's files (kit.sh, qt_files)." >&2
		exit 1 ;;
	esac
done
present=""
for entry in ${qt_files//;/ }; do
	[ -e "$runtime/${entry%%|*}" ] && present+="${present:+;}$entry"
done

# The notice: from Qt's own record of what it built (its SBOMs, tag and value), each of Qt's files with the
# code of others built into it, that code's licence and copyright, and its own licence file from Qt's
# source where it has one; then every licence named, as Qt's source has it. As kit.ps1 makes it.
awk -v files="$present" -v src="$qt_licences/$qtbase" -v version="$qt" -v qtbase_url="$qtbase_url" \
	-v qtsvg_url="$qtsvg_url" '
function store(key, value) {
	if (key == "PackageName") { n++; name[n] = value; doc[n] = document; current = n; return }
	if (key == "LicenseID") { current = 0; return }
	if (current && !((current, key) in field)) field[current, key] = value
}
function line(text) { out[++lines] = text }
function trim(s) { sub(/^[ \t\r\n]+/, "", s); sub(/[ \t\r\n]+$/, "", s); return s }
function normal(path,   parts, count, i, kept, k, result) {
	count = split(path, parts, "/")
	k = 0
	for (i = 1; i <= count; i++) {
		if (parts[i] == "..") { if (k > 0) k-- }
		else if (parts[i] != "" && parts[i] != ".") kept[++k] = parts[i]
	}
	result = ""
	for (i = 1; i <= k; i++) result = result (i > 1 ? "/" : "") kept[i]
	return result
}
# A file of Qt source, its lines with the trailing blanks gone, each after `indent`; 0 when it is not there.
function source_file(path, indent,   file, text, count, i, l) {
	file = src "/" normal(path)
	count = 0
	while ((getline l < file) > 0) { sub(/[ \t\r]+$/, "", l); text[++count] = l }
	close(file)
	while (count > 0 && text[count] == "") count--
	if (count == 0) return 0
	for (i = 1; i <= count; i++) line(text[i] == "" ? "" : indent text[i])
	return 1
}
function find(d, target,   k) {
	for (k = 1; k <= n; k++) if (doc[k] == d && name[k] == target) return k
	return 0
}
function need(ids, own,   count, parts, i, id) {
	gsub(/[()]/, " ", ids)
	count = split(ids, parts, /[ \t]+/)
	for (i = 1; i <= count; i++) {
		id = parts[i]
		if (id == "" || id == "OR" || id == "AND" || id == "WITH" || id == "NOASSERTION") continue
		if (!(id in generic)) { order[++ids_named] = id; generic[id] = 0 }
		if (!own) generic[id] = 1
	}
}
FNR == 1 { document = FILENAME; sub(/.*\//, "", document); sub(/-[0-9.]+\.spdx$/, "", document); intext = 0 }
{
	if (intext) {
		value = value "\n" $0
		if (index($0, "</text>")) { intext = 0; store(key, substr(value, 1, index(value, "</text>") - 1)) }
		next
	}
	if (!match($0, /^[A-Za-z]+:/)) next
	key = substr($0, 1, RLENGTH - 1)
	value = substr($0, RLENGTH + 1)
	sub(/^[ \t]/, "", value)
	if (substr(value, 1, 6) == "<text>") {
		value = substr(value, 7)
		if (index(value, "</text>")) store(key, substr(value, 1, index(value, "</text>") - 1))
		else intext = 1
		next
	}
	store(key, value)
}
END {
	rule = "------------------------------------------------------------------------------"
	order[++ids_named] = "LGPL-3.0-only"; generic["LGPL-3.0-only"] = 1
	order[++ids_named] = "GPL-3.0-only"; generic["GPL-3.0-only"] = 1
	count = split(files, entries, ";")
	list = ""
	for (i = 1; i <= count; i++) { split(entries[i], e, "|"); list = list (i > 1 ? ", " : "") e[1] }
	line("Qt, in this folder and in every game'"'"'s folder made with it")
	line("==========================================================")
	line("")
	line("Qt " version " (https://www.qt.io), in runtime/: " list ".")
	line("")
	line("Qt is The Qt Company'"'"'s and its contributors'"'"', offered under the GNU Lesser General Public License, version 3")
	line("(LGPL-3.0-only), the GNU General Public License, version 2 or 3, or a commercial licence. PS2Native carries it")
	line("under the LGPL, version 3, whose text is at the end of this file with the GPL, version 3, which it builds on.")
	line("Each of these libraries is a file of its own, which another build of the same Qt can replace.")
	line("")
	line("It is Qt " version " as its source has it, unchanged: " qtbase_url " and")
	line(qtsvg_url ", built for PCSX2'"'"'s dependencies by PCSX2'"'"'s script, which says how")
	line("(.github/workflows/scripts/linux/build-dependencies-qt.sh in PCSX2'"'"'s source; see README.txt beside this).")
	line("")
	line("Below, from Qt'"'"'s own record of what it built (its SBOMs), each of those files with the code of others")
	line("built into it, and that code'"'"'s licence and copyright: its own licence file from Qt'"'"'s source where it has one,")
	line("and otherwise the licence it names, whose text is at the end of this file.")
	for (i = 1; i <= count; i++) {
		split(entries[i], e, "|")
		module = find(e[2], e[3])
		if (!module) { print "kit.sh: Qt'"'"'s SBOM " e[2] " has no " e[3] > "/dev/stderr"; failed = 1; exit 1 }
		line("")
		line(rule)
		line(e[1] ": Qt'"'"'s " e[3] " " field[module, "PackageVersion"])
		line(field[module, "PackageCopyrightText"])
		line("Licence: LGPL-3.0-only (at the end of this file)")
		first = 1
		for (k = 1; k <= n; k++) {
			if (doc[k] != e[2] || index(name[k], e[3] "_Attribution_") != 1) continue
			if (first) { line(""); line("Built into it:"); first = 0 }
			comment = field[k, "PackageComment"]
			lines_in = split(comment, cl, "\n")
			part = name[k]; where = ""; own_count = 0
			for (j = 1; j <= lines_in; j++) {
				c = cl[j]
				if (match(c, /^[ \t]*Name: /)) part = trim(substr(c, RLENGTH + 1))
				if (match(c, /^[ \t]*License file: /)) own[++own_count] = trim(substr(c, RLENGTH + 1))
				if (match(c, /^[ \t]*\/src_dir\/qtbase\/.+\/qt_attribution\.json[ \t]*$/)) {
					where = trim(c); sub(/^\/src_dir\/qtbase\//, "", where); sub(/\/qt_attribution\.json$/, "", where)
				}
			}
			v = field[k, "PackageVersion"]
			line("")
			line("* " part (v != "" && v != "unknown" ? " " v : ""))
			from = field[k, "PackageDownloadLocation"]
			if (from != "" && from != "NOASSERTION") line("  From: " from)
			line("  Licence: " field[k, "PackageLicenseConcluded"])
			copyright = field[k, "PackageCopyrightText"]
			if (copyright != "" && copyright != "NOASSERTION") {
				cc = split(copyright, cr, "\n")
				for (j = 1; j <= cc; j++) line("  " trim(cr[j]))
			}
			has_own = own_count > 0 && where != ""
			for (j = 1; has_own && j <= own_count; j++) {
				line("")
				line("  Its licence (" where "/" own[j] " in Qt'"'"'s source):")
				line("")
				if (!source_file(where "/" own[j], "    ")) {
					print "kit.sh: " where "/" own[j] " is not in Qt'"'"'s source" > "/dev/stderr"; failed = 1; exit 1
				}
			}
			need(field[k, "PackageLicenseConcluded"], has_own)
		}
	}
	line("")
	line(rule)
	line("The licences named above, as Qt'"'"'s source has them (its LICENSES folder)")
	for (i = 1; i <= ids_named; i++) {
		id = order[i]
		if (seen[id]++) continue
		file = "LICENSES/" id ".txt"
		probe = src "/" file
		if ((getline l < probe) <= 0) {
			close(probe)
			if (generic[id]) { print "kit.sh: Qt'"'"'s source has no text of the licence " id > "/dev/stderr"; failed = 1; exit 1 }
			continue
		}
		close(probe)
		line("")
		line(rule)
		line(id)
		line("")
		source_file(file, "")
	}
	for (i = 1; i <= lines; i++) print out[i]
}' "$deps/sbom/qtbase-$qt.spdx" "$deps/sbom/qtsvg-$qt.spdx" > "$licences/Qt-NOTICE.txt"

# The libraries of this system's in the folder: each package's own copyright file, as Ubuntu has it, and
# where the source of that version is.
mkdir -p "$licences/Ubuntu"
declare -A by_package=()
for name in "${!from_system[@]}"; do
	path="${from_system[$name]}"
	owner="$(dpkg -S "$path" 2>/dev/null || dpkg -S "${path#/usr}" 2>/dev/null || true)"
	package="${owner%%:*}"
	if [ -z "$package" ]; then
		echo "No package of this system's has $path, which the folder takes." >&2
		exit 1
	fi
	by_package["$package"]+="${by_package[$package]:+, }$name"
done
ubuntu_list=""
for package in $(printf '%s\n' "${!by_package[@]}" | sort); do
	version="$(dpkg-query -W -f '${Version}' "$package")"
	source="$(dpkg-query -W -f '${source:Package}' "$package")"
	source_version="$(dpkg-query -W -f '${source:Version}' "$package")"
	copyright="/usr/share/doc/$package/copyright"
	[ -f "$copyright" ] || copyright="/usr/share/doc/$source/copyright"
	if [ ! -f "$copyright" ]; then
		echo "Ubuntu's package $package has no copyright file in /usr/share/doc." >&2
		exit 1
	fi
	cp -L "$copyright" "$licences/Ubuntu/$package.txt"
	ubuntu_list+="  ${by_package[$package]}: $package $version, Ubuntu/$package.txt;"$'\n'
	ubuntu_list+="    source: https://launchpad.net/ubuntu/+source/$source/$source_version"$'\n'
done

# Which is which, and where the source of each is.
version="$(head -1 "$root/version.txt" | tr -d '\r ')"
repository="$(git -C "$root" remote get-url origin 2>/dev/null | sed 's/\.git$//')"
commit="$(git -C "$root" rev-parse HEAD 2>/dev/null)"
changed=""
if [ -n "$(git -C "$root" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
	echo "Warning: the tree has changes not committed; licences/README.txt says so, and the folder is not for others."
	changed=", with changes not committed when this folder was made"
fi
pcsx2_tag="$(git -C "$pcsx2" describe --tags --exact-match 2>/dev/null || true)"
pcsx2_commit="$(git -C "$pcsx2" rev-parse HEAD 2>/dev/null)"
# Where the runtime's copy of PCSX2's core is not PCSX2's as released, the licences say so (the GPL's notice of a
# modified version), and where the changes are.
pcsx2_changed=""
if [ "${#pcsx2_changes[@]}" -gt 0 ]; then
	if [ "${#pcsx2_changes[@]}" -eq 1 ]; then pcsx2_count="change"; else pcsx2_count="${#pcsx2_changes[@]} changes"; fi
	pcsx2_changed="
PS2Native changed it: the runtime's copy of PCSX2's core is built from that source with PS2Native's
$pcsx2_count to it, in PCSX2-changes/: $pcsx2_changes_text.
Each change says at its top what it changes, why, and when it was made. They are patches/pcsx2 in PS2Native's
source above."
fi
system="$(. /etc/os-release && echo "$PRETTY_NAME")"
cat > "$licences/README.txt" <<EOF
The licences of what this folder holds
======================================

PS2Native $version: PS2Native-LICENSE.txt (GNU General Public License, version 3). Its source: $repository,
commit $commit$changed.

PCSX2, whose core the runtime is built on: PCSX2-COPYING.GPLv3.txt (GNU General Public License, version 3).
Its source: https://github.com/PCSX2/pcsx2, $pcsx2_tag (commit $pcsx2_commit).$pcsx2_changed

The libraries in runtime/ that come from PCSX2's dependencies, and what PCSX2 builds into its core:
PCSX2-ThirdPartyLicenses.html, PCSX2's own list of them with their licences. They are built by PCSX2's
script .github/workflows/scripts/linux/build-dependencies-qt.sh, in PCSX2's source above, from the sources
it names. SDL 3, which that list leaves out: SDL3-LICENSE.txt.

Qt $qt (runtime/libQt6*.so.6 and runtime/qt): Qt-NOTICE.txt, made from Qt's own record of what each file
holds, with every licence text it names, Qt's own LGPL, version 3, among them. Its source: Qt's release, as
Qt-NOTICE.txt says.

LLVM's clang, ld.lld and llvm-profdata, the compiler's headers and its profile runtime (toolchain/):
LLVM-LICENSE.TXT (Apache License 2.0 with LLVM exceptions). Their source: https://github.com/llvm/llvm-project,
llvmorg-$llvm_version.

The other libraries, in runtime/ and toolchain/lib, are $system's, as its packages have them; each
package's copyright and licence is in Ubuntu/, and Ubuntu keeps the source of that very version:
$ubuntu_list
EOF

cp "$root/scripts/linux/kit/README.txt" "$out/README.txt"

echo "The folder is ready in $out:"
du -sh "$runtime" "$toolchain" "$out" | sed "s|$root/||"
echo "Taken from $system: $(printf '%s\n' "${!from_system[@]}" | sort | tr '\n' ' ')"
echo "Its runtime is built $([ "$runtime_lto" = 1 ] && echo with || echo without) link-time optimization and $([ "$runtime_pgo" = "use" ] && echo with || echo without) a profile of play."
if [ "${#pcsx2_changes[@]}" -gt 0 ]; then
	echo "PCSX2's core in it has PS2Native's changes: $pcsx2_changes_text."
else
	echo "PCSX2's core in it is as PCSX2 has it, with none of PS2Native's changes."
fi
if [ "$pcsx2_changes_text" != "$pcsx2_patches_text" ]; then
	echo "Warning: the tree's runtime is not built with the changes of patches/pcsx2 as they are now ($pcsx2_patches_text): a release's is (docs/adr/0050; build.sh --build-dir $build --pcsx2-patches on)."
fi
if [ "$runtime_lto" != 1 ] || [ "$runtime_pgo" != "use" ]; then
	echo "Warning: a release's runtime is built with both (docs/adr/0047: scripts/linux/runtime-profile.sh, then build.sh --runtime-lto on --runtime-pgo use). Games play slower with this one."
fi

if [ "$archive" = 1 ]; then
	jobs="$(( ($(nproc) + 1) / 2 ))"
	tar -C "$(dirname "$out")" --exclude="$(basename "$out")/games" --exclude="$(basename "$out")/patches" \
		-cf - "$(basename "$out")" | xz -T "$jobs" -6 > "$out.tar.xz"
	echo "Archived: $out.tar.xz ($(du -h "$out.tar.xz" | cut -f1))"
fi
