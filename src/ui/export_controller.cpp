// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/export_controller.h"

#include "ui/project_controller.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace ft::ui {

using rendering::RenderJob;

namespace {

QString formatMillis(qint64 ms)
{
    const qint64 seconds = ms / 1000;
    return seconds >= 3600 ? QStringLiteral("%1:%2:%3")
                                 .arg(seconds / 3600)
                                 .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                                 .arg(seconds % 60, 2, 10, QLatin1Char('0'))
                           : QStringLiteral("%1:%2")
                                 .arg(seconds / 60)
                                 .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

ExportController::ExportController(ProjectController* project, QString helperProgram,
                                   QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_helperProgram(std::move(helperProgram))
{
    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &ExportController::progressChanged);
}

ExportController::~ExportController() = default;

QString ExportController::state() const
{
    if (!m_job) {
        return QStringLiteral("idle");
    }
    switch (m_job->state()) {
    case RenderJob::State::Idle:
        return QStringLiteral("idle");
    case RenderJob::State::Starting:
        return QStringLiteral("starting");
    case RenderJob::State::Running:
        return QStringLiteral("running");
    case RenderJob::State::Cancelling:
        return QStringLiteral("cancelling");
    case RenderJob::State::Succeeded:
        return QStringLiteral("succeeded");
    case RenderJob::State::Failed:
        return QStringLiteral("failed");
    case RenderJob::State::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("idle");
}

bool ExportController::busy() const
{
    return m_batchActive || (m_job && !RenderJob::isFinished(m_job->state()) &&
                             m_job->state() != RenderJob::State::Idle);
}

double ExportController::progress() const
{
    return m_job ? m_job->progress() : 0.0;
}

QString ExportController::progressText() const
{
    if (!m_job) {
        return {};
    }
    const QString elapsed = formatMillis(m_job->elapsedMilliseconds());
    switch (m_job->state()) {
    case RenderJob::State::Starting:
        return QStringLiteral("Preparing…");
    case RenderJob::State::Running: {
        const qint64 remaining = m_job->remainingMilliseconds();
        const double fps = m_job->framesPerSecond();
        return QStringLiteral("%1% · %2 elapsed%3%4")
            .arg(qRound(m_job->progress() * 100))
            .arg(elapsed,
                 remaining >= 0 ? QStringLiteral(" · about %1 left").arg(formatMillis(remaining))
                                : QString(),
                 fps > 0.0 ? QStringLiteral(" · %1 frames/s").arg(fps, 0, 'f', 1) : QString());
    }
    case RenderJob::State::Cancelling:
        return QStringLiteral("Cancelling…");
    case RenderJob::State::Succeeded:
        return QStringLiteral("Finished in %1").arg(elapsed);
    case RenderJob::State::Cancelled:
        return QStringLiteral("Cancelled after %1").arg(elapsed);
    case RenderJob::State::Failed:
        return QStringLiteral("Failed after %1").arg(elapsed);
    case RenderJob::State::Idle:
        break;
    }
    return {};
}

QString ExportController::errorMessage() const
{
    return m_job ? m_job->errorMessage() : QString();
}

QString ExportController::errorDetail() const
{
    return m_job ? m_job->errorDetail() : QString();
}

QString ExportController::outputPath() const
{
    return m_job ? m_job->outputPath() : QString();
}

QString ExportController::encoder() const
{
    return m_job ? m_job->encoder() : QString();
}

QVariantList ExportController::presets() const
{
    QVariantList list;
    for (const rendering::ExportPreset& preset : rendering::exportPresets()) {
        list.append(QVariantMap{{QStringLiteral("id"), preset.id},
                                {QStringLiteral("name"), preset.name},
                                {QStringLiteral("description"), preset.description},
                                {QStringLiteral("shortSide"), preset.shortSide},
                                {QStringLiteral("crf"), preset.crf},
                                {QStringLiteral("speedPreset"), preset.speedPreset},
                                {QStringLiteral("audioBitrateKbps"), preset.audioBitrateKbps},
                                {QStringLiteral("avoidUpscaling"), preset.avoidUpscaling}});
    }
    return list;
}

QStringList ExportController::speedPresets() const
{
    return rendering::speedPresets();
}

double ExportController::batchProgress() const
{
    if (m_batchJobs.isEmpty()) {
        return progress();
    }
    const double current =
        m_batchActive && m_job && !RenderJob::isFinished(m_job->state()) ? m_job->progress() : 0.0;
    return qBound(0.0, (static_cast<double>(m_batchCompleted) + current) / m_batchJobs.size(), 1.0);
}

QString ExportController::batchText() const
{
    if (m_batchJobs.isEmpty()) {
        return {};
    }
    if (!m_batchActive) {
        return m_batchMessage;
    }
    return QStringLiteral("Export %1/%2 · %3 completed · %4")
        .arg(m_batchCompleted + 1)
        .arg(m_batchJobs.size())
        .arg(m_batchCompleted)
        .arg(progressText());
}

rendering::ExportSettings ExportController::toSettings(const QVariantMap& map) const
{
    rendering::ExportSettings s;
    const QString presetId =
        map.value(QStringLiteral("presetId"), QStringLiteral("match")).toString();
    for (const rendering::ExportPreset& preset : rendering::exportPresets()) {
        if (preset.id == presetId) {
            s = rendering::applyPreset(preset, {});
        }
    }
    s.outputPath = ProjectController::localPath(map.value(QStringLiteral("outputPath")).toString());
    if (map.contains(QStringLiteral("crf"))) {
        s.crf = map.value(QStringLiteral("crf")).toInt();
    }
    if (map.contains(QStringLiteral("speedPreset"))) {
        s.speedPreset = map.value(QStringLiteral("speedPreset")).toString();
    }
    if (map.contains(QStringLiteral("audioBitrateKbps"))) {
        s.audioBitrateKbps = map.value(QStringLiteral("audioBitrateKbps")).toInt();
    }
    if (map.contains(QStringLiteral("includeAudio"))) {
        s.includeAudio = map.value(QStringLiteral("includeAudio")).toBool();
    }
    if (map.contains(QStringLiteral("avoidUpscaling"))) {
        s.avoidUpscaling = map.value(QStringLiteral("avoidUpscaling")).toBool();
    }
    if (map.contains(QStringLiteral("frameRate"))) {
        s.frameRate = map.value(QStringLiteral("frameRate")).toString();
    }
    const auto frameValue = [&](const char* key, FrameCount fallback) -> FrameCount {
        const QVariant supplied = map.value(QLatin1String(key));
        if (!supplied.isValid()) {
            return fallback;
        }
        bool valid = false;
        const qint64 value = supplied.toString().toLongLong(&valid);
        return valid ? static_cast<FrameCount>(value) : FrameCount{-2};
    };
    s.rangeStartFrame = frameValue("rangeStartFrame", 0);
    s.rangeEndFrame = frameValue("rangeEndFrame", -1);
    return s;
}

QString ExportController::validate(const QVariantMap& settings) const
{
    const Status valid = rendering::validateExport(toSettings(settings), m_project->project(),
                                                   m_project->filePath());
    return valid ? QString() : valid.error().message;
}

QString ExportController::describeOutput(const QVariantMap& settings) const
{
    const rendering::ExportSettings s = toSettings(settings);
    const QSize size = rendering::outputSize(s, m_project->project().settings());
    const Result<FrameRate> rate = rendering::outputFrameRate(s, m_project->project().settings());
    const bool partial = s.rangeStartFrame != 0 || s.rangeEndFrame != -1;
    const QString rangeText =
        partial ? QStringLiteral(" · source frames %1–%2 (end exclusive)")
                      .arg(s.rangeStartFrame)
                      .arg(s.rangeEndFrame == -1 ? m_project->project().timeline().duration()
                                                 : s.rangeEndFrame)
                : QString();
    QString frameCountText;
    if (rate) {
        const auto bounds = rendering::resolveExportFrameBounds(
            s, m_project->project().timeline().duration(), m_project->project().settings().frameRate,
            *rate);
        if (bounds) {
            frameCountText =
                QStringLiteral(" · %1 output frames").arg(bounds->outputLength());
        }
    }
    return QStringLiteral("%1×%2 · %3 · H.264 + %4%5%6%7")
        .arg(size.width())
        .arg(size.height())
        .arg(rate ? rate->displayName() : QStringLiteral("invalid FPS"),
             s.includeAudio ? QStringLiteral("AAC %1 kbit/s").arg(s.audioBitrateKbps)
                            : QStringLiteral("no audio"),
             s.avoidUpscaling ? QStringLiteral(" · no upscaling") : QString(), rangeText,
             frameCountText);
}

Result<QVector<rendering::ExportSettings>>
ExportController::prepareBatch(const QVariantMap& settings, const QStringList& presetIds) const
{
    if (presetIds.size() < 2 || presetIds.size() > 5) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose between two and five distinct export presets."));
    }
    const rendering::ExportSettings common = toSettings(settings);
    const QFileInfo base(common.outputPath);
    if (base.fileName().isEmpty() || !base.isAbsolute() ||
        base.suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0 ||
        base.completeBaseName().isEmpty()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose an absolute .mp4 base filename for batch export."));
    }

    const QList<rendering::ExportPreset> presets = rendering::exportPresets();
    QSet<QString> usedIds;
    QSet<QString> usedPaths;
    QVector<rendering::ExportSettings> jobs;
    jobs.reserve(presetIds.size());
    for (const QString& id : presetIds) {
        if (usedIds.contains(id)) {
            return makeError(ErrorCode::Conflict,
                             QStringLiteral("The batch contains the same preset twice."));
        }
        usedIds.insert(id);
        const auto found =
            std::find_if(presets.cbegin(), presets.cend(),
                         [&id](const rendering::ExportPreset& preset) { return preset.id == id; });
        if (found == presets.cend()) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("The batch contains an unknown export preset."));
        }
        const QString target =
            QDir(base.absolutePath())
                .filePath(base.completeBaseName() + QLatin1Char('-') + id + QStringLiteral(".mp4"));
        const QString key = QDir::cleanPath(target).toCaseFolded();
        if (usedPaths.contains(key) || QFileInfo::exists(target)) {
            return makeError(
                ErrorCode::Conflict,
                QStringLiteral("Batch output already exists or repeats another output: %1")
                    .arg(QFileInfo(target).fileName()));
        }
        usedPaths.insert(key);
        rendering::ExportSettings item = rendering::applyPreset(*found, target);
        // Every variant uses the same selected program frames and delivery frame
        // rate, but independently retains its preset's quality, scaling and speed.
        item.frameRate = common.frameRate;
        item.rangeStartFrame = common.rangeStartFrame;
        item.rangeEndFrame = common.rangeEndFrame;
        item.includeAudio = common.includeAudio;
        item.protectExistingOutput = true;
        if (const Status valid =
                rendering::validateExport(item, m_project->project(), m_project->filePath());
            !valid) {
            return valid.error();
        }
        jobs.append(std::move(item));
    }
    return jobs;
}

