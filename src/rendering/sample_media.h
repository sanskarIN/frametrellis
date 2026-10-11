// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QColor>
#include <QList>
#include <QSize>
#include <QString>

/// Generates small synthetic media files with the MLT engine itself. Used by the self-test that
/// checks an installation and by the automated tests, so no sample footage has to be shipped or
/// committed. Requires ft::engine::MltRuntime::initialize().
namespace ft::rendering {

struct SampleVideoSpec
{
    QList<QColor> segmentColors{Qt::red, Qt::green, Qt::blue}; ///< one solid colour per second
    int fps = 25;
    QSize size{160, 90};
    bool withAudio = true;
    double toneFrequency = 440.0;
};

/// Writes an MP4 (H.264 or the best available encoder, AAC audio) whose picture is one solid
/// colour per second. Returns false if the engine could not encode the file.
bool writeSampleVideo(const QString& path, const SampleVideoSpec& spec = {});

/// Writes a stereo 48 kHz 16-bit WAV file containing a sine tone at -6 dBFS, exactly
/// @p seconds long.
bool writeSampleTone(const QString& path, double seconds, double frequency = 440.0);

} // namespace ft::rendering
