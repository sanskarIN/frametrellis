// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/project_document.h"

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace ft::ui {

/// Owns the open ProjectDocument and exposes its lifecycle to QML: new/open/save, undo/redo,
/// recent projects, and periodic autosaves that are offered for recovery after a crash.
class ProjectController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(QString title READ title NOTIFY documentStateChanged)
    Q_PROPERTY(QString name READ name NOTIFY documentStateChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentStateChanged)
    Q_PROPERTY(bool hasFilePath READ hasFilePath NOTIFY documentStateChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY documentStateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QString settingsSummary READ settingsSummary NOTIFY settingsChanged)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY settingsChanged)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY settingsChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY settingsChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QVariantMap recovery READ recovery NOTIFY recoveryChanged)

public:
    explicit ProjectController(QObject* parent = nullptr);
    ~ProjectController() override;

    [[nodiscard]] ProjectDocument* document() const { return m_document.get(); }
    [[nodiscard]] const Project& project() const { return m_document->project(); }
    /// Runs an edit, reporting failures to the user. Returns true when it succeeded.
    bool execute(std::unique_ptr<EditCommand> command);

    [[nodiscard]] QString title() const;
    [[nodiscard]] QString name() const { return m_document->project().name(); }
    [[nodiscard]] QString filePath() const { return m_document->filePath(); }
    [[nodiscard]] bool hasFilePath() const { return m_document->hasFilePath(); }
    [[nodiscard]] bool isModified() const { return m_document->isModified(); }
    [[nodiscard]] bool canUndo() const { return m_document->canUndo(); }
    [[nodiscard]] bool canRedo() const { return m_document->canRedo(); }
    [[nodiscard]] QString undoText() const { return m_document->undoText(); }
    [[nodiscard]] QString redoText() const { return m_document->redoText(); }
    [[nodiscard]] QString settingsSummary() const;
    [[nodiscard]] int frameWidth() const { return project().settings().width; }
    [[nodiscard]] int frameHeight() const { return project().settings().height; }
    [[nodiscard]] double frameRate() const { return project().settings().frameRate.toDouble(); }
    [[nodiscard]] QStringList recentProjects() const { return m_recent; }
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QVariantMap recovery() const { return m_recovery; }

    Q_INVOKABLE bool newProject(int presetIndex, const QString& name);
    Q_INVOKABLE bool openProject(const QString& pathOrUrl);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QString& pathOrUrl);
    /// Rename project metadata without changing its .ftproj filename; undoable.
    Q_INVOKABLE bool renameProject(const QString& newName);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearRecentProjects();
    Q_INVOKABLE QString suggestedFileName() const;
    Q_INVOKABLE QString projectFolder() const;
    /// Opens the autosave described by recovery() as an unsaved document.
    Q_INVOKABLE bool recoverAutosave();
    Q_INVOKABLE void discardAutosave();
    /// Writes an autosave now if there are unsaved changes (also run by a timer).
    Q_INVOKABLE void autosave();
    /// Removes the autosave of the current document (on clean exit).
    void removeOwnAutosave();

    static QString localPath(const QString& pathOrUrl);
    static QString autosaveDirectory();

signals:
    void documentReplaced();
    void documentStateChanged();
    void historyChanged();
    void settingsChanged();
    void projectChanged(const ft::ChangeSet& changes);
    void recentProjectsChanged();
    void recoveryChanged();
    void errorOccurred(const QString& title, const QString& message);
    void statusMessage(const QString& message);

private:
    void adopt(std::unique_ptr<ProjectDocument> document);
    void addRecent(const QString& path);
    void loadRecent();
    void scanAutosaves();
    [[nodiscard]] QString autosavePath() const;
    void reportError(const QString& title, const Error& error);

    std::unique_ptr<ProjectDocument> m_document;
    QStringList m_recent;
    QVariantMap m_recovery;
    QTimer m_autosaveTimer;
};

} // namespace ft::ui
