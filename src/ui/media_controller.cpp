// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/media_controller.h"

#include "core/logging.h"
#include "media/media_probe.h"
#include "ui/project_controller.h"
#include "ui/title_image.h"

#include "core/ids.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

namespace ft::ui {

MediaController::MediaController(ProjectController* project,
                                 std::shared_ptr<ThumbnailRegistry> registry, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_registry(std::move(registry))
{
    connect(&m_importer, &media::MediaImporter::finished, this, &MediaController::onImported);
    connect(&m_importer, &media::MediaImporter::pendingCountChanged, this,
            &MediaController::importingChanged);
    connect(m_project, &ProjectController::documentReplaced, this,
            [this] { m_importer.cancelAll(); });
    connect(m_project, &ProjectController::projectChanged, this, [this](const ChangeSet& changes) {
        if (changes.media || changes.trackStructure || !changes.tracks.isEmpty()) {
            refresh();
        }
    });
    refresh();
}

QStringList MediaController::nameFilters() const
{
    QStringList patterns;
    for (const QString& suffix : media::importFileSuffixes()) {
        patterns << QStringLiteral("*.") + suffix;
    }
    return {QStringLiteral("Media files (%1)").arg(patterns.join(QLatin1Char(' '))),
            QStringLiteral("All files (*)")};
}

void MediaController::refresh()
{
    m_registry->update(m_project->project().media());
    m_model.refresh(m_project->project());
}

void MediaController::importFiles(const QVariantList& urls)
{
    QStringList paths;
    for (const QVariant& value : urls) {
        const QString text =
            value.typeId() == QMetaType::QUrl ? value.toUrl().toString() : value.toString();
        const QString path = ProjectController::localPath(text);
        if (!path.isEmpty() && !paths.contains(path)) {
            paths << path;
        }
    }
    QStringList fresh;
    int duplicates = 0;
    for (const QString& path : std::as_const(paths)) {
        if (m_project->project().assetByPath(path)) {
            ++duplicates;
        } else {
            fresh << path;
        }
    }
    if (duplicates > 0) {
        emit statusMessage(
            duplicates == 1
                ? QStringLiteral("That file is already in the media bin.")
                : QStringLiteral("%1 files are already in the media bin.").arg(duplicates));
    }
    if (!fresh.isEmpty()) {
        emit statusMessage(QStringLiteral("Importing %1 file(s)…").arg(fresh.size()));
        m_importer.import(fresh);
    }
}

void MediaController::onImported(const std::vector<MediaAsset>& assets,
                                 const QList<media::MediaImporter::Failure>& failures)
{
    std::vector<MediaAsset> fresh;
    for (const MediaAsset& asset : assets) {
        // Another import may have added the same file while this batch was probing.
        if (!m_project->project().assetByPath(asset.filePath)) {
            fresh.push_back(asset);
        }
    }
    if (!fresh.empty()) {
        const int count = static_cast<int>(fresh.size());
        if (m_project->execute(std::make_unique<AddMediaCommand>(std::move(fresh)))) {
            emit imported(count);
            emit statusMessage(QStringLiteral("Imported %1 file(s).").arg(count));
        }
    }
    if (!failures.isEmpty()) {
        QStringList lines;
        for (const auto& failure : failures) {
            lines << QStringLiteral("• %1").arg(failure.message);
        }
        emit importProblems(
            failures.size() == 1
                ? QStringLiteral("A file could not be imported")
                : QStringLiteral("%1 files could not be imported").arg(failures.size()),
            lines.join(QLatin1Char('\n')) +
                QStringLiteral("\n\nFrameTrellis supports common video, audio and image formats "
                               "that FFmpeg can decode. The files were not changed."));
    }
}

QString MediaController::createTitle(const QString& text, int fontPixels, const QString& color)
{
    if (!m_project->hasFilePath()) {
        emit importProblems(QStringLiteral("Save your project first"),
                            QStringLiteral("Save the project before creating a title, so its "
                                           "image can be kept beside the project file."));
        return {};
    }
    const Result<QImage> rendered =
        makeTitleImage(text, QSize(m_project->frameWidth(), m_project->frameHeight()),
                       fontPixels, QColor(color));
    if (!rendered) {
        emit importProblems(QStringLiteral("Could not create title"), rendered.error().message);
        return {};
    }
    QDir projectDir(m_project->projectFolder());
    const QString folder = QStringLiteral("FrameTrellis Titles");
    if (!projectDir.mkpath(folder)) {
        emit importProblems(QStringLiteral("Could not create title"),
                            QStringLiteral("Could not create the title image folder."));
        return {};
    }
    const QString path =
        projectDir.filePath(folder + QStringLiteral("/title-") + createId() + QStringLiteral(".png"));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        emit importProblems(QStringLiteral("Could not create title"), output.errorString());
        return {};
    }
    if (!rendered->save(&output, "PNG") || !output.commit()) {
        output.cancelWriting();
        emit importProblems(QStringLiteral("Could not create title"),
                            QStringLiteral("The PNG image could not be saved."));
        return {};
    }
    Result<MediaAsset> asset = media::createAsset(path);
    if (!asset) {
        QFile::remove(path);
        emit importProblems(QStringLiteral("Could not import title"), asset.error().message);
        return {};
    }
    const Id id = asset->id;
    std::vector<MediaAsset> items;
    items.push_back(std::move(asset).value());
    if (!m_project->execute(std::make_unique<AddMediaCommand>(std::move(items)))) {
        QFile::remove(path);
        return {};
    }
    emit imported(1);
    emit statusMessage(QStringLiteral("Created a title image alongside the project."));
    return id;
}

void MediaController::remove(const QString& assetId)
{
    const MediaAsset* asset = m_project->project().asset(assetId);
    if (!asset) {
        return;
    }
    const QString name = asset->name;
    if (m_project->execute(std::make_unique<RemoveMediaCommand>(assetId))) {
        emit statusMessage(
            QStringLiteral("Removed %1 from the project (the file on disk was not touched).")
                .arg(name));
    }
}

bool MediaController::relink(const QString& assetId, const QString& urlOrPath)
{
    const MediaAsset* current = m_project->project().asset(assetId);
    if (!current) {
        return false;
    }
    const QString path = ProjectController::localPath(urlOrPath);
    Result<MediaInfo> info = media::probeMedia(path);
    if (!info) {
        emit importProblems(QStringLiteral("Could not relink the media"), info.error().message);
        return false;
    }
    if (info->kind != current->info.kind) {
        emit importProblems(QStringLiteral("Could not relink the media"),
                            QStringLiteral("The replacement must be the same kind of media (%1).")
                                .arg(mediaKindToString(current->info.kind)));
        return false;
    }
    MediaAsset updated = *current;
    updated.filePath = path;
    updated.info = std::move(info).value();
    updated.status = MediaStatus::Online;
    if (!m_project->execute(
            std::make_unique<UpdateMediaCommand>(updated, QStringLiteral("Relink Media")))) {
        emit importProblems(
            QStringLiteral("Could not relink the media"),
            QStringLiteral("The replacement file is shorter than the clips that use it, or lacks "
                           "the audio or video they need."));
        return false;
    }
    emit statusMessage(QStringLiteral("Relinked %1.").arg(QFileInfo(path).fileName()));
    return true;
}

void MediaController::revealInFolder(const QString& assetId) const
{
    if (const MediaAsset* asset = m_project->project().asset(assetId)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(asset->filePath).absolutePath()));
    }
}

void MediaController::rescan()
{
    const QList<Id> missing = m_project->document()->refreshMediaStatus();
    emit statusMessage(missing.isEmpty()
                           ? QStringLiteral("All media files are available.")
                           : QStringLiteral("%1 media file(s) are offline.").arg(missing.size()));
}

} // namespace ft::ui
