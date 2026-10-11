// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media_fixtures.h"

#include "core/framerate.h"
#include "engine/mlt_profile.h"
#include "rendering/export_settings.h"
#include "rendering/render_runner.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>

#include <mlt++/Mlt.h>

#include <cstdlib>

namespace ft::testing {

namespace {

bool runConsumer(Mlt::Consumer& consumer, Mlt::Producer& producer)
{
    consumer.set("real_time", 0); // see RenderRunner: safe with every MLT version
    consumer.set("terminate_on_pause", 1);
    consumer.connect(producer);
    if (consumer.start() != 0) {
        return false;
    }
    while (!consumer.is_stopped()) {
        QThread::msleep(5);
    }
    consumer.stop();
    return true;
}

QString colorString(const QColor& c)
{
    return QStringLiteral("0x%1%2%3ff")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'));
}

} // namespace

QString makeVideo(const QString& directory, const QString& name, const VideoSpec& spec)
{
    const QString path = QDir(directory).filePath(name);
    auto profile = engine::createProfile(spec.size.width(), spec.size.height(), *FrameRate::create(spec.fps, 1));
    const int total = static_cast<int>(spec.segmentColors.size()) * spec.fps;

    Mlt::Playlist pictures(*profile);
    for (const QColor& color : spec.segmentColors) {
        Mlt::Producer solid(*profile, "color", colorString(color).toUtf8().constData());
        solid.set("length", spec.fps);
        pictures.append(solid, 0, spec.fps - 1);
    }
    Mlt::Tractor tractor(*profile);
    tractor.set_track(pictures, 0);
    if (spec.withAudio) {
        Mlt::Producer tone(*profile, "tone");
        tone.set("frequency", spec.toneFrequency);
        tone.set("level", -6.0);
        tone.set("length", total);
        Mlt::Playlist sound(*profile);
        sound.append(tone, 0, total - 1);
        tractor.set_track(sound, 1);
        Mlt::Transition mix(*profile, "mix");
        mix.set("always_active", 1);
        mix.set("sum", 1);
        tractor.plant_transition(mix, 0, 1);
    }

    const QString encoder = rendering::chooseVideoEncoder(rendering::availableVideoEncoders());
    Mlt::Consumer consumer(*profile, "avformat", path.toUtf8().constData());
    consumer.set("f", "mp4");
    consumer.set("vcodec", encoder.toUtf8().constData());
    consumer.set("pix_fmt", "yuv420p");
    if (encoder == QLatin1String("libx264")) {
        consumer.set("preset", "ultrafast");
        consumer.set("crf", 18);
    }
    if (spec.withAudio) {
        consumer.set("acodec", "aac");
        consumer.set("ar", 48000);
        consumer.set("ac", 2);
    } else {
        consumer.set("an", 1);
    }
    consumer.set("in", 0);
    consumer.set("out", total - 1);
    if (!runConsumer(consumer, tractor)) {
        return {};
    }
    return QFileInfo::exists(path) ? path : QString();
}

QString makeTone(const QString& directory, const QString& name, double seconds, double frequency)
{
    const QString path = QDir(directory).filePath(name);
    auto profile = engine::createProfile(64, 36, *FrameRate::create(25, 1));
    const int total = static_cast<int>(seconds * 25);
    Mlt::Producer tone(*profile, "tone");
    tone.set("frequency", frequency);
    tone.set("level", -6.0);
    tone.set("length", total);
    tone.set_in_and_out(0, total - 1);
    Mlt::Consumer consumer(*profile, "avformat", path.toUtf8().constData());
    consumer.set("f", "wav");
    consumer.set("acodec", "pcm_s16le");
    consumer.set("ar", 48000);
    consumer.set("ac", 2);
    consumer.set("vn", 1);
    consumer.set("video_off", 1);
    consumer.set("in", 0);
    consumer.set("out", total - 1);
    if (!runConsumer(consumer, tone)) {
        return {};
    }
    return QFileInfo::exists(path) ? path : QString();
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
    return std::abs(actual.red() - expected.red()) <= tolerance
           && std::abs(actual.green() - expected.green()) <= tolerance
           && std::abs(actual.blue() - expected.blue()) <= tolerance;
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
