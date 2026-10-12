// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media_fixtures.h"

#include "rendering/sample_media.h"

#include <QDir>

#include <cstdlib>

namespace ft::testing {

QString makeVideo(const QString& directory, const QString& name, const VideoSpec& spec)
{
    const QString path = QDir(directory).filePath(name);
    rendering::SampleVideoSpec sample;
    sample.segmentColors = spec.segmentColors;
    sample.fps = spec.fps;
    sample.size = spec.size;
    sample.withAudio = spec.withAudio;
    sample.toneFrequency = spec.toneFrequency;
    return rendering::writeSampleVideo(path, sample) ? path : QString();
}

QString makeTone(const QString& directory, const QString& name, double seconds, double frequency)
{
    const QString path = QDir(directory).filePath(name);
    return rendering::writeSampleTone(path, seconds, frequency) ? path : QString();
}

QString makeStill(const QString& directory, const QString& name, const QColor& color, QSize size)
{
    const QString path = QDir(directory).filePath(name);
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path) ? path : QString();
}

QColor averageColor(const QImage& image, QRect region)
{
    if (image.isNull()) {
        return {};
    }
    if (region.isNull()) {
        region = image.rect();
    }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    qint64 r = 0;
    qint64 g = 0;
    qint64 b = 0;
    qint64 n = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            const QRgb p = rgb.pixel(x, y);
            r += qRed(p);
            g += qGreen(p);
            b += qBlue(p);
            ++n;
        }
    }
    return n == 0 ? QColor() : QColor(int(r / n), int(g / n), int(b / n));
}

bool isCloseTo(const QColor& actual, const QColor& expected, int tolerance)
{
    return std::abs(actual.red() - expected.red()) <= tolerance &&
           std::abs(actual.green() - expected.green()) <= tolerance &&
           std::abs(actual.blue() - expected.blue()) <= tolerance;
}

int peakLevel(const std::vector<std::int16_t>& samples)
{
    int peak = 0;
    for (const std::int16_t s : samples) {
        peak = std::max(peak, std::abs(static_cast<int>(s)));
    }
    return peak;
}

} // namespace ft::testing
