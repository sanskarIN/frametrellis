// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"
#include "rendering/export_settings.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

namespace ft::rendering {

/// Runs one export in the frametrellis-render helper process and tracks its progress.
///
/// The project is snapshotted to a temporary file when the job starts, so the user can keep
/// editing (or close the project) while it renders, and an encoder crash only fails the job.
class RenderJob : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Running, Cancelling, Succeeded, Failed, Cancelled };
    Q_ENUM(State)

    /// The allowed state machine transitions (see docs/architecture/rendering.md).
    static bool canTransition(State from, State to);
    static bool isFinished(State state);

    explicit RenderJob(QString helperProgram, QObject* parent = nullptr);
    ~RenderJob() override;

    /// Validates the settings, snapshots the project and launches the helper.
    Status start(const Project& project, const ExportSettings& settings,
                 const QString& projectFilePath = {});
    /// Asks the helper to stop; it is killed if it does not exit within the grace period.
    void cancel();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] double progress() const;
    [[nodiscard]] FrameCount framesDone() const { return m_framesDone; }
    [[nodiscard]] FrameCount totalFrames() const { return m_totalFrames; }
    [[nodiscard]] const QString& encoder() const { return m_encoder; }
    [[nodiscard]] const QString& outputPath() const { return m_settings.outputPath; }
    [[nodiscard]] const QString& errorMessage() const { return m_errorMessage; }
    [[nodiscard]] const QString& errorDetail() const { return m_errorDetail; }
    [[nodiscard]] qint64 elapsedMilliseconds() const;
    /// Average completed frames/second since the export started (zero until measurable).
    [[nodiscard]] double framesPerSecond() const;
    /// Estimated remaining time, or -1 while unknown.
    [[nodiscard]] qint64 remainingMilliseconds() const;

    void setKillGracePeriod(int milliseconds) { m_killTimer.setInterval(milliseconds); }

signals:
    void stateChanged(ft::rendering::RenderJob::State state);
    void progressChanged();
    void finished(ft::rendering::RenderJob::State state);

private:
    void setState(State state);
    void fail(const QString& message, const QString& detail = {});
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void handleEvent(const QJsonObject& event);

    QString m_helperProgram;
    State m_state = State::Idle;
    ExportSettings m_settings;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QTemporaryDir> m_snapshotDir;
    QTimer m_killTimer;
    QElapsedTimer m_elapsed;
    qint64 m_finalElapsed = -1;
    QByteArray m_buffer;
    bool m_discardingOversizedLine = false;
    FrameCount m_framesDone = 0;
    FrameCount m_totalFrames = 0;
    QString m_encoder;
    QString m_errorMessage;
    QString m_errorDetail;
    bool m_sawFinished = false;
};

} // namespace ft::rendering
