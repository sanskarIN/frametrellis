// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/media_asset.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace ft {

QString mediaKindToString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Audio:
        return QStringLiteral("audio");
    case MediaKind::Image:
        return QStringLiteral("image");
    }
    return {};
}

std::optional<MediaKind> mediaKindFromString(const QString& text)
{
    if (text == QLatin1String("video")) {
        return MediaKind::Video;
    }
    if (text == QLatin1String("audio")) {
        return MediaKind::Audio;
    }
    if (text == QLatin1String("image")) {
        return MediaKind::Image;
    }
    return std::nullopt;
}

std::optional<FrameCount> MediaAsset::lengthInFrames(const FrameRate& rate) const
{
    if (info.kind == MediaKind::Image) {
        return std::nullopt;
    }
    const auto frames = rate.microsecondsToFrames(info.durationUs, Rounding::Down);
    return std::max<FrameCount>(frames.value_or(0), 1);
}

QString canonicalMediaKey(const QString& path)
{
    const QFileInfo fileInfo(path);
    QString key = fileInfo.canonicalFilePath();
    if (key.isEmpty()) {
        key = QDir::cleanPath(fileInfo.absoluteFilePath());
    }
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

} // namespace ft
