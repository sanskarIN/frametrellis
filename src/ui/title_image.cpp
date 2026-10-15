// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/title_image.h"

#include <QFont>
#include <QPainter>

#include <algorithm>

namespace ft::ui {

Result<QImage> makeTitleImage(const QString& text, QSize resolution, int fontPixels,
                              const QColor& color)
{
    const QString caption = text.trimmed();
    if (caption.isEmpty() || caption.size() > 200) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Enter a title with 1 to 200 characters."));
    }
    if (resolution.width() < 16 || resolution.height() < 16 ||
        resolution.width() > 4096 || resolution.height() > 4096) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Title images support sequences up to 4096 pixels per side."));
    }
    if (fontPixels < 16 || fontPixels > 240 || !color.isValid() || color.alpha() == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose a valid font size and visible text colour."));
    }
    QImage image(resolution, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not allocate the title image."));
    }
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font;
    font.setFamily(QStringLiteral("Sans Serif"));
    font.setBold(true);
    font.setPixelSize(fontPixels);
    painter.setFont(font);

    // Leave a safe margin for TV overscan and vertical titles. Wrapping occurs within
    // the safe rectangle; the source image itself remains completely transparent.
    const QRect area(resolution.width() / 12, resolution.height() / 8,
                     resolution.width() * 5 / 6, resolution.height() * 3 / 4);
    const int alignment = Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap;
    const int shadow = std::clamp(fontPixels / 35, 1, 6);
    painter.setPen(QColor(0, 0, 0, 220));
    painter.drawText(area.translated(shadow, shadow), alignment, caption);
    painter.setPen(color);
    painter.drawText(area, alignment, caption);
    painter.end();
    return image;
}

} // namespace ft::ui
