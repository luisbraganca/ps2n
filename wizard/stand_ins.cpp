// SPDX-License-Identifier: GPL-3.0-or-later
//
// What PCSX2's settings pages call of the rest of its Qt frontend (its main
// window, the thread that runs the machine, QtHost), for the wizard, where no
// machine runs (docs/adr/0032). The pages are compiled from PCSX2 unchanged;
// these are what they find in its place. A page tells the machine that its
// settings changed (applySettings, reloadGameSettings, reloadPatches): there is
// none to tell. What the wizard keeps of a game's settings is the file the
// page wrote (SaveGameSettings).

#include "MainWindow.h"

#include "QtHost.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "pcsx2/Config.h"
#include "pcsx2/INISettingsInterface.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QThread>

namespace
{
	MainWindow s_main_window;

	// The pages call the machine's thread through this pointer and touch
	// nothing of it; it is never made (it is a QThread of PCSX2's, whose code
	// the wizard does not have), so the pointer points at storage of its size.
	alignas(EmuThread) unsigned char s_emu_thread[sizeof(EmuThread)];
} // namespace

MainWindow* g_main_window = &s_main_window;
EmuThread* g_emu_thread = reinterpret_cast<EmuThread*>(s_emu_thread);

// As PCSX2's main window has it (pcsx2-qt/MainWindow.cpp).
const char* MainWindow::DISC_IMAGE_FILTER = QT_TRANSLATE_NOOP("MainWindow",
	"All File Types (*.bin *.iso *.cue *.mdf *.chd *.cso *.zso *.gz *.dump);;"
	"Single-Track Raw Images (*.bin *.iso);;"
	"Cue Sheets (*.cue);;"
	"Media Descriptor File (*.mdf);;"
	"MAME CHD Images (*.chd);;"
	"CSO Images (*.cso);;"
	"ZSO Images (*.zso);;"
	"GZ Images (*.gz);;"
	"Block Dumps (*.dump)");

void MainWindow::rescanFile(const std::string&)
{
}

void EmuThread::applySettings()
{
}

void EmuThread::reloadGameSettings()
{
}

void EmuThread::reloadPatches()
{
}

void EmuThread::updateEmuFolders()
{
}

namespace QtHost
{
	// Every setting of a game is shown (docs/adr/0032), the ones PCSX2 keeps for
	// those who ask for its advanced settings among them.
	bool ShouldShowAdvancedSettings()
	{
		return true;
	}

	void RunOnUIThread(const std::function<void()>& func, bool block)
	{
		if (QThread::currentThread() == qApp->thread())
		{
			func();
			return;
		}
		QMetaObject::invokeMethod(qApp, func, block ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
	}

	std::vector<std::pair<QString, QString>> GetAvailableLanguageList()
	{
		return {};
	}

	QIcon GetAppIcon()
	{
		return QIcon();
	}

	QString GetResourcesBasePath()
	{
		return QString::fromStdString(EmuFolders::Resources);
	}

	// As PCSX2 saves a game's settings (pcsx2-qt/QtHost.cpp): a file with no
	// setting in it is deleted, the others saved without their empty sections.
	bool SaveGameSettings(SettingsInterface* sif, bool delete_if_empty)
	{
		INISettingsInterface* ini = static_cast<INISettingsInterface*>(sif);
		Error error;
		if (delete_if_empty && ini->IsEmpty())
		{
			if (FileSystem::FileExists(ini->GetFileName().c_str()) && !FileSystem::DeleteFilePath(ini->GetFileName().c_str(), &error))
			{
				Console.ErrorFmt("ps2n-wizard: cannot delete {}: {}", ini->GetFileName(), error.GetDescription());
				return false;
			}
			return true;
		}
		ini->RemoveEmptySections();
		if (!ini->Save(&error))
		{
			Console.ErrorFmt("ps2n-wizard: cannot save {}: {}", ini->GetFileName(), error.GetDescription());
			return false;
		}
		return true;
	}

	bool IsVMValid()
	{
		return false;
	}
} // namespace QtHost
