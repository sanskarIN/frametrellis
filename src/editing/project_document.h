// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/command_history.h"

#include <QObject>
#include <QString>

#include <memory>

namespace ft {

/// An open project: the Project value, its undo history and its file on disk.
/// All modifications go through execute()/undo()/redo() so that every change is undoable and
/// announced through changed().
class ProjectDocument : public QObject
{
    Q_OBJECT

public:
    explicit ProjectDocument(Project project, QObject* parent = nullptr);
    ~ProjectDocument() override;

    static Result<std::unique_ptr<ProjectDocument>> load(const QString& filePath);

    [[nodiscard]] const Project& project() const { return m_project; }

    Status execute(std::unique_ptr<EditCommand> command);
    Status undo();
    Status redo();
    void breakMerge() { m_history.breakMerge(); }
    [[nodiscard]] bool canUndo() const { return m_history.canUndo(); }
    [[nodiscard]] bool canRedo() const { return m_history.canRedo(); }
    [[nodiscard]] QString undoText() const { return m_history.undoText(); }
    [[nodiscard]] QString redoText() const { return m_history.redoText(); }

    [[nodiscard]] bool isModified() const { return !m_history.isClean(); }
    [[nodiscard]] const QString& filePath() const { return m_filePath; }
    [[nodiscard]] bool hasFilePath() const { return !m_filePath.isEmpty(); }
    /// File name without suffix, or the project name for unsaved documents.
    [[nodiscard]] QString displayName() const;

    /// Saves to the current path. Requires hasFilePath().
    Status save();
    /// Saves to a new path and adopts it as the document's path.
    Status saveAs(const QString& filePath);
    /// Writes a copy (for example, an autosave or a render snapshot) without changing the
    /// document's path or modified state.
    Status saveCopy(const QString& filePath) const;

    /// Re-checks whether every media file is still present. Returns the ids of missing assets.
    QList<Id> refreshMediaStatus();

signals:
    void changed(const ft::ChangeSet& changes);
    void historyChanged();
    void modifiedChanged(bool modified);
    void filePathChanged();

private:
    void afterChange(const ChangeSet& changes, bool wasModified);

    Project m_project;
    CommandHistory m_history;
    QString m_filePath;
};

} // namespace ft
