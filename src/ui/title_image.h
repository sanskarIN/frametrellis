// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

namespace ft::ui {

/// Draw a title onto a transparent, sequence-sized PNG canvas. Returned images can be
/// imported as ordinary stills: no project-format changes or external font files are needed.
Result<QImage> makeTitleImage(const QString& text, QSize resolution, int fontPixels,
                              const QColor& color);

} // namespace ft::ui
