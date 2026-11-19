// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/playback_controller.h"

#include "core/timecode.h"
#include "ui/project_controller.h"

#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ft::ui {

PlaybackController::PlaybackController(ProjectController* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(30);
    connect(&m_syncTimer, &QTimer::timeout, this, &PlaybackController::syncNow);
    connect(m_project, &ProjectController::projectChanged, this, [this] { m_syncTimer.start(); });
    connect(m_project, &ProjectController::documentReplaced, this, [this] {
        m_engine.pause();
        syncNow();
        m_engine.seek(0);
    });
    connect(&m_engine, &playback::PlaybackEngine::frameReady, this,
            [this](const QImage& image, FrameCount position) {
                m_frame = image;
                emit frameReady();
                if (position != m_position) {
                    m_position = position;
                    emit positionChanged();
                }
            });
    connect(&m_engine, &playback::PlaybackEngine::durationChanged, this,
            &PlaybackController::durationChanged);
    connect(&m_engine, &playback::PlaybackEngine::playingChanged, this,
            &PlaybackController::playingChanged);
    connect(&m_engine, &playback::PlaybackEngine::errorOccurred, this,
            &PlaybackController::errorOccurred);
    connect(&m_engine, &playback::PlaybackEngine::warningsChanged, this,
            [this](const QStringList& warnings) {
                if (warnings != m_warnings) {
                    m_warnings = warnings;
                    emit warningsChanged();
                }
            });
    // The engine opens the audio device on its worker thread; report the outcome once known.
    QTimer::singleShot(500, this, &PlaybackController::audioAvailableChanged);
    syncNow();
}

PlaybackController::~PlaybackController() = default;

QString PlaybackController::timecode() const
{
    return timecode::format(m_position, m_project->project().settings().frameRate);
}

QString PlaybackController::durationTimecode() const
{
    return timecode::format(m_engine.duration(), m_project->project().settings().frameRate);
}

void PlaybackController::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    m_muted = muted;
    m_engine.setAudioEnabled(!muted);
    emit mutedChanged();
}

void PlaybackController::setLoopPreview(bool enabled)
{
    if (enabled == m_loopPreview) {
        return;
    }
    m_loopPreview = enabled;
    m_engine.setLoopEnabled(enabled);
    emit loopPreviewChanged();
}

void PlaybackController::syncNow()
{
    m_syncTimer.stop();
    m_engine.setProject(m_project->project());
}

void PlaybackController::play()
{
    if (m_syncTimer.isActive()) {
        syncNow();
    }
    m_engine.play();
}

void PlaybackController::pause()
{
    m_engine.pause();
}

