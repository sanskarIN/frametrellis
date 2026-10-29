// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "project/project.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QStringList>

#include <atomic>
#include <memory>

namespace ft::playback {

/// Real-time preview of a project's timeline.
///
/// A dedicated worker thread owns every MLT object (profile, graph builder, tractor), renders
/// frames at preview resolution and paces them by the audio device clock, or by the wall clock
/// when no audio device is available. Seeks are coalesced so that scrubbing never queues work, and
/// only the most recent frame is handed to the UI thread, so a slow UI cannot cause a backlog.
/// Requests invalidate superseded frames before publication and queued GUI delivery, keeping
/// an old picture from reversing a newer seek or restoring a replaced project snapshot.
///
/// The engine works on snapshots: setProject() copies the project, so edits never race with
/// rendering.
class PlaybackEngine : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine() override;

    void setProject(const Project& project);
    void clearProject();
    void play();
    void pause();
    void togglePlayback();
    void seek(FrameCount frame);
    void step(int frames);
    /// Largest picture size the UI wants; frames keep the project's aspect ratio.
    void setMaximumPreviewSize(QSize size);
    void setAudioEnabled(bool enabled);
    void setVolume(double volume);

    [[nodiscard]] FrameCount position() const { return m_position.load(); }
    [[nodiscard]] FrameCount duration() const { return m_duration.load(); }
    [[nodiscard]] bool isPlaying() const { return m_playing.load(); }
    [[nodiscard]] bool hasAudioOutput() const { return m_audioAvailable.load(); }
    [[nodiscard]] QImage currentFrame() const;
    /// Number of frames rendered since construction (diagnostics and tests).
    [[nodiscard]] qint64 renderedFrameCount() const { return m_renderedFrames.load(); }
    /// Number of frames handed to the UI thread (diagnostics and tests).
    [[nodiscard]] qint64 deliveredFrameCount() const { return m_deliveredFrames.load(); }

signals:
    void frameReady(const QImage& image, ft::FrameCount position);
    void positionChanged(ft::FrameCount position);
    void durationChanged(ft::FrameCount duration);
    void playingChanged(bool playing);
    void errorOccurred(const QString& message);
    void warningsChanged(const QStringList& warnings);

private:
    class Worker;
    friend class Worker;

    void deliverFromWorker();

    std::atomic<FrameCount> m_position{0};
    std::atomic<FrameCount> m_duration{0};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_audioAvailable{false};
    std::atomic<qint64> m_renderedFrames{0};
    std::atomic<qint64> m_deliveredFrames{0};
    FrameCount m_lastEmittedDuration = -1;
    bool m_lastEmittedPlaying = false;
    QImage m_displayed;
    // Construct last: Worker starts its thread in its constructor and can immediately
    // publish audio/position state. All shared owner members must already be alive.
    std::unique_ptr<Worker> m_worker;
};

} // namespace ft::playback