QString ExportController::validateBatch(const QVariantMap& settings,
                                        const QStringList& presetIds) const
{
    if (busy()) {
        return QStringLiteral("Finish or cancel the current export before starting a batch.");
    }
    const auto jobs = prepareBatch(settings, presetIds);
    return jobs ? QString() : jobs.error().message;
}

bool ExportController::startBatch(const QVariantMap& settings, const QStringList& presetIds)
{
    if (busy()) {
        return false;
    }
    const auto prepared = prepareBatch(settings, presetIds);
    if (!prepared) {
        emit finished(false, prepared.error().message);
        return false;
    }
    if (!QFileInfo(m_helperProgram).isExecutable()) {
        emit finished(false,
                      QStringLiteral("The render helper is missing from this installation."));
        return false;
    }
    // Save the most recent unsaved edits before handing work to the renderer,
    // exactly as for a single export. All variants share one immutable snapshot.
    m_project->autosave();
    reset();
    m_batchJobs = *prepared;
    m_batchSnapshot = m_project->project();
    m_batchProjectFile = m_project->filePath();
    m_batchCompleted = 0;
    m_batchActive = true;
    m_batchCancelRequested = false;
    m_batchState = QStringLiteral("running");
    m_batchMessage.clear();
    emit batchChanged();
    emit stateChanged();
    return startBatchJob();
}

