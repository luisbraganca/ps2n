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
#endif

	// What a module's source is compiled with, its object and itself last.
	inline QStringList CompileArguments(const QString& toolchain, const QString& object, const QString& source)
	{
		const QString include = QDir::toNativeSeparators(QDir(toolchain).filePath(QStringLiteral("include")));
#if defined(_WIN32)
		return {QStringLiteral("/nologo"), QStringLiteral("/c"), QStringLiteral("/X"), QStringLiteral("/clang:-ffreestanding"),
			QStringLiteral("/Zl"), QStringLiteral("/GS-"), QStringLiteral("/O2"), QStringLiteral("/Ob2"), QStringLiteral("-std:c++20"),
			QStringLiteral("-msse4.1"), QStringLiteral("/DNDEBUG"), QStringLiteral("/DPS2N_MODULE"), QStringLiteral("-I") + include,
			QStringLiteral("-Werror=global-constructors"), QStringLiteral("-Werror=exit-time-destructors"),
			QStringLiteral("/Fo") + QDir::toNativeSeparators(object), QDir::toNativeSeparators(source)};
#else
		return {QStringLiteral("-c"), QStringLiteral("-O3"), QStringLiteral("-DNDEBUG"), QStringLiteral("-std=gnu++20"),
			QStringLiteral("-fPIC"), QStringLiteral("-msse"), QStringLiteral("-msse2"), QStringLiteral("-msse4.1"),
			QStringLiteral("-mfxsr"), QStringLiteral("-fvisibility=hidden"), QStringLiteral("-fno-exceptions"),
			QStringLiteral("-nostdlibinc"), QStringLiteral("-ffreestanding"), QStringLiteral("-fno-stack-protector"),
			QStringLiteral("-DPS2N_MODULE"), QStringLiteral("-I") + include, QStringLiteral("-Werror=global-constructors"),
			QStringLiteral("-Werror=exit-time-destructors"), QStringLiteral("-o"), object, source};
#endif
	}

	// What a module is linked with: its objects listed in a response file.
	inline QStringList LinkArguments(const QString& toolchain, const QString& kind, const QString& module, const QString& objects,
		const QString& import_library)
	{
#if defined(_WIN32)
		return {QStringLiteral("/nologo"), QStringLiteral("/DLL"), QStringLiteral("/NOENTRY"), QStringLiteral("/NODEFAULTLIB"),
			QStringLiteral("/MACHINE:X64"), QStringLiteral("/OPT:REF"), QStringLiteral("/OPT:ICF"),
			QStringLiteral("/OUT:") + QDir::toNativeSeparators(module), QStringLiteral("/IMPLIB:") + QDir::toNativeSeparators(import_library),
			QStringLiteral("@") + QDir::toNativeSeparators(objects),
			QDir::toNativeSeparators(QDir(toolchain).filePath(QStringLiteral("lib/ps2n-host.lib")))};
#else
		(void)toolchain;
		(void)import_library;
		return {QStringLiteral("-shared"), QStringLiteral("-nostdlib"), QStringLiteral("-fuse-ld=lld"),
			QStringLiteral("-Wl,-soname,") + Module(kind), QStringLiteral("-o"), module, QStringLiteral("@") + objects};
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
