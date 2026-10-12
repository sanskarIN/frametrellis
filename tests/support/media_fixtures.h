// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

/// Generates small media files for tests with the MLT engine itself, so no media needs to be
/// committed. Requires ft::engine::MltRuntime::initialize().
namespace ft::testing {

struct VideoSpec
{
    QList<QColor> segmentColors{Qt::red, Qt::green, Qt::blue}; ///< one solid colour per second
    int fps = 25;
    QSize size{160, 90};
    bool withAudio = true;
    double toneFrequency = 440.0;
};

/// Writes an MP4 whose picture is one solid colour per second. Returns the path or empty on error.
QString makeVideo(const QString& directory, const QString& name, const VideoSpec& spec = {});
/// Writes a stereo 48 kHz WAV file containing a sine tone.
QString makeTone(const QString& directory, const QString& name, double seconds,
                 double frequency = 440.0);
/// Writes a PNG image filled with one colour.
QString makeStill(const QString& directory, const QString& name, const QColor& color,
                  QSize size = {64, 48});

/// Mean colour of an image region (whole image by default).
QColor averageColor(const QImage& image, QRect region = {});
bool isCloseTo(const QColor& actual, const QColor& expected, int tolerance = 40);

/// Peak absolute sample value in interleaved 16-bit audio.
int peakLevel(const std::vector<std::int16_t>& samples);

} // namespace ft::testing
