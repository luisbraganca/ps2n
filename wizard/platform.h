// SPDX-License-Identifier: GPL-3.0-or-later
//
// What differs between the systems a game is made and played on, for the
// wizard, its recompile and a game's folder (step P4.7): the file names of
// programs and of the modules of compiled code, and the toolchain's compiler
// and linker, with what they are given. A module is compiled as the
// development build compiles it (tests/tests.cmake, ps2n_compile_freestanding,
// with the flags PCSX2's build gives every file), so that the two make the
// same code.
#pragma once

#include <QtCore/QDir>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace ps2n::wizard::platform
{
#if defined(_WIN32)
	// A program's file, by its name: "ps2n-host.exe".
	inline QString Program(const QString& name)
	{
		return name + QStringLiteral(".exe");
	}
	// A module of compiled code's file, by its kind: "ps2n-game.dll".
	inline QString Module(const QString& kind)
	{
		return QStringLiteral("ps2n-%1.dll").arg(kind);
	}
	inline const QString kObject = QStringLiteral(".obj");
	inline const QString kCompiler = QStringLiteral("clang-cl.exe");
	inline const QString kLinker = QStringLiteral("lld-link.exe");
	// The toolchain's files a module needs beyond its headers and the compiler
	// and linker: the runtime's import library, and the module's entry.
	inline const QStringList kToolchainFiles = {QStringLiteral("lib/ps2n-host.lib"), QStringLiteral("src/module_entry.cpp")};
	// LLVM's profile runtime, which a module made to count what of its code
	// runs is linked with (docs/adr/0045).
	inline const QString kProfileRuntime = QStringLiteral("lib/clang_rt.profile-x86_64.lib");
#else
	inline QString Program(const QString& name)
	{
		return name;
	}
	inline QString Module(const QString& kind)
	{
		return QStringLiteral("ps2n-%1.so").arg(kind);
	}
	inline const QString kObject = QStringLiteral(".o");
	// clang links too, through ld.lld beside it, as the development build does.
	inline const QString kCompiler = QStringLiteral("clang");
	inline const QString kLinker = QStringLiteral("ld.lld");
	// A module's undefined symbols are the runtime's executable's, bound when
	// it loads it: no import library.
	inline const QStringList kToolchainFiles = {QStringLiteral("src/module_entry.cpp")};
	inline const QString kProfileRuntime = QStringLiteral("lib/libclang_rt.profile.a");
#endif

	// What a module made to count what of its code runs is built from beyond its
	// own sources (runtime/module_counts.cpp), and the program its counts are
	// merged with (docs/adr/0045).
	inline const QString kCountsSource = QStringLiteral("src/module_counts.cpp");
	inline QString ProfileTool()
	{
		return QStringLiteral("bin/") + Program(QStringLiteral("llvm-profdata"));
	}

	// What a module's source is compiled with besides, to count what of its code
	// runs (clang's -fprofile-generate: counted where LLVM's optimizer has the
	// code, the targets of calls through a pointer and the sizes copied among
	// it), or with the counts of a run (-fprofile-use, a profile llvm-profdata
	// merged; docs/adr/0045). A function whose code the counts do not know, or no
	// longer match, is compiled as it would be without, without a word.
	inline QStringList ProfileArguments(bool count, const QString& profile)
	{
#if defined(_WIN32)
		const QString prefix = QStringLiteral("/clang:");
		const QString path = QDir::toNativeSeparators(profile);
#else
		const QString prefix;
		const QString& path = profile;
#endif
		if (count)
			return {prefix + QStringLiteral("-fprofile-generate")};
		if (profile.isEmpty())
			return {};
		return {prefix + QStringLiteral("-fprofile-use=") + path, prefix + QStringLiteral("-Wno-profile-instr-out-of-date"),
			prefix + QStringLiteral("-Wno-profile-instr-unprofiled"), prefix + QStringLiteral("-Wno-backend-plugin")};
	}

	// What a module's source is compiled with, its object and itself last;
	// `extra` (ProfileArguments) before them.
	inline QStringList CompileArguments(const QString& toolchain, const QString& object, const QString& source,
		const QStringList& extra = QStringList())
	{
		const QString include = QDir::toNativeSeparators(QDir(toolchain).filePath(QStringLiteral("include")));
#if defined(_WIN32)
		QStringList arguments = {QStringLiteral("/nologo"), QStringLiteral("/c"), QStringLiteral("/X"), QStringLiteral("/clang:-ffreestanding"),
			QStringLiteral("/Zl"), QStringLiteral("/GS-"), QStringLiteral("/O2"), QStringLiteral("/Ob2"), QStringLiteral("-std:c++20"),
			QStringLiteral("-msse4.1"), QStringLiteral("/DNDEBUG"), QStringLiteral("/DPS2N_MODULE"), QStringLiteral("-I") + include,
			QStringLiteral("-Werror=global-constructors"), QStringLiteral("-Werror=exit-time-destructors")};
		arguments += extra;
		arguments += {QStringLiteral("/Fo") + QDir::toNativeSeparators(object), QDir::toNativeSeparators(source)};
		return arguments;
#else
		// Each function in a section of its own, which the linker can place
		// (LinkArguments, `order`); clang-cl's /O2 does it on Windows (/Gy).
		QStringList arguments = {QStringLiteral("-c"), QStringLiteral("-O3"), QStringLiteral("-DNDEBUG"), QStringLiteral("-std=gnu++20"),
			QStringLiteral("-fPIC"), QStringLiteral("-msse"), QStringLiteral("-msse2"), QStringLiteral("-msse4.1"),
			QStringLiteral("-mfxsr"), QStringLiteral("-fvisibility=hidden"), QStringLiteral("-fno-exceptions"),
			QStringLiteral("-nostdlibinc"), QStringLiteral("-ffreestanding"), QStringLiteral("-fno-stack-protector"),
			QStringLiteral("-ffunction-sections"), QStringLiteral("-DPS2N_MODULE"), QStringLiteral("-I") + include,
			QStringLiteral("-Werror=global-constructors"), QStringLiteral("-Werror=exit-time-destructors")};
		arguments += extra;
		arguments += {QStringLiteral("-o"), object, source};
		return arguments;
#endif
	}

	// What a module is linked with: its objects listed in a response file;
	// with `order`, a file of function names one to a line, its functions
	// placed first, in that order (ADR 0044). A name the module does not
	// define is passed over without a word: the list is the game's sessions'
	// for all of its modules. `count`: a module made to count what of its code
	// runs, linked with LLVM's profile runtime (docs/adr/0045).
	inline QStringList LinkArguments(const QString& toolchain, const QString& kind, const QString& module, const QString& objects,
		const QString& import_library, const QString& order = QString(), bool count = false)
	{
		const QString profile_runtime = QDir::toNativeSeparators(QDir(toolchain).filePath(kProfileRuntime));
#if defined(_WIN32)
		QStringList arguments = {QStringLiteral("/nologo"), QStringLiteral("/DLL"), QStringLiteral("/NOENTRY"), QStringLiteral("/NODEFAULTLIB"),
			QStringLiteral("/MACHINE:X64"), QStringLiteral("/OPT:REF"), QStringLiteral("/OPT:ICF"),
			QStringLiteral("/OUT:") + QDir::toNativeSeparators(module), QStringLiteral("/IMPLIB:") + QDir::toNativeSeparators(import_library),
			QStringLiteral("@") + QDir::toNativeSeparators(objects),
			QDir::toNativeSeparators(QDir(toolchain).filePath(QStringLiteral("lib/ps2n-host.lib")))};
		if (count)
			arguments.push_back(profile_runtime);
		if (!order.isEmpty())
			arguments += {QStringLiteral("/ORDER:@") + QDir::toNativeSeparators(order), QStringLiteral("/ignore:4037")};
		return arguments;
#else
		(void)import_library;
		QStringList arguments = {QStringLiteral("-shared"), QStringLiteral("-nostdlib"), QStringLiteral("-fuse-ld=lld"),
			QStringLiteral("-Wl,-soname,") + Module(kind), QStringLiteral("-o"), module, QStringLiteral("@") + objects};
		if (count)
			arguments.push_back(profile_runtime);
		// (-Xlinker: -Wl, would cut a path with a comma in it.)
		if (!order.isEmpty())
			arguments += {QStringLiteral("-Xlinker"), QStringLiteral("--symbol-ordering-file"), QStringLiteral("-Xlinker"), order,
				QStringLiteral("-Xlinker"), QStringLiteral("--no-warn-symbol-ordering")};
		return arguments;
#endif
	}

	// The program a module is linked by: on Windows the linker, on Linux the
	// compiler, which runs ld.lld beside it.
	inline const QString& LinkProgram()
	{
#if defined(_WIN32)
		return kLinker;
#else
		return kCompiler;
#endif
	}
} // namespace ps2n::wizard::platform
