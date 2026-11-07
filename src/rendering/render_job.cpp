// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/render_job.h"

#include "core/logging.h"
#include "project/project_serializer.h"
#include "rendering/render_protocol.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace ft::rendering {

bool RenderJob::canTransition(State from, State to)
{
    switch (from) {
    case State::Idle:
        return to == State::Starting;
    case State::Starting:
        return to == State::Running || to == State::Cancelling || to == State::Failed;
    case State::Running:
        return to == State::Cancelling || to == State::Succeeded || to == State::Failed;
    case State::Cancelling:
        return to == State::Cancelled || to == State::Succeeded || to == State::Failed;
    case State::Succeeded:
    case State::Failed:
    case State::Cancelled:
        return to == State::Starting;
    }
    return false;
}

bool RenderJob::isFinished(State state)
{
    return state == State::Succeeded || state == State::Failed || state == State::Cancelled;
}

RenderJob::RenderJob(QString helperProgram, QObject* parent)
    : QObject(parent)
    , m_helperProgram(std::move(helperProgram))
{
    m_killTimer.setSingleShot(true);
    m_killTimer.setInterval(10'000);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            qCWarning(lcRender) << "Render helper did not stop in time; killing it";
            m_process->kill();
        }
    });
}

RenderJob::~RenderJob()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void RenderJob::setState(State state)
{
    if (state == m_state) {
        return;
    }
    if (!canTransition(m_state, state)) {
        qCWarning(lcRender) << "Ignoring invalid render state transition" << int(m_state) << "->"
                            << int(state);
        return;
    }
    m_state = state;
    if (isFinished(state)) {
        m_finalElapsed = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
        m_killTimer.stop();
        m_snapshotDir.reset();
    }
    emit stateChanged(state);
    if (isFinished(state)) {
        emit finished(state);
    }
}

void RenderJob::fail(const QString& message, const QString& detail)
{
    if (m_errorMessage.isEmpty()) {
        m_errorMessage = message;
        m_errorDetail = detail;
    }
    setState(State::Failed);
}

Status RenderJob::start(const Project& project, const ExportSettings& settings,
                        const QString& projectFilePath)
{
    if (!canTransition(m_state, State::Starting)) {
        return makeError(ErrorCode::Conflict, QStringLiteral("An export is already running."));
    }
    if (const Status valid = validateExport(settings, project, projectFilePath); !valid) {
        return valid;
    }
    if (!QFileInfo(m_helperProgram).isExecutable()) {
        return makeError(ErrorCode::NotFound,
                         QStringLiteral("The render helper is missing from this installation."),
                         m_helperProgram);
    }
    auto snapshotDir = std::make_unique<QTemporaryDir>();
    if (!snapshotDir->isValid()) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("Could not create a temporary folder for the export."));
    }
    const QString snapshot = snapshotDir->filePath(QStringLiteral("render.ftproj"));
    if (const Status saved = project_format::saveToFile(project, snapshot); !saved) {
        return saved;
    }

    m_snapshotDir = std::move(snapshotDir);
    m_settings = settings;
    m_framesDone = 0;
    m_totalFrames = project.timeline().duration();
    m_lastLoggedProgressBucket = -1;
    m_encoder.clear();
    m_errorMessage.clear();
    m_errorDetail.clear();
    m_buffer.clear();
    m_stderrTail.clear();
    m_discardingOversizedLine = false;
    m_sawFinished = false;
    m_finalElapsed = -1;
    m_elapsed.start();

    QStringList arguments{
        QStringLiteral("--project"),       snapshot,
        QStringLiteral("--output"),        settings.outputPath,
        QStringLiteral("--short-side"),    QString::number(settings.shortSide),
        QStringLiteral("--crf"),           QString::number(settings.crf),
        QStringLiteral("--preset"),        settings.speedPreset,
        QStringLiteral("--audio-bitrate"), QString::number(settings.audioBitrateKbps),
        QStringLiteral("--fps"),           settings.frameRate,
        QStringLiteral("--watch-stdin")};
    if (!settings.includeAudio) {
        arguments << QStringLiteral("--no-audio");
    }
    if (settings.avoidUpscaling) {
        arguments << QStringLiteral("--no-upscale");
    }

    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_helperProgram);
    m_process->setArguments(arguments); // passed as a list: no shell, no quoting issues
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &RenderJob::onReadyRead);
    connect(m_process.get(), &QProcess::readyReadStandardError, this, [this] {
        const QByteArray output = m_process->readAllStandardError();
        if (!output.isEmpty()) {
            // A runaway FFmpeg log must not grow memory without bound. Preserve the
            // last diagnostic lines so an encoder crash has an actionable error.
            constexpr qsizetype kMaxStderrBytes = 16'384;
            m_stderrTail += output.right(kMaxStderrBytes);
            if (m_stderrTail.size() > kMaxStderrBytes) {
                m_stderrTail.remove(0, m_stderrTail.size() - kMaxStderrBytes);
            }
            qCDebug(lcRender).noquote()
                << "helper:" << QString::fromUtf8(output.right(1024)).trimmed();
        }
    });
    connect(m_process.get(), &QProcess::started, this, [this] {
        if (m_state == State::Starting) {
            setState(State::Running);
        }
    });
    connect(m_process.get(), &QProcess::finished, this, &RenderJob::onProcessFinished);
    connect(m_process.get(), &QProcess::errorOccurred, this, &RenderJob::onProcessError);

    setState(State::Starting);
    qCInfo(lcRender) << "Starting render helper for" << settings.outputPath;
    m_process->start(QIODevice::ReadWrite);
    emit progressChanged();
    return {};
}

