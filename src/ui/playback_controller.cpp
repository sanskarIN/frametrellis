// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/playback_controller.h"

#include "core/timecode.h"
#include "ui/project_controller.h"

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

void PlaybackController::goToStart()
{
    seek(0);
}

void PlaybackController::goToEnd()
{
    seek(static_cast<double>(
        std::max<FrameCount>(m_project->project().timeline().duration() - 1, 0)));
}

void PlaybackController::setPreviewSize(int width, int height)
{
    m_engine.setMaximumPreviewSize(QSize(std::max(width, 64), std::max(height, 36)));
}

} // namespace ft::ui
