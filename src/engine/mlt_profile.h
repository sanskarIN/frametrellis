// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"

#include <QSize>

#include <memory>

namespace Mlt {
class Profile;
}

namespace ft::engine {

/// Creates an explicit, progressive, square-pixel MLT profile.
std::unique_ptr<Mlt::Profile> createProfile(int width, int height, const FrameRate& rate);

/// Scales a frame size down to fit within maxSize (keeping even dimensions and aspect ratio).
QSize fitPreviewSize(QSize frameSize, QSize maxSize);

} // namespace ft::engine
