// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "editing/change_set.h"
#include "project/project.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ft {

/// An undoable modification of a Project.
///
/// redo() is first called by the history to perform the edit; it may fail, in which case the
/// project must be left unchanged and the command is discarded. After a successful first redo,
/// undo() and redo() must always succeed and restore exactly the same states.
class EditCommand
{
public:
    virtual ~EditCommand() = default;
    EditCommand() = default;
    EditCommand(const EditCommand&) = delete;
    EditCommand& operator=(const EditCommand&) = delete;

    [[nodiscard]] virtual QString text() const = 0;
    virtual Result<ChangeSet> redo(Project& project) = 0;
    virtual ChangeSet undo(Project& project) = 0;

    /// Commands with the same non-empty merge key that follow each other (for example, the
    /// steps of a slider drag) are merged into a single undo step.
    [[nodiscard]] virtual QString mergeKey() const { return {}; }
    virtual bool mergeWith(EditCommand& /*next*/) { return false; }
};

/// Generic command for timeline edits. It runs an edit function once, then records only the
/// tracks that changed (or the whole track list for structural edits), which keeps undo exact
/// and memory proportional to the edit.
class TimelineCommand final : public EditCommand
{
public:
    using Operation = std::function<Status(Timeline& timeline, const SourceLookup& sources)>;

    TimelineCommand(QString text, Operation operation, QString mergeKey = {});

    [[nodiscard]] QString text() const override { return m_text; }
    Result<ChangeSet> redo(Project& project) override;
    ChangeSet undo(Project& project) override;
    [[nodiscard]] QString mergeKey() const override { return m_mergeKey; }
    bool mergeWith(EditCommand& next) override;

private:
    QString m_text;
    Operation m_operation;
    QString m_mergeKey;
    bool m_done = false;
    bool m_structural = false;
    std::vector<Track> m_before; ///< changed tracks (or all tracks when structural)
    std::vector<Track> m_after;
};

/// Adds media assets to the project's media bin.
class AddMediaCommand final : public EditCommand
{
public:
    explicit AddMediaCommand(std::vector<MediaAsset> assets);
    [[nodiscard]] QString text() const override;
    Result<ChangeSet> redo(Project& project) override;
    ChangeSet undo(Project& project) override;

private:
    std::vector<MediaAsset> m_assets;
};

/// Removes an unused media asset.
class RemoveMediaCommand final : public EditCommand
{
public:
    explicit RemoveMediaCommand(Id assetId);
    [[nodiscard]] QString text() const override { return QStringLiteral("Remove Media"); }
    Result<ChangeSet> redo(Project& project) override;
    ChangeSet undo(Project& project) override;

private:
    Id m_assetId;
    std::optional<MediaAsset> m_removed;
    int m_index = -1;
};

/// Replaces an asset's file path and metadata, e.g. when relinking missing media.
class UpdateMediaCommand final : public EditCommand
{
public:
    UpdateMediaCommand(MediaAsset updated, QString text);
    [[nodiscard]] QString text() const override { return m_text; }
    Result<ChangeSet> redo(Project& project) override;
    ChangeSet undo(Project& project) override;

private:
    MediaAsset m_after;
    std::optional<MediaAsset> m_before;
    QString m_text;
};

/// Changes project settings and/or name.
class ProjectSettingsCommand final : public EditCommand
{
public:
    ProjectSettingsCommand(ProjectSettings settings, QString name);
    [[nodiscard]] QString text() const override
    {
        return QStringLiteral("Change Project Settings");
    }
    Result<ChangeSet> redo(Project& project) override;
    ChangeSet undo(Project& project) override;

private:
    ProjectSettings m_settings;
    QString m_name;
    ProjectSettings m_oldSettings;
    QString m_oldName;
};

} // namespace ft
