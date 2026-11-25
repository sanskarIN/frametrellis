// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/project_bundle_controller.h"

#include "ui/project_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <mutex>
#include <utility>

namespace ft::ui {

struct ProjectBundleController::WorkState
{
    std::atomic_bool cancelled{false};
    std::mutex mutex;
    project_bundle::Progress progress;
};

namespace {

QString phaseName(project_bundle::Phase phase)
{
    switch (phase) {
    case project_bundle::Phase::Planning:
        return QStringLiteral("planning");
    case project_bundle::Phase::Copying:
        return QStringLiteral("copying");
    case project_bundle::Phase::Finalizing:
        return QStringLiteral("finalizing");
    }
    return {};
}

QString displayBytes(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1);
}

bool validFolderName(const QString& name)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    return allowed.match(name).hasMatch() && !reserved.match(name).hasMatch();
}

} // namespace

ProjectBundleController::ProjectBundleController(ProjectController* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &ProjectBundleController::poll);
    connect(&m_watcher, &QFutureWatcherBase::finished, this, &ProjectBundleController::finished);
}

ProjectBundleController::~ProjectBundleController()
{
    if (m_work) {
        m_work->cancelled.store(true);
    }
    m_timer.stop();
    m_watcher.waitForFinished();
}

bool ProjectBundleController::reject(const QString& message)
{
    m_result = {{QStringLiteral("success"), false}, {QStringLiteral("message"), message}};
    emit stateChanged();
    return false;
}

bool ProjectBundleController::start(Work work, const QString& operation)
{
    if (m_busy) {
        return false;
    }
    m_work = std::make_shared<WorkState>();
    m_operation = operation;
    m_busy = true;
    m_cancelling = false;
    m_result.clear();
    m_progress.clear();
    const auto state = m_work;
    m_watcher.setFuture(QtConcurrent::run([state, work = std::move(work)] {
        return work(state);
    }));
    m_timer.start();
    emit stateChanged();
    poll();
    return true;
}

bool ProjectBundleController::createBundle(const QString& parentPath, const QString& folderName,
                                           bool usedOnly)
{
    if (m_busy) {
        return false;
    }
    const QString path = ProjectController::localPath(parentPath);
    if (!validFolderName(folderName) || !QFileInfo(path).isAbsolute() ||
        !QFileInfo(path).isDir()) {
        return reject(QStringLiteral("Choose an existing parent folder and a new folder name "
                                     "using 1–80 letters, digits, hyphens or underscores."));
    }
    const QString destination = QDir::cleanPath(QDir(path).filePath(folderName));
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
        return reject(QStringLiteral("The bundle folder already exists."));
    }

    // The worker owns a value snapshot and never refers to live QObjects.
    const Project snapshot = m_project->project();
    return start(
        [snapshot, destination, usedOnly](const std::shared_ptr<WorkState>& state)
            -> Result<QVariantMap> {
            const auto bundle = project_bundle::create(
                snapshot, destination, project_bundle::Options{usedOnly},
                [state] { return state->cancelled.load(); },
                [state](const project_bundle::Progress& progress) {
                    const std::lock_guard guard(state->mutex);
                    state->progress = progress;
                });
            if (!bundle) {
                return bundle.error();
            }
            return QVariantMap{{QStringLiteral("directory"), bundle->directory},
                               {QStringLiteral("projectFile"), bundle->projectFile},
                               {QStringLiteral("files"), bundle->files},
                               {QStringLiteral("size"), displayBytes(bundle->bytes)},
                               {QStringLiteral("message"),
                                QStringLiteral("Created independent portable project.")}};
        },
        QStringLiteral("create"));
}

bool ProjectBundleController::verifyBundle(const QString& path)
{
    if (m_busy) {
        return false;
    }
    const QString directory = ProjectController::localPath(path);
    if (!QFileInfo(directory).isAbsolute() || !QFileInfo(directory).isDir()) {
        return reject(QStringLiteral("Choose an existing portable project folder."));
    }
    return start(
        [directory](const std::shared_ptr<WorkState>& state) -> Result<QVariantMap> {
            const auto verified =
                project_bundle::verify(directory, [state] { return state->cancelled.load(); });
            if (!verified) {
                return verified.error();
            }
            return QVariantMap{
                {QStringLiteral("directory"), directory},
                {QStringLiteral("files"), verified->files},
                {QStringLiteral("size"), displayBytes(verified->bytes)},
                {QStringLiteral("matches"), verified->problems.isEmpty()},
                {QStringLiteral("projectMatches"), verified->projectMatches},
                {QStringLiteral("problems"), verified->problems},
                {QStringLiteral("message"), verified->problems.isEmpty()
                                                ? QStringLiteral("All recorded checksums match.")
                                                : QStringLiteral("Some recorded files differ.")}};
        },
        QStringLiteral("verify"));
}

void ProjectBundleController::cancel()
{
    if (!m_busy || !m_work || m_cancelling) {
        return;
    }
    m_work->cancelled.store(true);
    m_cancelling = true;
    emit stateChanged();
}

void ProjectBundleController::poll()
{
    if (!m_work) {
        return;
    }
    project_bundle::Progress progress;
    {
        const std::lock_guard guard(m_work->mutex);
        progress = m_work->progress;
    }
    const double fraction =
        progress.totalBytes > 0
            ? static_cast<double>(progress.bytesCopied) / static_cast<double>(progress.totalBytes)
            : (progress.totalFiles > 0 ? static_cast<double>(progress.filesCompleted) /
                                            static_cast<double>(progress.totalFiles)
                                      : 0.0);
    const QVariantMap next{
        {QStringLiteral("operation"), m_operation},
        {QStringLiteral("phase"), phaseName(progress.phase)},
        {QStringLiteral("fraction"), fraction},
        {QStringLiteral("filesCompleted"), progress.filesCompleted},
        {QStringLiteral("totalFiles"), progress.totalFiles},
        {QStringLiteral("copied"), displayBytes(progress.bytesCopied)},
        {QStringLiteral("total"), displayBytes(progress.totalBytes)},
    };
    if (next != m_progress) {
        m_progress = next;
        emit progressChanged();
    }
}

void ProjectBundleController::finished()
{
    m_timer.stop();
    poll();
    const auto outcome = m_watcher.result();
    m_result = outcome ? *outcome
                       : QVariantMap{{QStringLiteral("message"), outcome.error().toString()},
                                     {QStringLiteral("cancelled"),
                                      outcome.error().code == ErrorCode::Cancelled}};
    m_result[QStringLiteral("operation")] = m_operation;
    m_result[QStringLiteral("success")] = outcome.ok();
    m_work.reset();
    m_busy = false;
    m_cancelling = false;
    emit stateChanged();
}

} // namespace ft::ui
