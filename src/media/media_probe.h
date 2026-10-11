// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/media_asset.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

namespace ft::media {

/// File name suffixes offered in the import dialog. Other files can still be imported; the probe
/// decides whether they are usable.
QStringList importFileSuffixes();

/// Inspects a media file with MLT/FFmpeg and verifies that it can actually be decoded.
/// Safe to call from worker threads after MltRuntime::initialize().
Result<MediaInfo> probeMedia(const QString& filePath);

/// Creates a MediaAsset (with a new id) for a successfully probed file.
Result<MediaAsset> createAsset(const QString& filePath);

/// Renders a representative picture of a video or image file, scaled to fit maxSize.
Result<QImage> renderThumbnail(const QString& filePath, const MediaInfo& info, QSize maxSize);

} // namespace ft::media
