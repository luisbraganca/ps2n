// SPDX-License-Identifier: GPL-3.0-or-later
//
// See patches.h. Fetched with PCSX2's own downloader (WinHTTP on Windows,
// libcurl on Linux), which PCSX2 already brings: nothing new to depend on.

#include "patches.h"

#include "common/HTTPDownloader.h"
#include "common/ZipHelpers.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QTimer>

#include <memory>
#include <optional>
#include <utility>

namespace ps2n::wizard::patches
{
	bool IsArchive(const QString& path, QString* error)
	{
		zip_error_t zip_error = {};
		const auto zip = zip_open_managed(QDir::toNativeSeparators(path).toUtf8().constData(), ZIP_RDONLY, &zip_error);
		if (!zip)
		{
			*error = QObject::tr("%1 is not a zip archive.").arg(QDir::toNativeSeparators(path));
			return false;
		}
		const zip_int64_t entries = zip_get_num_entries(zip.get(), 0);
		for (zip_int64_t i = 0; i < entries; i++)
		{
			const char* name = zip_get_name(zip.get(), static_cast<zip_uint64_t>(i), 0);
			if (name && QString::fromUtf8(name).endsWith(QStringLiteral(".pnach"), Qt::CaseInsensitive))
				return true;
		}
		*error = QObject::tr("%1 holds no patch files (.pnach): it is not PCSX2's archive of patches.").arg(QDir::toNativeSeparators(path));
		return false;
	}

	void Fetch(QObject* context, const QString& to, std::function<void(bool ok, const QString& error)> done)
	{
		std::shared_ptr<HTTPDownloader> downloader(HTTPDownloader::Create("PS2Native"));
		if (!downloader)
		{
			done(false, QObject::tr("This computer cannot fetch anything from the Internet here."));
			return;
		}
		downloader->SetTimeout(120.0f);
		QDir().mkpath(QFileInfo(to).path());
		auto result = std::make_shared<std::optional<std::pair<bool, QString>>>();
		downloader->CreateRequest(kUrl, [result, to](s32 status, const std::string&, HTTPDownloader::Request::Data data) {
			if (status != HTTPDownloader::HTTP_STATUS_OK)
			{
				const QString why = status == HTTPDownloader::HTTP_STATUS_TIMEOUT ? QObject::tr("it took too long") :
									status == HTTPDownloader::HTTP_STATUS_ERROR   ? QObject::tr("no connection") :
																					QObject::tr("the answer %1").arg(status);
				*result = std::make_pair(false, QObject::tr("PCSX2's archive of patches could not be fetched from %1: %2.")
													 .arg(QString::fromLatin1(kUrl), why));
				return;
			}
			QSaveFile file(to);
			if (!file.open(QIODevice::WriteOnly) ||
				file.write(reinterpret_cast<const char*>(data.data()), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size()) ||
				!file.commit())
			{
				*result = std::make_pair(false, QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(to)));
				return;
			}
			QString error;
			if (!IsArchive(to, &error))
			{
				QFile::remove(to);
				*result = std::make_pair(false, error);
				return;
			}
			*result = std::make_pair(true, QString());
		});

		// The downloader calls back from PollRequests, here on this thread.
		auto* timer = new QTimer(context);
		QObject::connect(timer, &QTimer::timeout, context, [downloader, result, timer, done = std::move(done)]() {
			downloader->PollRequests();
			if (!result->has_value())
				return;
			timer->stop();
			timer->deleteLater();
			done((*result)->first, (*result)->second);
		});
		timer->start(50);
	}

	bool Keep(const QString& from, const QString& to, QString* error)
	{
		if (!IsArchive(from, error))
			return false;
		if (QFileInfo(from).absoluteFilePath() == QFileInfo(to).absoluteFilePath())
			return true;
		QDir().mkpath(QFileInfo(to).path());
		const QString part = to + QStringLiteral(".part");
		QFile::remove(part);
		if (!QFile::copy(from, part))
		{
			*error = QObject::tr("Cannot copy %1 into %2.").arg(QDir::toNativeSeparators(from), QDir::toNativeSeparators(QFileInfo(to).path()));
			return false;
		}
		QFile::remove(to);
		if (!QFile::rename(part, to))
		{
			QFile::remove(part);
			*error = QObject::tr("Cannot write %1.").arg(QDir::toNativeSeparators(to));
			return false;
		}
		return true;
	}
} // namespace ps2n::wizard::patches
