// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>

namespace Mlt {
class Producer;
class Profile;
} // namespace Mlt

namespace ft::engine {

/// Image formats Qt can decode in this installation (lower-case suffixes).
QStringList stillImageFormats();

/// Decodes a still image with Qt, honouring EXIF orientation. Images much larger than maxSize
/// are decoded at a reduced size to save memory (pass an empty size for full resolution).
QImage loadStillImage(const QString& path, QSize maxSize = {}, QString* error = nullptr);

/// Creates an MLT producer that shows a still image decoded with Qt.
///
/// MLT's FFmpeg producer is unreliable for single images (repeated seeks and multiple instances
/// can return blank frames), so FrameTrellis serves stills itself: the picture is decoded once,
/// then aspect-fitted onto a transparent canvas of whatever size a consumer requests (cached), so
/// stills composite correctly over lower tracks. Returns nullptr when the image cannot be read.
std::unique_ptr<Mlt::Producer> createStillProducer(Mlt::Profile& profile, const QString& path,
                                                   QString* error = nullptr);

} // namespace ft::engine
