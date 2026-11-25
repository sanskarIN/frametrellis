// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/project_controller.h"

#include "core/logging.h"
#include "core/timecode.h"
#include "project/project_checkpoints.h"
#include "project/project_diff.h"
#include "project/project_serializer.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace ft::ui {

namespace {
constexpr int kMaxRecent = 10;
constexpr int kAutosaveIntervalMs = 60'000;
const QString kRecentKey = QStringLiteral("project/recent");

// Quote CSV cells per RFC 4180 and neutralize leading spreadsheet formula syntax
// for untrusted names, paths and identifiers. Quotes alone do not prevent execution.
QByteArray csvCell(QString value)
{
    const QString leading = value.trimmed();
    if (!leading.isEmpty() &&
        (leading.startsWith(QLatin1Char('=')) || leading.startsWith(QLatin1Char('+')) ||
         leading.startsWith(QLatin1Char('-')) || leading.startsWith(QLatin1Char('@')))) {
        value.prepend(QLatin1Char('\''));
    }
    if (!value.isEmpty() &&
        (value.startsWith(QLatin1Char('\t')) || value.startsWith(QLatin1Char('\r')) ||
         value.startsWith(QLatin1Char('\n')))) {
        value.prepend(QLatin1Char('\''));
    }
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return (QLatin1Char('"') + value + QLatin1Char('"')).toUtf8();
}
} // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent)
{
    loadRecent();
    adopt(std::make_unique<ProjectDocument>(
        Project::createNew(QStringLiteral("Untitled"), ProjectSettings{})));
    m_autosaveTimer.setInterval(kAutosaveIntervalMs);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &ProjectController::autosave);
    m_autosaveTimer.start();
    scanAutosaves();
}

ProjectController::~ProjectController() = default;

QString ProjectController::localPath(const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    if (url.isLocalFile()) {
        return QDir::cleanPath(url.toLocalFile());
    }
    return pathOrUrl.isEmpty() ? QString()
                               : QDir::cleanPath(QFileInfo(pathOrUrl).absoluteFilePath());
}

QString ProjectController::autosaveDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/autosave");
}

QString ProjectController::autosavePath() const
{
    return QDir(autosaveDirectory()).filePath(project().id() + QStringLiteral(".ftproj"));
}

void ProjectController::adopt(std::unique_ptr<ProjectDocument> document)
{
    if (m_document) {
        removeOwnAutosave();
    }
    m_document = std::move(document);
    connect(m_document.get(), &ProjectDocument::changed, this, [this](const ChangeSet& changes) {
        if (changes.settings) {
            emit settingsChanged();
            emit documentStateChanged();
        }
        emit projectChanged(changes);
    });
    connect(m_document.get(), &ProjectDocument::historyChanged, this,
            &ProjectController::historyChanged);
    connect(m_document.get(), &ProjectDocument::modifiedChanged, this,
            &ProjectController::documentStateChanged);
    connect(m_document.get(), &ProjectDocument::filePathChanged, this,
            &ProjectController::documentStateChanged);
    emit documentReplaced();
    emit documentStateChanged();
    emit historyChanged();
    emit settingsChanged();
    emit projectChanged(ChangeSet::everything());
}

bool ProjectController::execute(std::unique_ptr<EditCommand> command)
{
    const QString text = command ? command->text() : QString();
    const Status status = m_document->execute(std::move(command));
    if (!status) {
        emit statusMessage(status.error().message);
        qCInfo(lcUi) << "Edit refused:" << text << status.error().toString();
        return false;
    }
    return true;
}

QString ProjectController::title() const
{
    return m_document->displayName() +
           (m_document->isModified() ? QStringLiteral(" •") : QString());
}

QString ProjectController::settingsSummary() const
{
    return project().settings().summary();
}

QVariantList ProjectController::presets() const
{
    QVariantList list;
    for (const ProjectPreset& preset : projectPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), preset.name},
                                {QStringLiteral("summary"), preset.settings.summary()}});
    }
    return list;
}

