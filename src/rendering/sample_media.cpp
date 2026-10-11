// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/sample_media.h"

#include "core/framerate.h"
#include "engine/mlt_profile.h"
#include "rendering/export_settings.h"
#include "rendering/render_runner.h"

#include <QFileInfo>
#include <QThread>
#include <mlt++/Mlt.h>

namespace ft::rendering {

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

QByteArray colorString(const QColor& c)
{
    return QStringLiteral("0x%1%2%3ff")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .toLatin1();
}

} // namespace

bool writeSampleVideo(const QString& path, const SampleVideoSpec& spec)
{
    const std::optional<FrameRate> rate = FrameRate::create(spec.fps, 1);
    if (!rate || spec.segmentColors.isEmpty() || spec.size.isEmpty()) {
        return false;
    }
    auto profile = engine::createProfile(spec.size.width(), spec.size.height(), *rate);
    const int total = static_cast<int>(spec.segmentColors.size()) * spec.fps;

    Mlt::Playlist pictures(*profile);
    for (const QColor& color : spec.segmentColors) {
        Mlt::Producer solid(*profile, "color", colorString(color).constData());
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

    const QString encoder = chooseVideoEncoder(availableVideoEncoders());
    if (encoder.isEmpty()) {
        return false;
    }
    const QByteArray target = path.toUtf8(); // MLT and FFmpeg expect UTF-8 paths
    Mlt::Consumer consumer(*profile, "avformat", target.constData());
    if (!consumer.is_valid()) {
        return false;
    }
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
    return runConsumer(consumer, tractor) && QFileInfo(path).size() > 0;
}

bool writeSampleTone(const QString& path, double seconds, double frequency)
{
    auto profile = engine::createProfile(64, 36, *FrameRate::create(25, 1));
    const int total = static_cast<int>(seconds * 25);
    if (total <= 0) {
        return false;
    }
    Mlt::Producer tone(*profile, "tone");
    tone.set("frequency", frequency);
    tone.set("level", -6.0);
    tone.set("length", total);
    tone.set_in_and_out(0, total - 1);
    const QByteArray target = path.toUtf8(); // MLT and FFmpeg expect UTF-8 paths
    Mlt::Consumer consumer(*profile, "avformat", target.constData());
    if (!consumer.is_valid()) {
        return false;
    }
    consumer.set("f", "wav");
    consumer.set("acodec", "pcm_s16le");
    consumer.set("ar", 48000);
    consumer.set("ac", 2);
    consumer.set("vn", 1);
    consumer.set("video_off", 1);
    consumer.set("in", 0);
    consumer.set("out", total - 1);
    return runConsumer(consumer, tone) && QFileInfo(path).size() > 0;
}

} // namespace ft::rendering
