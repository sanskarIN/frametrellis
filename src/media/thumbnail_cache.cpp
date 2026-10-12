// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/thumbnail_cache.h"

#include "core/logging.h"
#include "media/media_probe.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
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
    if (!info.hasVideo || !QFileInfo::exists(filePath)) {
        return {};
    }
    const QString cached = cacheFile(filePath, maxSize);
    QImage image;
    if (image.load(cached)) {
        return image;
    }
    Result<QImage> rendered = renderThumbnail(filePath, info, maxSize);
    if (!rendered) {
        qCInfo(lcMedia) << "No thumbnail for" << filePath << rendered.error().toString();
        return {};
    }
    QSaveFile file(cached);
    if (file.open(QIODevice::WriteOnly) && rendered->save(&file, "PNG")) {
        file.commit();
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