bool ProjectController::newProject(int presetIndex, const QString& name)
{
    const QList<ProjectPreset> presets = projectPresets();
    const ProjectSettings settings = presetIndex >= 0 && presetIndex < presets.size()
                                         ? presets.at(presetIndex).settings
                                         : ProjectSettings{};
    adopt(std::make_unique<ProjectDocument>(Project::createNew(name, settings)));
    emit statusMessage(QStringLiteral("Created a new project (%1).").arg(settings.summary()));
    return true;
}

bool ProjectController::openProject(const QString& pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    Result<std::unique_ptr<ProjectDocument>> loaded = ProjectDocument::load(path);
    if (!loaded) {
        reportError(QStringLiteral("Could not open the project"), loaded.error());
        if (loaded.error().code == ErrorCode::NotFound) {
            m_recent.removeAll(path);
            QSettings().setValue(kRecentKey, m_recent);
            emit recentProjectsChanged();
        }
        return false;
    }
    adopt(std::move(loaded).value());
    addRecent(path);
    int missing = 0;
    for (const MediaAsset& asset : project().media()) {
        missing += asset.status == MediaStatus::Missing ? 1 : 0;
    }
    if (missing > 0) {
        emit errorOccurred(
            QStringLiteral("Some media files are missing"),
            QStringLiteral(
                "%1 media file(s) used by this project could not be found. They are marked "
                "as offline in the media bin; use \"Relink\" to locate them. Offline clips "
                "show a red placeholder and the project cannot be exported until they are "
                "relinked.")
                .arg(missing));
    } else {
        emit statusMessage(QStringLiteral("Opened %1.").arg(QFileInfo(path).fileName()));
    }
    return true;
}

bool ProjectController::renameProject(const QString& newName)
{
    const QString name = newName.trimmed();
    if (name.isEmpty() || name.size() > 120) {
        emit statusMessage(QStringLiteral("Choose a project name between 1 and 120 characters."));
        return false;
    }
    if (name == project().name()) {
        return true; // No change or Undo entry for renaming to the current name.
    }
    if (!execute(std::make_unique<ProjectSettingsCommand>(project().settings(), name,
                                                          QStringLiteral("Rename Project")))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Renamed project to %1.").arg(name));
    return true;
}

QString ProjectController::checkpointDirectory()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return root.isEmpty() ? QString() : QDir(root).filePath(QStringLiteral("checkpoints"));
}

QVariantMap ProjectController::checkpointHistory() const
{
    const auto history = checkpoints::Store(checkpointDirectory()).list(project().id());
    if (!history) {
        return {{QStringLiteral("error"), history.error().toString()}};
    }
    QVariantList entries;
    for (const checkpoints::Entry& entry : history->entries) {
        entries.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("projectName"), entry.projectName},
            {QStringLiteral("originalPath"), entry.originalPath},
            {QStringLiteral("createdAt"), entry.createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("displayDate"), QLocale().toString(entry.createdAt.toLocalTime(),
                                                               QLocale::ShortFormat)},
            {QStringLiteral("bytes"), entry.bytes},
            {QStringLiteral("readable"), entry.readable},
            {QStringLiteral("error"), entry.error},
        });
    }
    return {{QStringLiteral("projectId"), project().id()},
            {QStringLiteral("entries"), entries},
            {QStringLiteral("limit"), checkpoints::kMaxCheckpointsPerProject},
            {QStringLiteral("truncated"), history->truncated}};
}

bool ProjectController::createCheckpoint(const QString& label)
{
    const auto saved =
        checkpoints::Store(checkpointDirectory()).create(project(), label, m_document->filePath());
    if (!saved) {
        reportError(QStringLiteral("Could not create checkpoint"), saved.error());
        return false;
    }
    emit statusMessage(QStringLiteral("Created checkpoint: %1.").arg(saved->label));
    return true;
}