bool ExportController::startBatchJob()
{
    if (!m_batchActive || !m_batchSnapshot || m_batchCompleted >= m_batchJobs.size()) {
        return false;
    }
    // Never overwrite a file that appeared after batch preflight (for example,
    // another editor's export while a previous variant was rendering).
    const QString pendingPath = m_batchJobs.at(m_batchCompleted).outputPath;
    if (QFileInfo::exists(pendingPath)) {
        finishBatch(RenderJob::State::Failed,
                    QStringLiteral("Batch output appeared while rendering: %1")
                        .arg(QFileInfo(pendingPath).fileName()));
        return false;
    }
    // This method runs from the event loop rather than a RenderJob signal stack.
    // Releasing the previous completed process here cannot destroy the sender
    // while it is emitting its own finished/stateChanged signal.
    m_job = std::make_unique<RenderJob>(m_helperProgram);
    connect(m_job.get(), &RenderJob::stateChanged, this, [this](RenderJob::State state) {
        if (RenderJob::isFinished(state)) {
            m_tick.stop();
            if (state == RenderJob::State::Succeeded) {
                ++m_batchCompleted;
            }
            emit batchChanged();
            QTimer::singleShot(0, this, [this, state] {
                if (!m_batchActive) {
                    return;
                }
                if (m_batchCancelRequested) {
                    finishBatch(RenderJob::State::Cancelled);
                } else if (state != RenderJob::State::Succeeded) {
                    finishBatch(state);
                } else if (m_batchCompleted >= m_batchJobs.size()) {
                    finishBatch(RenderJob::State::Succeeded);
                } else {
                    startBatchJob();
                }
            });
        }
        emit stateChanged();
        emit progressChanged();
    });
    connect(m_job.get(), &RenderJob::progressChanged, this, &ExportController::progressChanged);
    const Status started =
        m_job->start(*m_batchSnapshot, m_batchJobs.at(m_batchCompleted), m_batchProjectFile);
    if (!started) {
        finishBatch(RenderJob::State::Failed, started.error().message);
        return false;
    }
    m_tick.start();
    emit stateChanged();
    emit progressChanged();
    emit batchChanged();
    return true;
}

