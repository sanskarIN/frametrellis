// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/subtitle_producer.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTextDocument>
#include <QTextOption>
#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace ft::engine {
namespace {

QImage paintCaption(const QString& text, QSize canvas, const SubtitleTrack& style)
{
    // Bound raster work for very large projects. Layout is still canonical for every
    // preview/export size, and no image is allocated for each inactive cue in the project.
    if (canvas.width() > 4096 || canvas.height() > 4096) {
        canvas.scale(QSize(4096, 4096), Qt::KeepAspectRatio);
    }
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.fill(Qt::transparent);
    QTextDocument document;
    document.setDocumentMargin(0);
    QTextOption option;
    option.setAlignment(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setPlainText(text); // SRT text is never interpreted as HTML or resource URLs.
    const qreal maxWidth = std::max(1.0, canvas.width() * 0.84);
    const qreal maxHeight = std::max(1.0, canvas.height() * 0.72);
    document.setTextWidth(maxWidth);
    QFont font(QStringLiteral("Sans Serif"));
    font.setBold(true);
    const int preferred = std::max(1, canvas.height() * style.fontPercent / 100);
    const auto heightAt = [&](int pixels) {
        font.setPixelSize(pixels);
        document.setDefaultFont(font);
        return document.size().height();
    };
    int low = 1;
    int high = preferred;
    int fitted = 1;
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        if (heightAt(middle) <= maxHeight) {
            fitted = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    (void)heightAt(fitted);
    // Tighten short captions without changing wrapping for longer ones.
    document.setTextWidth(std::min(maxWidth, std::ceil(document.idealWidth()) + 1.0));
    const QSizeF size = document.size();
    const qreal fit = std::min(1.0, maxHeight / std::max(1.0, size.height()));
    const qreal padding = std::max(1.0, fitted * 0.22 * fit);
    const qreal left = (canvas.width() - size.width() * fit) / 2;
    const qreal top = canvas.height() * 0.92 - size.height() * fit - padding;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    if (style.background) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 185));
        painter.drawRoundedRect(QRectF(left - padding, top - padding,
                                       size.width() * fit + 2 * padding,
                                       size.height() * fit + 2 * padding),
                                padding / 2, padding / 2);
    }
    painter.translate(left, top);
    painter.scale(fit, fit);
    QAbstractTextDocumentLayout::PaintContext context;
    const qreal shadow = std::max(1.0, fitted / 24.0);
    painter.translate(shadow, shadow);
    context.palette.setColor(QPalette::Text, QColor(0, 0, 0, 230));
    document.documentLayout()->draw(&painter, context);
    painter.translate(-shadow, -shadow);
    context.palette.setColor(QPalette::Text, Qt::white);
    document.documentLayout()->draw(&painter, context);
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

struct SubtitleSource
{
    SubtitleTrack track;
    QSize canvas;
    FrameRate rate;
    std::mutex mutex;
    Id cachedId;
    QImage canonical;
    QImage scaled;
    QSize scaledSize;