QVariantMap ProjectController::compareCheckpoint(const QString& projectId,
                                                 const QString& checkpointId) const
{
    if (projectId != project().id()) {
        return {{QStringLiteral("error"), QStringLiteral("The current project has changed.")}};
    }
    const auto previous =
        checkpoints::Store(checkpointDirectory()).restore(projectId, checkpointId);
    if (!previous) {
        return {{QStringLiteral("error"), previous.error().toString()}};
    }
    const auto report = project_diff::compare(*previous, project());
    if (!report) {
        return {{QStringLiteral("error"), report.error().toString()}};
    }
    return project_diff::toJson(*report).toVariantMap();
}

bool ProjectController::restoreCheckpoint(const QString& projectId, const QString& checkpointId)
{
    // The confirmation dialog may have been open while the project was
    // replaced. Never apply a stale checkpoint selection to a different ID.
    if (projectId != project().id()) {
        emit errorOccurred(QStringLiteral("Could not open checkpoint"),
                           QStringLiteral("The current project has changed."));
        return false;
    }
    auto restored = checkpoints::Store(checkpointDirectory()).restore(projectId, checkpointId);
    if (!restored) {
        reportError(QStringLiteral("Could not open checkpoint"), restored.error());
        return false;
    }
    auto document = std::make_unique<ProjectDocument>(std::move(restored).value());
    document->refreshMediaStatus();
    document->markModified();
    adopt(std::move(document));
    // Preserve the restored state on a second crash. The immutable checkpoint
    // remains available even if the normal recovery autosave cannot be written.
    autosave();
    emit statusMessage(QStringLiteral("Opened checkpoint as unsaved work. Save As to keep it."));
    return true;
}

bool ProjectController::removeCheckpoint(const QString& projectId, const QString& checkpointId)
{
    if (projectId != project().id()) {
        emit errorOccurred(QStringLiteral("Could not remove checkpoint"),
                           QStringLiteral("The current project has changed."));
        return false;
    }
    const Status removed = checkpoints::Store(checkpointDirectory()).remove(projectId, checkpointId);
    if (!removed) {
        reportError(QStringLiteral("Could not remove checkpoint"), removed.error());
        return false;
    }
    emit statusMessage(QStringLiteral("Removed checkpoint."));
    return true;
}

bool ProjectController::save()
{
    if (!m_document->hasFilePath()) {
        return false;
    }
    return saveAs(m_document->filePath());
}