void RenderJob::cancel()
{
    if (m_state != State::Starting && m_state != State::Running) {
        return;
    }
    setState(State::Cancelling);
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->write("cancel\n");
        m_process->closeWriteChannel();
        m_killTimer.start();
    }
}

double RenderJob::progress() const
{
    if (m_state == State::Succeeded) {
        return 1.0;
    }
    if (m_totalFrames <= 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(m_framesDone) / static_cast<double>(m_totalFrames), 0.0,
                      1.0);
}

qint64 RenderJob::elapsedMilliseconds() const
{
    if (m_finalElapsed >= 0) {
        return m_finalElapsed;
    }
    return m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
}

double RenderJob::framesPerSecond() const
{
    const qint64 ms = elapsedMilliseconds();
    if (ms < 1000 || m_framesDone <= 0 || m_state != State::Running) {
        return 0.0;
    }
    return static_cast<double>(m_framesDone) * 1000.0 / static_cast<double>(ms);
}

qint64 RenderJob::remainingMilliseconds() const
{
    const double done = progress();
    if (m_state != State::Running || done < 0.02) {
        return -1;
    }
    const auto elapsed = static_cast<double>(elapsedMilliseconds());
    return static_cast<qint64>(elapsed / done - elapsed);
}

void RenderJob::onReadyRead()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        // Skip the rest of a line whose earlier fragments exceeded the limit.
        if (!m_discardingOversizedLine && newline <= protocol::kMaxEventBytes) {
            if (const auto event = protocol::decode(m_buffer.left(newline))) {
                handleEvent(*event);
            }
        }
        m_buffer.remove(0, newline + 1);
        m_discardingOversizedLine = false;
    }
    if (m_buffer.size() > protocol::kMaxEventBytes) {
        // A helper or native library may print arbitrary output without a newline.
        // Drop it in bounded space, then ignore fragments until its next newline.
        m_buffer.clear();
        m_discardingOversizedLine = true;
    }
}