void ExportController::finishBatch(RenderJob::State state, const QString& explicitError)
{
    m_tick.stop();
    const int total = m_batchJobs.size();
    const bool success = state == RenderJob::State::Succeeded && m_batchCompleted == total;
    const bool cancelled = state == RenderJob::State::Cancelled || m_batchCancelRequested;
    const QString error = !explicitError.isEmpty() ? explicitError
                          : m_job                  ? m_job->errorMessage()
                                                   : QString();
    if (success) {
        m_batchState = QStringLiteral("succeeded");
        m_batchMessage = QStringLiteral("Batch finished: %1/%2 files exported.")
                             .arg(m_batchCompleted)
                             .arg(total);
    } else if (cancelled) {
        m_batchState = QStringLiteral("cancelled");
        m_batchMessage = QStringLiteral("Batch cancelled: %1/%2 files exported.")
                             .arg(m_batchCompleted)
                             .arg(total);
    } else {
        m_batchState = QStringLiteral("failed");
        m_batchMessage = QStringLiteral("Batch stopped: %1/%2 files exported. %3")
                             .arg(m_batchCompleted)
                             .arg(total)
                             .arg(error);
    }
    m_batchSnapshot.reset();
    m_batchProjectFile.clear();
    m_batchActive = false;
    m_batchCancelRequested = false;
    emit batchChanged();
    emit stateChanged();
    emit progressChanged();
    emit finished(success, m_batchMessage);
}

