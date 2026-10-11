// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"

#include "core/logging.h"
#include "project/project_serializer.h"

#include <QFileInfo>

namespace ft {

ProjectDocument::ProjectDocument(Project project, QObject* parent)
    : QObject(parent)
    , m_project(std::move(project))
{
}

ProjectDocument::~ProjectDocument() = default;

Result<std::unique_ptr<ProjectDocument>> ProjectDocument::load(const QString& filePath)
{
    Result<Project> project = project_format::loadFromFile(filePath);
    if (!project) {
        return project.error();
    }
    auto document = std::make_unique<ProjectDocument>(std::move(project).value());
    document->m_filePath = QFileInfo(filePath).absoluteFilePath();
    document->refreshMediaStatus();
    return document;
}

void ProjectDocument::afterChange(const ChangeSet& changes, bool wasModified)
{
    if (!changes.isEmpty()) {
        emit changed(changes);
    }
    emit historyChanged();
    if (wasModified != isModified()) {
        emit modifiedChanged(isModified());
    }
}

Status ProjectDocument::execute(std::unique_ptr<EditCommand> command)
{
    const bool wasModified = isModified();
    Result<ChangeSet> result = m_history.execute(std::move(command), m_project);
    if (!result) {
        return result.error();
    }
    afterChange(*result, wasModified);
    return {};
}

Status ProjectDocument::undo()
{
    const bool wasModified = isModified();
    Result<ChangeSet> result = m_history.undo(m_project);
    if (!result) {
        return result.error();
    }
    afterChange(*result, wasModified);
    return {};
}

Status ProjectDocument::redo()
{
    const bool wasModified = isModified();
    Result<ChangeSet> result = m_history.redo(m_project);
    if (!result) {
        return result.error();
    }
    afterChange(*result, wasModified);
    return {};
}

QString ProjectDocument::displayName() const
{
    return m_filePath.isEmpty() ? m_project.name() : QFileInfo(m_filePath).completeBaseName();
}

Status ProjectDocument::save()
{
    if (m_filePath.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Choose a file name first."));
    }
    return saveAs(m_filePath);
}

Status ProjectDocument::saveAs(const QString& filePath)
{
    const QString absolute = QFileInfo(filePath).absoluteFilePath();
    if (const Status saved = project_format::saveToFile(m_project, absolute); !saved) {
        return saved;
    }
    const bool wasModified = isModified();
    const bool pathChanged = absolute != m_filePath;
    m_filePath = absolute;
    m_history.setClean();
    if (pathChanged) {
        emit filePathChanged();
    }
    if (wasModified) {
        emit modifiedChanged(false);
    }
    emit historyChanged();
    return {};
}

Status ProjectDocument::saveCopy(const QString& filePath) const
{
    return project_format::saveToFile(m_project, filePath);
}

QList<Id> ProjectDocument::refreshMediaStatus()
{
    QList<Id> missing;
    bool anyChanged = false;
    for (const MediaAsset& asset : m_project.media()) {
        const QFileInfo info(asset.filePath);
        MediaStatus status = MediaStatus::Online;
        if (!info.exists() || !info.isFile()) {
            status = MediaStatus::Missing;
            missing.append(asset.id);
        } else if (asset.info.fileSize > 0 && info.size() != asset.info.fileSize) {
            status = MediaStatus::Changed;
        }
        if (status != asset.status) {
            m_project.setAssetStatus(asset.id, status);
            anyChanged = true;
        }
    }
    if (!missing.isEmpty()) {
        qCWarning(lcProject) << missing.size() << "media file(s) are missing";
    }
    if (anyChanged) {
        ChangeSet changes;
        changes.media = true;
        emit changed(changes);
    }
    return missing;
}

} // namespace ft
