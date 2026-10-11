// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/still_producer.h"

#include <QImageReader>
#include <QPainter>
#include <mlt++/Mlt.h>

#include <climits>
#include <cstring>
#include <mutex>

namespace ft::engine {

namespace {

/// Shared between the producer and every frame it creates, so frames may outlive the producer.
struct StillSource
{
    QImage image;
    std::mutex mutex;
    QImage fitted;
    QSize fittedSize;

    QImage fittedTo(QSize size)
    {
        std::lock_guard lock(mutex);
        if (size != fittedSize || fitted.isNull()) {
            QImage canvas(size, QImage::Format_RGBA8888);
            canvas.fill(Qt::transparent);
            const QSize scaled = image.size().scaled(size, Qt::KeepAspectRatio);
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRect(QPoint((size.width() - scaled.width()) / 2,
                                           (size.height() - scaled.height()) / 2),
                                    scaled),
                              image);
            painter.end();
            fitted = canvas;
            fittedSize = size;
        }
        return fitted;
    }
};

using SourcePtr = std::shared_ptr<StillSource>;
constexpr char kSourceKey[] = "_ft.still.source";

void deleteSourcePtr(void* pointer)
{
    delete static_cast<SourcePtr*>(pointer);
}

int stillGetImage(mlt_frame frame, uint8_t** buffer, mlt_image_format* format, int* width,
                  int* height, int /*writable*/)
{
    mlt_properties properties = MLT_FRAME_PROPERTIES(frame);
    auto* source =
        static_cast<SourcePtr*>(mlt_properties_get_data(properties, kSourceKey, nullptr));
    if (!source || !*source) {
        return 1;
    }
    mlt_profile profile =
        mlt_service_profile(MLT_PRODUCER_SERVICE(mlt_frame_get_original_producer(frame)));
    const int w = *width > 0 ? *width : (profile ? profile->width : (*source)->image.width());
    const int h = *height > 0 ? *height : (profile ? profile->height : (*source)->image.height());
    if (w <= 0 || h <= 0) {
        return 1;
    }
    const QImage picture = (*source)->fittedTo(QSize(w, h));
    const int size = w * h * 4;
    auto* image = static_cast<uint8_t*>(mlt_pool_alloc(size));
    if (!image) {
        return 1;
    }
    for (int y = 0; y < h; ++y) {
        std::memcpy(image + static_cast<size_t>(y) * w * 4, picture.constScanLine(y),
                    static_cast<size_t>(w) * 4);
    }
    mlt_frame_set_image(frame, image, size, mlt_pool_release);
    *buffer = image;
    *format = mlt_image_rgba;
    *width = w;
    *height = h;
    mlt_properties_set_int(properties, "width", w);
    mlt_properties_set_int(properties, "height", h);
    mlt_properties_set_int(properties, "format", mlt_image_rgba);
    return 0;
}

int stillGetFrame(mlt_producer producer, mlt_frame_ptr frame, int /*index*/)
{
    *frame = mlt_frame_init(MLT_PRODUCER_SERVICE(producer));
    if (!*frame) {
        return 1;
    }
    mlt_properties producerProps = MLT_PRODUCER_PROPERTIES(producer);
    mlt_properties frameProps = MLT_FRAME_PROPERTIES(*frame);
    auto* source =
        static_cast<SourcePtr*>(mlt_properties_get_data(producerProps, kSourceKey, nullptr));
    if (source) {
        mlt_properties_set_data(frameProps, kSourceKey, new SourcePtr(*source), 0, deleteSourcePtr,
                                nullptr);
    }
    mlt_frame_set_position(*frame, mlt_producer_position(producer));
    mlt_properties_set_int(frameProps, "progressive", 1);
    mlt_properties_set_double(frameProps, "aspect_ratio", 1.0);
    mlt_properties_set_int(frameProps, "test_audio", 1); // stills are silent
    mlt_frame_push_get_image(*frame, stillGetImage);
    mlt_producer_prepare_next(producer);
    return 0;
}

} // namespace

QStringList stillImageFormats()
{
    QStringList formats;
    for (const QByteArray& format : QImageReader::supportedImageFormats()) {
        formats << QString::fromLatin1(format).toLower();
    }
    return formats;
}

QImage loadStillImage(const QString& path, QSize maxSize, QString* error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        if (error) {
            *error = reader.errorString();
        }
        return {};
    }
    QSize size = reader.size();
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
        size.transpose();
    }
    if (size.isValid() && maxSize.isValid() &&
        (size.width() > maxSize.width() || size.height() > maxSize.height())) {
        QSize target = size.scaled(maxSize, Qt::KeepAspectRatio);
        if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
            target.transpose(); // the scaled size applies before rotation
        }
        reader.setScaledSize(target);
    }
    QImage image = reader.read();
    if (image.isNull() && error) {
        *error = reader.errorString();
    }
    return image;
}

std::unique_ptr<Mlt::Producer> createStillProducer(Mlt::Profile& profile, const QString& path,
                                                   QString* error)
{
    // Keep up to twice the sequence resolution so scaling stays sharp without huge buffers.
    const QSize limit(std::max(profile.width() * 2, 256), std::max(profile.height() * 2, 256));
    QImage image = loadStillImage(path, limit, error);
    if (image.isNull()) {
        return nullptr;
    }
    auto source = std::make_shared<StillSource>();
    source->image = image.convertToFormat(QImage::Format_RGBA8888);

    mlt_producer producer = mlt_producer_new(profile.get_profile());
    if (!producer) {
        return nullptr;
    }
    producer->get_frame = stillGetFrame;
    mlt_properties properties = MLT_PRODUCER_PROPERTIES(producer);
    mlt_properties_set(properties, "resource", path.toUtf8().constData());
    mlt_properties_set(properties, "mlt_service", "ft.still");
    mlt_properties_set_int(properties, "length", INT_MAX / 2);
    mlt_properties_set_int(properties, "out", INT_MAX / 2 - 1);
    mlt_properties_set_int(properties, "meta.media.width", source->image.width());
    mlt_properties_set_int(properties, "meta.media.height", source->image.height());
    mlt_properties_set_int(properties, "seekable", 1);
    mlt_properties_set_data(properties, kSourceKey, new SourcePtr(source), 0, deleteSourcePtr,
                            nullptr);

    auto wrapper = std::make_unique<Mlt::Producer>(producer);
    mlt_producer_close(producer); // the wrapper holds its own reference
    // Let consumers that ask for another pixel format (for example YUV) get a converted image.
    Mlt::Filter convert(profile, "imageconvert");
    if (convert.is_valid()) {
        wrapper->attach(convert);
    }
    return wrapper;
}

} // namespace ft::engine
