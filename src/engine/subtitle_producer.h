// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/subtitles.h"

#include <QSize>

#include <memory>

namespace Mlt {
class Producer;
class Profile;
} // namespace Mlt

namespace ft::engine {

/// A lazy transparent video layer with one cached caption image. Text layout uses the
/// project canvas even for smaller previews, so wrapping agrees with full-size exports.
/// Requires a QGuiApplication for font rendering, but creates no windows or GPU resources.
std::unique_ptr<Mlt::Producer> createSubtitleProducer(Mlt::Profile& profile,
                                                      const SubtitleTrack& track, QSize canvas,
                                                      const FrameRate& rate, FrameCount length);

} // namespace ft::engine
