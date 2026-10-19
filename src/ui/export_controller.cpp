// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/export_controller.h"

#include "ui/project_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

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
    return m_job && !RenderJob::isFinished(m_job->state()) &&
           m_job->state() != RenderJob::State::Idle;
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
                 fps > 0.0 ? QStringLiteral(" · %1 frames/s").arg(fps, 0, 'f', 1)
                           : QString());
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
    return QStringLiteral("%1×%2 · %3 · H.264 + %4%5")
        .arg(size.width())
        .arg(size.height())
        .arg(m_project->project().settings().frameRate.displayName(),
             s.includeAudio ? QStringLiteral("AAC %1 kbit/s").arg(s.audioBitrateKbps)
                            : QStringLiteral("no audio"),
             s.avoidUpscaling ? QStringLiteral(" · no upscaling") : QString());
}

bool ExportController::start(const QVariantMap& settings)
{
    if (busy()) {
        return false;
    }
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
    if (m_job) {
        m_job->cancel();
    }
}

void ExportController::reset()
{
    if (!busy()) {
        m_job.reset();
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

void ExportController::revealOutput() const
{
    if (!outputPath().isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputPath()).absolutePath()));
    }
}

} // namespace ft::ui
