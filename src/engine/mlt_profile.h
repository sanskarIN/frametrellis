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

/// Scales down without exceeding maxSize or the native frame dimension limit.
/// Preserves aspect ratio approximately, rounding scaled axes to even dimensions where possible.
QSize fitPreviewSize(QSize frameSize, QSize maxSize);

} // namespace ft::engine
