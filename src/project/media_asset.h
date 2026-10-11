// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/ids.h"
#include "core/rational.h"

#include <QDateTime>
#include <QString>

#include <optional>

namespace ft {

enum class MediaKind { Video, Audio, Image };

QString mediaKindToString(MediaKind kind);
std::optional<MediaKind> mediaKindFromString(const QString& text);

/// Technical facts discovered by probing a media file.
struct MediaInfo
{
    MediaKind kind = MediaKind::Video;
    std::int64_t durationUs = 0; ///< 0 for still images
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
    Rational frameRate;      ///< native video frame rate; invalid when unknown or not video
    int audioChannels = 0;
    int audioSampleRate = 0;
    QString container;       ///< demuxer name, e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    QString videoCodec;
    QString audioCodec;
    qint64 fileSize = 0;
    QDateTime lastModified;  ///< UTC modification time when probed

    friend bool operator==(const MediaInfo&, const MediaInfo&) = default;
};

/// Availability of an asset's file, determined at runtime and never stored in documents.
enum class MediaStatus { Unknown, Online, Missing, Changed };

/// A media file referenced by the project. Assets are never modified or moved on disk.
struct MediaAsset
{
    Id id;
    QString filePath; ///< absolute, cleaned path
    QString name;     ///< display name, defaults to the file name
    MediaInfo info;
    MediaStatus status = MediaStatus::Unknown;

    /// Number of whole project frames available, or std::nullopt for stills (unbounded).
    [[nodiscard]] std::optional<FrameCount> lengthInFrames(const FrameRate& rate) const;

    friend bool operator==(const MediaAsset&, const MediaAsset&) = default;
};

/// Normalises a path for identity comparison (absolute, cleaned, case-folded on Windows).
QString canonicalMediaKey(const QString& path);

} // namespace ft
