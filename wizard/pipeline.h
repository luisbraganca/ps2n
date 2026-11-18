// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the wizard's recompile step does (docs/phase4-proposal.md, step P4.5),
// as scripts/windows/kit/make-game.ps1 does it: what the game's folder's
// sessions had to interpret put into its captures; the game's executables
// taken from its disc image (by the runtime, ps2n-host -disc-extract, through
// PCSX2's own readers of every format PCSX2 plays; step P4.8); them, the
// BIOS's code and the captured micro code
// and IOP code translated to C++ (ps2n-recomp); the files compiled with the
// toolchain's clang-cl, as many at once as the computer has processors, and
// linked with its lld-link into the game's four modules. Each program is run
// as a process of its own, one after another but for the compiles, so that
// the window stays awake, and what each says when it fails is passed on.
//
// The same recompile is a game's own, when its sessions met new code (step
// P4.6, docs/adr/0034; remake_main.cpp): then it keeps what the last compile
// left and compiles only what changed since, and does nothing at all when the
// captures are those the modules were compiled from (work/compiled-from.txt,
// which every compile that ends well writes).

#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <deque>
#include <functional>
#include <vector>

namespace ps2n::wizard
{
	struct PipelineInput
	{
		QString toolchain; // the toolchain folder: bin, include, lib, src
		QString runtime; // the folder of the runtime that reads the disc (ps2n-host.exe): the kit's, or the game's folder
		QString image; // the disc image, in any format PCSX2 plays
		QString bios; // the BIOS image
		QStringList executables; // on the disc: the one it boots first, then those it loads itself
		QString folder; // the game's folder: its work in work, its captures in captures
		QString modules; // where the four modules are linked: the game's folder when empty
		bool incremental = false; // the game's own recompile (above)
	};

	class Pipeline : public QObject
	{
		Q_OBJECT

	public:
		explicit Pipeline(QObject* parent = nullptr);

		void start(const PipelineInput& input);
		void cancel();
		bool running() const { return m_running; }
		// After an incremental run that finished well: whether it found the
		// captures to be those the modules were compiled from, and did nothing.
		bool nothingNew() const { return m_nothing_new; }

	Q_SIGNALS:
		// The captures hold what the modules were not compiled from: it goes on
		// to compile.
		void newCode();
		// What it is doing now, in a few words.
		void stage(const QString& what);
		// How many of the files are compiled.
		void progress(int done, int total);
		// A line for the log: what was run, what a program said.
		void line(const QString& text);
		// Done: ok, or why not.
		void finished(bool ok, const QString& error);

	private:
		struct Job
		{
			QString program;
			QStringList arguments;
			QString describe; // what the log says it is
		};

		// The steps, each started when the one before has ended well.
		void mergeCaptures();
		void extract();
		void translate();
		void compileAll();
		void link();

		// Runs jobs one after another, then `next`.
		void runInTurn(std::deque<Job> jobs, std::function<void()> next);
		void runNextInTurn();
		// Runs the compiles, so many at once.
		void startCompiles();

		void fail(const QString& error);
		QString tool(const char* name) const;
		// What the captures are, with the executables they are for: what
		// work/compiled-from.txt holds once they are compiled.
		QString fingerprint() const;
		// What the toolchain is: work/objects/toolchain.txt holds it for the
		// objects there, which another toolchain's would not match.
		QString toolchainStamp() const;

		PipelineInput m_in;
		bool m_running = false;
		bool m_cancelled = false;
		bool m_nothing_new = false;
		QString m_fingerprint;

		std::deque<Job> m_turn;
		std::function<void()> m_after_turn;
		QProcess* m_process = nullptr;

		struct Compile
		{
			QString kind; // game, bios, vu, iop
			QString source;
			QString object;
		};
		std::vector<Compile> m_compiles; // every object a module is linked from
		std::vector<size_t> m_todo; // of them, those to compile now
		size_t m_next_compile = 0; // in m_todo
		int m_compiles_done = 0;
		int m_compiles_running = 0;
		int m_jobs = 1;
		std::vector<QProcess*> m_compile_processes;

		QStringList m_program_ids; // the game module's list
		bool m_have_vu = false;
		bool m_have_iop = false;
	};
} // namespace ps2n::wizard
