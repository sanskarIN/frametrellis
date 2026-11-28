// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/thumbnail_cache.h"

#include "core/logging.h"
#include "engine/mlt_profile.h"
#include "media/media_probe.h"
#include "project/project_settings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>

namespace ft::media {

ThumbnailCache::ThumbnailCache(QString directory)
    : m_directory(std::move(directory))
{
    QDir().mkpath(m_directory);
}

QString ThumbnailCache::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
           QStringLiteral("/thumbnails");
}

QString ThumbnailCache::cacheFile(const QString& filePath, QSize maxSize) const
{
    const QFileInfo info(filePath);
    const QString key = QStringLiteral("%1|%2|%3|%4x%5")
                            .arg(info.absoluteFilePath())
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(maxSize.width())
                            .arg(maxSize.height());
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(m_directory).filePath(QString::fromLatin1(hash) + QStringLiteral(".png"));
}

QImage ThumbnailCache::thumbnail(const QString& filePath, const MediaInfo& info,
                                 QSize maxSize) const
{
    const QFileInfo source(filePath);
    if (!info.hasVideo || info.width <= 0 || info.height <= 0 ||
        (info.kind == MediaKind::Video && info.durationUs < 0) || !source.isFile() ||
        maxSize.isEmpty() || maxSize.width() > kMaxFrameDimension ||
        maxSize.height() > kMaxFrameDimension) {
        return {};
    }
    const QSize expected = engine::fitPreviewSize(QSize(info.width, info.height), maxSize);
    const auto fits = [&expected](QSize size) {
        return !size.isEmpty() && size.width() <= expected.width() &&
               size.height() <= expected.height();
    };
    const QString cached = cacheFile(filePath, maxSize);
    const QFileInfo entry(cached);
    // A cache file is disposable, not trusted media. Inspect PNG dimensions
    // before decoding and never follow a cache symlink or open a special file.
    if (entry.isFile() && !entry.isSymLink()) {
        QImageReader reader(cached, QByteArrayLiteral("png"));
        if (fits(reader.size())) {
            const QImage image = reader.read();
            if (!image.isNull() && fits(image.size())) {
                return image;
            }
        }
    }
    Result<QImage> rendered = renderThumbnail(filePath, info, maxSize);
    if (!rendered) {
        qCInfo(lcMedia) << "No thumbnail for" << filePath << rendered.error().toString();
        return {};
    }
    if (rendered->isNull() || !fits(rendered->size())) {
        return {};
    }
    // Do not publish under an old key if the media changed during rendering,
    // or replace a nonregular cache entry.
    const QFileInfo destination(cached);
    if (cacheFile(filePath, maxSize) == cached && !destination.isSymLink() &&
        (!destination.exists() || destination.isFile())) {
        QSaveFile file(cached);
        if (file.open(QIODevice::WriteOnly) && rendered->save(&file, "PNG")) {
            file.commit();
        }
    }
    return *rendered;
}

void ThumbnailCache::prune(int maxAgeDays) const
{
    const QDateTime limit = QDateTime::currentDateTimeUtc().addDays(-maxAgeDays);
    QDirIterator it(m_directory, {QStringLiteral("*.png")}, QDir::Files);
    while (it.hasNext()) {
        const QFileInfo file(it.next());
        if (file.lastModified().toUTC() < limit) {
            QFile::remove(file.absoluteFilePath());
        }
    }
}

} // namespace ft::media