bool ExportController::start(const QVariantMap& settings)
{
    if (busy()) {
        return false;
    }
    if (!m_batchJobs.isEmpty()) {
        reset();
    }
    // An export snapshots the current timeline, but the normal recovery autosave
    // may be almost a minute old. Protect the latest edits before the render
    // helper starts, including when the editor has never been saved explicitly.
    m_project->autosave();
    auto job = std::make_unique<RenderJob>(m_helperProgram);
    connect(job.get(), &RenderJob::stateChanged, this, [this](RenderJob::State state) {
        if (RenderJob::isFinished(state)) {
            m_tick.stop();
            const bool ok = state == RenderJob::State::Succeeded;
            emit finished(
                ok,
                ok ? QStringLiteral("Export finished: %1").arg(QFileInfo(outputPath()).fileName())
                : state == RenderJob::State::Cancelled ? QStringLiteral("Export cancelled.")
                                                       : errorMessage());
        }
        emit stateChanged();
        emit progressChanged();
    });
    connect(job.get(), &RenderJob::progressChanged, this, &ExportController::progressChanged);
    const Status started =
        job->start(m_project->project(), toSettings(settings), m_project->filePath());
    if (!started) {
        emit finished(false, started.error().message);
        return false;
    }
    m_job = std::move(job);
    m_tick.start();
    emit stateChanged();
    emit progressChanged();
    return true;
}

void ExportController::cancel()
{
    if (m_batchActive) {
        m_batchCancelRequested = true;
    }
    if (m_job) {
        m_job->cancel();
    }
}

void ExportController::reset()
{
    if (!busy()) {
        m_job.reset();
        m_batchJobs.clear();
        m_batchSnapshot.reset();
        m_batchProjectFile.clear();
        m_batchCompleted = 0;
        m_batchState = QStringLiteral("idle");
        m_batchMessage.clear();
        m_batchCancelRequested = false;
        emit batchChanged();
        emit stateChanged();
        emit progressChanged();
    }
}

QString ExportController::suggestedOutputPath() const
{
    QString folder = m_project->hasFilePath()
                         ? QFileInfo(m_project->filePath()).absolutePath()
                         : QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (folder.isEmpty()) {
        folder = QDir::homePath();
    }
    QString base = m_project->suggestedFileName();
    base.chop(QStringLiteral(".ftproj").size());
    return QDir(folder).filePath(base + QStringLiteral(".mp4"));
}

bool ExportController::fileExists(const QString& pathOrUrl) const
{
    return QFileInfo::exists(ProjectController::localPath(pathOrUrl));
}

bool ExportController::copyFailureDetails() const
{
    const bool batchFailure =
        !m_batchActive && !m_batchJobs.isEmpty() && m_batchState == QLatin1String("failed");
    const bool helperFailure = m_job && m_job->state() == RenderJob::State::Failed;
    if (!batchFailure && !helperFailure) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }
    // This is a local, opt-in copy only. Do not automatically transmit logs:
    // native stderr may contain source paths and user-specific filesystem data.
    QString report;
    if (batchFailure) {
        // A batch can fail in preflight between variants while the previous
        // renderer succeeded. Do not mistake its last successful job for the
        // failure, and report the *pending* destination rather than the prior one.
        const QString destination = m_batchCompleted < m_batchJobs.size()
                                        ? m_batchJobs.at(m_batchCompleted).outputPath
                                    : m_job ? m_job->outputPath()
                                            : QString();
        report = QStringLiteral("FrameTrellis batch export failure\n"
                                "Review and redact private paths before sharing.\n"
                                "Completed: %1/%2\nDestination: %3\nError: %4")
                     .arg(m_batchCompleted)
                     .arg(m_batchJobs.size())
                     .arg(destination, m_batchMessage);
        if (helperFailure) {
            report += QStringLiteral("\nEncoder: %1\nRenderer error: %2\nDetails:\n%3")
                          .arg(m_job->encoder(), m_job->errorMessage(), m_job->errorDetail());
        }
    } else {
        report = QStringLiteral("FrameTrellis export failure\n"
                                "Review and redact private paths before sharing.\n"
                                "Destination: %1\nEncoder: %2\nError: %3\nDetails:\n%4")
                     .arg(m_job->outputPath(), m_job->encoder(), m_job->errorMessage(),
                          m_job->errorDetail());
    }
    clipboard->setText(report);
    return true;
}

void ExportController::revealOutput() const
{
    if (!outputPath().isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputPath()).absolutePath()));
    }
}

} // namespace ft::ui