bool ProjectController::saveAs(const QString& pathOrUrl)
{
    QString path = localPath(pathOrUrl);
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().compare(QLatin1String(project_format::kFileSuffix),
                                         Qt::CaseInsensitive) != 0) {
        path += QLatin1Char('.') + QLatin1String(project_format::kFileSuffix);
    }
    if (const Status saved = m_document->saveAs(path); !saved) {
        reportError(QStringLiteral("Could not save the project"), saved.error());
        return false;
    }
    removeOwnAutosave();
    addRecent(path);
    emit statusMessage(QStringLiteral("Saved %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool ProjectController::exportClipListCsv(const QString& pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    if (path.trimmed().isEmpty() ||
        QFileInfo(path).suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) != 0) {
        emit errorOccurred(QStringLiteral("Could not export the clip list"),
                           QStringLiteral("Choose a filename ending in .csv."));
        return false;
    }

    const QString destination = canonicalMediaKey(path);
    if ((m_document->hasFilePath() && destination == canonicalMediaKey(m_document->filePath())) ||
        project().assetByPath(path)) {
        emit errorOccurred(QStringLiteral("Could not export the clip list"),
                           QStringLiteral("The report cannot overwrite project or source media."));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QStringLiteral("Could not export the clip list"), file.errorString());
        return false;
    }

    // UTF-8 BOM helps spreadsheet programs recognize names from non-Latin scripts.
    const QByteArray header =
        "track,track_type,clip_id,source_name,source_path,"
        "start_frame,end_frame_exclusive,duration_frames,source_in_frame,"
        "source_out_frame_exclusive,start_timecode,end_timecode_exclusive\r\n";
    if (file.write("\xEF\xBB\xBF", 3) != 3 || file.write(header) != header.size()) {
        file.cancelWriting();
        emit errorOccurred(QStringLiteral("Could not export the clip list"),
                           QStringLiteral("Could not write the report header."));
        return false;
    }

    const auto rate = project().settings().frameRate;
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            const MediaAsset* source = project().asset(clip.assetId);
            const QStringList columns{track.name,
                                      track.kind == TrackKind::Video ? QStringLiteral("video")
                                                                     : QStringLiteral("audio"),
                                      clip.id,
                                      source ? source->name : QString(),
                                      source ? source->filePath : QString(),
                                      QString::number(clip.start),
                                      QString::number(clip.end()),
                                      QString::number(clip.duration),
                                      QString::number(clip.sourceIn),
                                      QString::number(clip.sourceOut()),
                                      timecode::format(clip.start, rate),
                                      timecode::format(clip.end(), rate)};
            QByteArray row;
            for (qsizetype i = 0; i < columns.size(); ++i) {
                if (i != 0) {
                    row.append(',');
                }
                row.append(csvCell(columns.at(i)));
            }
            row.append("\r\n");
            if (file.write(row) != row.size()) {
                file.cancelWriting();
                emit errorOccurred(QStringLiteral("Could not export the clip list"),
                                   QStringLiteral("Could not write the clip list."));
                return false;
            }
        }
    }
    if (!file.commit()) {
        emit errorOccurred(QStringLiteral("Could not export the clip list"), file.errorString());
        return false;
    }
    emit statusMessage(QStringLiteral("Saved timeline clip list as CSV."));
    return true;
}

void ProjectController::undo()
{
    if (const Status done = m_document->undo(); !done) {
        emit statusMessage(done.error().message);
    }
}

void ProjectController::redo()
{
    if (const Status done = m_document->redo(); !done) {
        emit statusMessage(done.error().message);
    }
}

QString ProjectController::suggestedFileName() const
{
    QString base = project().name().trimmed();
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : forbidden) {
        base.replace(c, QLatin1Char('_'));
    }
    if (base.isEmpty()) {
        base = QStringLiteral("Untitled");
    }
    return base + QLatin1Char('.') + QLatin1String(project_format::kFileSuffix);
}

QString ProjectController::projectFolder() const
{
    if (m_document->hasFilePath()) {
        return QFileInfo(m_document->filePath()).absolutePath();
    }
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
}

void ProjectController::addRecent(const QString& path)
{
    m_recent.removeAll(path);
    m_recent.prepend(path);
    while (m_recent.size() > kMaxRecent) {
        m_recent.removeLast();
    }
    QSettings().setValue(kRecentKey, m_recent);
    emit recentProjectsChanged();
}

void ProjectController::loadRecent()
{
    m_recent = QSettings().value(kRecentKey).toStringList();
    m_recent.erase(
        std::remove_if(m_recent.begin(), m_recent.end(),
                       [](const QString& p) { return p.isEmpty() || !QFileInfo::exists(p); }),
        m_recent.end());
}

void ProjectController::clearRecentProjects()
{
    m_recent.clear();
    QSettings().remove(kRecentKey);
    emit recentProjectsChanged();
}