void RenderJob::handleEvent(const QJsonObject& event)
{
    const QString type = event.value(QLatin1String("event")).toString();
    if (type == QLatin1String("started")) {
        const auto total = protocol::frameCount(event.value(QLatin1String("total")));
        if (!total || *total == 0) {
            qCWarning(lcRender) << "Ignored invalid renderer started frame count";
            return;
        }
        m_encoder = event.value(QLatin1String("encoder")).toString();
        m_totalFrames = *total;
        qCInfo(lcRender) << "Render helper started with encoder" << m_encoder << "and"
                         << m_totalFrames << "output frames";
        emit progressChanged();
    } else if (type == QLatin1String("progress")) {
        const auto frame = protocol::frameCount(event.value(QLatin1String("frame")));
        const auto total = protocol::frameCount(event.value(QLatin1String("total")));
        if (!frame || !total || *total == 0 || *frame > *total) {
            qCWarning(lcRender) << "Ignored invalid renderer progress counts";
            return;
        }
        m_framesDone = *frame;
        m_totalFrames = *total;
        // Keep the last known export position in the editor's persistent log.
        // A sudden Windows process termination cannot run Qt shutdown handlers,
        // but the next startup can still correlate its timestamp with this marker.
        if (m_totalFrames > 0 && (m_state == State::Running || m_state == State::Starting)) {
            const int bucket = static_cast<int>(progress() * 10.0);
            if (bucket > m_lastLoggedProgressBucket) {
                m_lastLoggedProgressBucket = bucket;
                qCInfo(lcRender) << "Export progress" << bucket * 10 << "%" << m_framesDone << "/"
                                 << m_totalFrames << "frames";
            }
        }
        emit progressChanged();
    } else if (type == QLatin1String("finished")) {
        m_sawFinished = true;
        m_framesDone = m_totalFrames;
        emit progressChanged();
    } else if (type == QLatin1String("error")) {
        m_errorMessage = event.value(QLatin1String("message")).toString();
        m_errorDetail = event.value(QLatin1String("detail")).toString();
    }
}

void RenderJob::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    onReadyRead(); // drain remaining protocol events
    // A process can exit before the final stderr-ready notification. Do not lose the
    // decoder/encoder diagnostics that explain why a Windows export failed.
    const QByteArray remainingStderr = m_process->readAllStandardError();
    if (!remainingStderr.isEmpty()) {
        constexpr qsizetype kMaxStderrBytes = 16'384;
        m_stderrTail += remainingStderr.right(kMaxStderrBytes);
        if (m_stderrTail.size() > kMaxStderrBytes) {
            m_stderrTail.remove(0, m_stderrTail.size() - kMaxStderrBytes);
        }
    }
    if (isFinished(m_state)) {
        return;
    }
    if (status == QProcess::NormalExit && exitCode == protocol::Success && m_sawFinished) {
        setState(State::Succeeded);
    } else if (m_state == State::Cancelling ||
               (status == QProcess::NormalExit && exitCode == protocol::Cancelled)) {
        if (m_state != State::Cancelling) {
            setState(State::Cancelling);
        }
        // A killed helper may leave its temporary file behind; it never touches the destination.
        setState(State::Cancelled);
    } else {
        QString detail = QStringLiteral("Renderer exit code: %1 (0x%2)")
                             .arg(exitCode)
                             .arg(QString::number(static_cast<quint32>(exitCode), 16)
                                      .rightJustified(8, QLatin1Char('0')));
        if (!m_errorDetail.isEmpty()) {
            detail += QLatin1Char('\n') + m_errorDetail;
        }
        const QString stderrText = QString::fromUtf8(m_stderrTail).trimmed();
        if (!stderrText.isEmpty()) {
            detail += QStringLiteral("\nRenderer stderr (last 16 KiB):\n") + stderrText;
        }
        qCWarning(lcRender).noquote() << "Render helper failed:" << detail;
        fail(status == QProcess::CrashExit
                 ? QStringLiteral(
                       "The renderer stopped unexpectedly. Your project is safe; please try again.")
             : m_errorMessage.isEmpty()
                 ? QStringLiteral("Rendering failed (exit code %1).").arg(exitCode)
                 : m_errorMessage,
             detail);
    }
    qCInfo(lcRender) << "Render job finished with state" << int(m_state);
}

void RenderJob::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        fail(QStringLiteral("The render helper could not be started."),
             m_helperProgram + QStringLiteral(": ") + m_process->errorString());
    }
}

} // namespace ft::rendering