    QImage imageAt(FrameCount frame, QSize size)
    {
        std::lock_guard lock(mutex);
        const SubtitleCue* cue = subtitleAtFrame(track, frame, rate);
        const Id id = cue ? cue->id : Id{};
        if (id != cachedId) {
            canonical = cue ? paintCaption(cue->text, canvas, track) : QImage();
            cachedId = id;
            scaled = {};
        }
        if (size != scaledSize || scaled.isNull()) {
            scaled = QImage(size, QImage::Format_RGBA8888);
            scaled.fill(Qt::transparent);
            if (!canonical.isNull()) {
                const QSize fitted = canonical.size().scaled(size, Qt::KeepAspectRatio);
                QPainter painter(&scaled);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(QRect(QPoint((size.width() - fitted.width()) / 2,
                                               (size.height() - fitted.height()) / 2),
                                        fitted),
                                  canonical);
            }
            scaledSize = size;
        }
        return scaled;
    }
};

using SourcePtr = std::shared_ptr<SubtitleSource>;
constexpr char kSourceKey[] = "_ft.subtitles.source";
void deleteSource(void* pointer)
{
    delete static_cast<SourcePtr*>(pointer);
}

int subtitleGetImage(mlt_frame frame, uint8_t** buffer, mlt_image_format* format, int* width,
                     int* height, int /*writable*/)
{
    auto* source = static_cast<SourcePtr*>(
        mlt_properties_get_data(MLT_FRAME_PROPERTIES(frame), kSourceKey, nullptr));
    if (!source || !*source || *width <= 0 || *height <= 0) {
        return 1;
    }
    const QImage image = (*source)->imageAt(mlt_frame_get_position(frame), QSize(*width, *height));
    if (image.isNull()) {
        return 1;
    }
    const int size = *width * *height * 4;
    auto* data = static_cast<uint8_t*>(mlt_pool_alloc(size));
    if (!data) {
        return 1;
    }
    for (int y = 0; y < *height; ++y) {
        std::memcpy(data + static_cast<size_t>(y) * *width * 4, image.constScanLine(y),
                    static_cast<size_t>(*width) * 4);
    }
    mlt_frame_set_image(frame, data, size, mlt_pool_release);
    *buffer = data;
    *format = mlt_image_rgba;
    mlt_properties properties = MLT_FRAME_PROPERTIES(frame);
    mlt_properties_set_int(properties, "width", *width);
    mlt_properties_set_int(properties, "height", *height);
    mlt_properties_set_int(properties, "format", mlt_image_rgba);
    return 0;
}

int subtitleGetFrame(mlt_producer producer, mlt_frame_ptr frame, int /*index*/)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (!*frame) {
        return 1;
    }
    auto* source = static_cast<SourcePtr*>(
        mlt_properties_get_data(MLT_PRODUCER_PROPERTIES(producer), kSourceKey, nullptr));
    mlt_properties properties = MLT_FRAME_PROPERTIES(*frame);
    if (source) {
        mlt_properties_set_data(properties, kSourceKey, new SourcePtr(*source), 0, deleteSource,
                                nullptr);
    }
    mlt_frame_set_position(*frame, mlt_producer_position(producer));
    mlt_properties_set_int(properties, "progressive", 1);
    mlt_properties_set_int(properties, "test_audio", 1);
    mlt_properties_set_double(properties, "aspect_ratio", 1.0);
    mlt_frame_push_get_image(*frame, subtitleGetImage);
    mlt_producer_prepare_next(producer);
    return 0;
}

} // namespace

std::unique_ptr<Mlt::Producer> createSubtitleProducer(Mlt::Profile& profile,
                                                      const SubtitleTrack& track, QSize canvas,
                                                      const FrameRate& rate, FrameCount length)
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        return nullptr;
    }
    auto source = std::make_shared<SubtitleSource>();
    source->track = track;
    source->canvas = canvas;
    source->rate = rate;
    mlt_producer producer = mlt_producer_new(profile.get_profile());
    if (!producer) {
        return nullptr;
    }
    producer->get_frame = subtitleGetFrame;
    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    mlt_properties_set(properties, "mlt_service", "ft.subtitles");
    mlt_properties_set(properties, "resource", "FrameTrellis subtitles");
    mlt_properties_set_int(properties, "length", static_cast<int>(length));
    mlt_properties_set_int(properties, "out", static_cast<int>(length - 1));
    mlt_properties_set_int(properties, "meta.media.width", profile.width());
    mlt_properties_set_int(properties, "meta.media.height", profile.height());
    mlt_properties_set_int(properties, "seekable", 1);
    mlt_properties_set_int(properties, "hide", 2);
    mlt_properties_set_data(properties, kSourceKey, new SourcePtr(source), 0, deleteSource,
                            nullptr);
    auto wrapper = std::make_unique<Mlt::Producer>(producer);
    mlt_producer_close(producer);
    Mlt::Filter convert(profile, "imageconvert");
    if (convert.is_valid()) {
        wrapper->attach(convert);
    }
    return wrapper;
}

} // namespace ft::engine