void ProjectController::autosave()
{
    if (!m_document || !m_document->isModified()) {
        return;
    }
    QDir().mkpath(autosaveDirectory());
    if (const Status saved = m_document->saveCopy(autosavePath()); !saved) {
        qCWarning(lcUi) << "Autosave failed:" << saved.error().toString();
        return;
    }
    QJsonObject meta{
        {QStringLiteral("originalPath"), m_document->filePath()},
        {QStringLiteral("name"), project().name()},
        {QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QSaveFile file(autosavePath() + QStringLiteral(".json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(meta).toJson());
        file.commit();
    }
    qCDebug(lcUi) << "Autosaved to" << autosavePath();
}

void ProjectController::removeOwnAutosave()
{
    if (!m_document) {
        return;
    }
    QFile::remove(autosavePath());
    QFile::remove(autosavePath() + QStringLiteral(".json"));
}

void ProjectController::scanAutosaves()
{
    m_recovery.clear();
    QDirIterator it(autosaveDirectory(), {QStringLiteral("*.ftproj")}, QDir::Files);
    QDateTime newest;
    while (it.hasNext()) {
        const QString path = it.next();
        if (path == autosavePath()) {
            continue;
        }
        QFile metaFile(path + QStringLiteral(".json"));
        QJsonObject meta;
        if (metaFile.open(QIODevice::ReadOnly)) {
            meta = QJsonDocument::fromJson(metaFile.read(qint64{64} * 1024)).object();
        }
        const QDateTime savedAt = QFileInfo(path).lastModified();
        if (!newest.isValid() || savedAt > newest) {
            newest = savedAt;
            m_recovery = QVariantMap{
                {QStringLiteral("autosavePath"), path},
                {QStringLiteral("originalPath"),
                 meta.value(QStringLiteral("originalPath")).toString()},
                {QStringLiteral("name"), meta.value(QStringLiteral("name")).toString()},
                {QStringLiteral("savedAt"), QLocale().toString(savedAt, QLocale::ShortFormat)}};
        }
    }
    emit recoveryChanged();
}

bool ProjectController::recoverAutosave()
{
    const QString autosaveFile = m_recovery.value(QStringLiteral("autosavePath")).toString();
    if (autosaveFile.isEmpty()) {
        return false;
    }
    Result<Project> project = project_format::loadFromFile(autosaveFile);
    if (!project) {
        reportError(QStringLiteral("The recovered project could not be opened"), project.error());
        return false;
    }
    // The recovered document starts unsaved: "Save" asks for a file name and "Save As" may
    // overwrite the original once the user has checked the recovered edit.
    auto document = std::make_unique<ProjectDocument>(std::move(project).value());
    document->refreshMediaStatus();
    document->markModified();
    adopt(std::move(document));
    // A recovered document normally retains the same project ID and therefore the
    // *same* autosave path. Do not delete that file before the user saves: a second
    // crash immediately after recovery would otherwise lose the only backup.
    // saveCopy() uses QSaveFile, so an unsuccessful refresh leaves the old copy intact.
    const QString refreshedPath = autosavePath();
    autosave();
    // If a backup was renamed or moved, only remove it once a replacement exists.
    if (QDir::cleanPath(refreshedPath) != QDir::cleanPath(autosaveFile) &&
        QFileInfo::exists(refreshedPath)) {
        QFile::remove(autosaveFile);
        QFile::remove(autosaveFile + QStringLiteral(".json"));
    }
    m_recovery.clear();
    emit recoveryChanged();
    emit statusMessage(QStringLiteral("Recovered unsaved work. Save it to keep it."));
    return true;
}

void ProjectController::discardAutosave()
{
    const QString autosaveFile = m_recovery.value(QStringLiteral("autosavePath")).toString();
    if (!autosaveFile.isEmpty()) {
        QFile::remove(autosaveFile);
        QFile::remove(autosaveFile + QStringLiteral(".json"));
    }
    scanAutosaves();
}

void ProjectController::reportError(const QString& title, const Error& error)
{
    QString message = error.message;
    if (!error.detail.isEmpty()) {
        message += QStringLiteral("\n\nDetails: ") + error.detail;
    }
    qCWarning(lcUi).noquote() << title << "-" << error.toString();
    emit errorOccurred(title, message);
}

} // namespace ft::ui