void PlaybackController::togglePlay()
{
    if (m_engine.isPlaying()) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::seek(double frame)
{
    // QML can supply NaN, infinity, or enormous finite doubles. Compare the
    // floating-point boundary before llround(): int64's largest values round
    // up to 2^63 in double and cannot safely be passed to llround().
    if (!std::isfinite(frame)) {
        return;
    }
    const FrameCount length = m_project->project().timeline().duration();
    const FrameCount last = length > 0 ? length - 1 : 0;
    const FrameCount target = frame <= 0.0 ? 0
                              : frame >= static_cast<double>(last)
                                  ? last
                                  : static_cast<FrameCount>(std::llround(frame));
    seekExact(target);
}

void PlaybackController::seekExact(FrameCount frame)
{
    // Integer transport requests must not make a round trip through double:
    // that loses individual frames in long-duration projects.
    if (m_syncTimer.isActive()) {
        syncNow();
    }
    const FrameCount length = m_project->project().timeline().duration();
    const FrameCount last = length > 0 ? length - 1 : 0;
    const FrameCount target = std::clamp(frame, FrameCount{0}, last);
    if (target != m_position) {
        m_position = target; // move the playhead immediately; the picture follows
        emit positionChanged();
    }
    m_engine.seek(target);
}

bool PlaybackController::seekToTimecode(const QString& label)
{
    const auto frame = timecode::parse(label, m_project->project().settings().frameRate);
    const FrameCount length = m_project->project().timeline().duration();
    if (!frame || *frame < 0 || *frame >= length) {
        return false;
    }
    seekExact(*frame);
    return true;
}

void PlaybackController::step(int frames)
{
    const FrameCount length = m_project->project().timeline().duration();
    const FrameCount last = length > 0 ? length - 1 : 0;
    const FrameCount current = std::clamp(m_position, FrameCount{0}, last);
    const FrameCount delta = frames;
    // Saturate before adding: frame positions near INT64_MAX must never wrap.
    const FrameCount target = delta > 0   ? (delta >= last - current ? last : current + delta)
                              : delta < 0 ? (-delta >= current ? 0 : current + delta)
                                          : current;
    seekExact(target);
}

bool PlaybackController::skipSeconds(int seconds)
{
    const FrameCount length = m_project->project().timeline().duration();
    if (length <= 0 || seconds == 0 || seconds < -60 || seconds > 60) {
        return false;
    }
    // Multiply the exact project rate, not the nominal SMPTE timecode rate
    // (30 fps would be wrong for 30000/1001). Bound input to avoid overflow.
    const FrameRate rate = m_project->project().settings().frameRate;
    const FrameCount last = length - 1;
    const double requested = static_cast<double>(std::abs(seconds)) * rate.toDouble();
    if (!std::isfinite(requested) || requested <= 0.0) {
        return false;
    }
    // A full-sequence skip needs no huge integer conversion, and the resulting
    // sum cannot overflow even when the playhead is near INT64_MAX.
    const FrameCount magnitude = requested >= static_cast<double>(last)
                                     ? last
                                     : static_cast<FrameCount>(std::llround(requested));
    const FrameCount current = std::clamp(m_position, FrameCount{0}, last);
    const FrameCount target = seconds < 0
                                  ? (magnitude >= current ? 0 : current - magnitude)
                                  : (magnitude >= last - current ? last : current + magnitude);
    if (target == m_position) {
        return false;
    }
    seekExact(target);
    return true;
}

bool PlaybackController::goToSequenceMidpoint()
{
    const FrameCount length = m_project->project().timeline().duration();
    if (length <= 0) {
        return false;
    }
    const FrameCount midpoint = (length - 1) / 2;
    seekExact(midpoint);
    return true;
}

void PlaybackController::goToStart()
{
    seek(0);
}

void PlaybackController::goToEnd()
{
    const FrameCount length = m_project->project().timeline().duration();
    seekExact(length > 0 ? length - 1 : 0);
}

bool PlaybackController::savePreviewFrame(const QString& targetPath)
{
    if (m_frame.isNull()) {
        emit errorOccurred(QStringLiteral("No preview frame is available to save."));
        return false;
    }
    const QFileInfo output(targetPath);
    if (targetPath.trimmed().isEmpty() ||
        output.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) != 0) {
        emit errorOccurred(QStringLiteral("Choose a filename ending in .png."));
        return false;
    }
    const QString destination = canonicalMediaKey(output.absoluteFilePath());
    if (!m_project->filePath().isEmpty() &&
        destination == canonicalMediaKey(m_project->filePath())) {
        emit errorOccurred(QStringLiteral("The preview cannot replace the project file."));
        return false;
    }
    for (const MediaAsset& asset : m_project->project().media()) {
        if (destination == canonicalMediaKey(asset.filePath)) {
            emit errorOccurred(QStringLiteral("The preview cannot overwrite source media."));
            return false;
        }
    }
    QSaveFile file(output.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(
            QStringLiteral("Could not open the snapshot destination: %1").arg(file.errorString()));
        return false;
    }
    if (!m_frame.save(&file, "PNG") || !file.commit()) {
        file.cancelWriting();
        emit errorOccurred(QStringLiteral("Could not save the preview PNG."));
        return false;
    }
    return true;
}

void PlaybackController::setPreviewSize(int width, int height)
{
    m_engine.setMaximumPreviewSize(QSize(std::max(width, 64), std::max(height, 36)));
}

} // namespace ft::ui
