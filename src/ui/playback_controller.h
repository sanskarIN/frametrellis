// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "playback/playback_engine.h"

#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>

namespace ft::ui {

class ProjectController;

/// QML-facing transport: play/pause, seeking and timecode, backed by PlaybackEngine. Project
/// changes are forwarded to the engine as snapshots, debounced so slider drags stay smooth.
class PlaybackController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY positionChanged)
    Q_PROPERTY(QString durationTimecode READ durationTimecode NOTIFY durationChanged)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY audioAvailableChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameReady)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
    PlaybackController(ProjectController* project, QObject* parent = nullptr);
    ~PlaybackController() override;

    [[nodiscard]] double position() const { return static_cast<double>(m_position); }
    [[nodiscard]] double duration() const { return static_cast<double>(m_engine.duration()); }
    [[nodiscard]] bool playing() const { return m_engine.isPlaying(); }
    [[nodiscard]] QString timecode() const;
    [[nodiscard]] QString durationTimecode() const;
    [[nodiscard]] bool audioAvailable() const { return m_engine.hasAudioOutput(); }
    [[nodiscard]] bool hasFrame() const { return !m_frame.isNull(); }
    [[nodiscard]] QStringList warnings() const { return m_warnings; }
    [[nodiscard]] bool muted() const { return m_muted; }
    void setMuted(bool muted);
    [[nodiscard]] FrameCount positionFrames() const { return m_position; }
    [[nodiscard]] const QImage& currentFrame() const { return m_frame; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double frame);
    /// Move to an exact SMPTE timecode within the current sequence; rejects invalid/out-of-range
    /// input.
    Q_INVOKABLE bool seekToTimecode(const QString& label);
    Q_INVOKABLE void step(int frames);
    Q_INVOKABLE void goToStart();
    Q_INVOKABLE void goToEnd();
    Q_INVOKABLE void setPreviewSize(int width, int height);
    /// Pushes the latest project snapshot to the engine immediately.
    Q_INVOKABLE void syncNow();

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void audioAvailableChanged();
    void frameReady();
    void warningsChanged();
    void mutedChanged();
    void errorOccurred(const QString& message);

private:
    ProjectController* m_project;
    playback::PlaybackEngine m_engine;
    QTimer m_syncTimer;
    QImage m_frame;
    FrameCount m_position = 0;
    QStringList m_warnings;
    bool m_muted = false;
};

} // namespace ft::ui
