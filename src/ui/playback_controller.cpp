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
    // The engine must know the latest sequence before it clamps the seek target.
    if (m_syncTimer.isActive()) {
        syncNow();
    }
    const FrameCount length = m_project->project().timeline().duration();
    const auto target = std::clamp<FrameCount>(static_cast<FrameCount>(std::llround(frame)), 0,
                                               std::max<FrameCount>(length - 1, 0));
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
    seek(static_cast<double>(*frame));
    return true;
}

void PlaybackController::step(int frames)
{
    seek(static_cast<double>(m_position + frames));
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
    const FrameCount magnitude = static_cast<FrameCount>(
        std::llround(static_cast<double>(std::abs(seconds)) * rate.toDouble()));
    const FrameCount delta = seconds < 0 ? -magnitude : magnitude;
    const FrameCount target = std::clamp<FrameCount>(m_position + delta, 0, length - 1);
    if (target == m_position) {
        return false;
    }
    seek(static_cast<double>(target));
    return true;
}

bool PlaybackController::goToSequenceMidpoint()
{
    const FrameCount length = m_project->project().timeline().duration();
    if (length <= 0) {
        return false;
    }
    const FrameCount midpoint = (length - 1) / 2;
    seek(static_cast<double>(midpoint));
    return true;
}

void PlaybackController::goToStart()
{
    seek(0);
}

void PlaybackController::goToEnd()
{
    seek(static_cast<double>(
        std::max<FrameCount>(m_project->project().timeline().duration() - 1, 0)));
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
