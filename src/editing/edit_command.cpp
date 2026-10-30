// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/edit_command.h"

#include <algorithm>

namespace ft {

namespace {

bool sameTrackOrder(const std::vector<Track>& a, const std::vector<Track>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const Track& x, const Track& y) { return x.id == y.id; });
}

ChangeSet trackChanges(bool structural, const std::vector<Track>& tracks)
{
    ChangeSet changes;
    changes.trackStructure = structural;
    for (const Track& t : tracks) {
        changes.tracks.insert(t.id);
    }
    return changes;
}

} // namespace

TimelineCommand::TimelineCommand(QString text, Operation operation, QString mergeKey)
    : m_text(std::move(text))
    , m_operation(std::move(operation))
    , m_mergeKey(std::move(mergeKey))
{}

Result<ChangeSet> TimelineCommand::redo(Project& project)
{
    if (m_done) {
        if (m_structural) {
            project.timeline().restoreTracks(m_after);
        } else {
            for (const Track& t : m_after) {
                project.timeline().restoreTrack(t);
            }
        }
        return trackChanges(m_structural, m_after);
    }

    // Run the edit on a copy so that a failing operation leaves the project untouched.
    Timeline working = project.timeline();
    if (const Status status = m_operation(working, project.sourceLookup()); !status) {
        return status.error();
    }
    const std::vector<Track>& before = project.timeline().tracks();
    const std::vector<Track>& after = working.tracks();
    m_before.clear();
    m_after.clear();
    if (!sameTrackOrder(before, after)) {
        m_structural = true;
        m_before = before;
        m_after = after;
    } else {
        for (size_t i = 0; i < before.size(); ++i) {
            if (!(before[i] == after[i])) {
                m_before.push_back(before[i]);
                m_after.push_back(after[i]);
            }
        }
    }
    m_done = true;
    if (!m_structural && m_after.empty()) {
        return ChangeSet{}; // nothing changed; the history discards no-op commands
    }
    project.timeline() = std::move(working);
    return trackChanges(m_structural, m_after);
}

ChangeSet TimelineCommand::undo(Project& project)
{
    if (m_structural) {
        project.timeline().restoreTracks(m_before);
    } else {
        for (const Track& t : m_before) {
            project.timeline().restoreTrack(t);
        }
    }
    return trackChanges(m_structural, m_before);
}

bool TimelineCommand::mergeWith(EditCommand& next)
{
    auto* other = dynamic_cast<TimelineCommand*>(&next);
    if (!other || m_mergeKey.isEmpty() || other->m_mergeKey != m_mergeKey || m_structural ||
        other->m_structural) {
        return false;
    }
    for (size_t i = 0; i < other->m_after.size(); ++i) {
        const Track& updated = other->m_after[i];
        const auto it = std::find_if(m_after.begin(), m_after.end(),
                                     [&](const Track& t) { return t.id == updated.id; });
        if (it != m_after.end()) {
            *it = updated;
        } else {
            m_before.push_back(other->m_before[i]);
            m_after.push_back(updated);
        }
    }
    return true;
}

AddMediaCommand::AddMediaCommand(std::vector<MediaAsset> assets)
    : m_assets(std::move(assets))
{}

QString AddMediaCommand::text() const
{
    return m_assets.size() == 1 ? QStringLiteral("Import Media")
                                : QStringLiteral("Import %1 Media Files").arg(m_assets.size());
}

Result<ChangeSet> AddMediaCommand::redo(Project& project)
{
    for (size_t i = 0; i < m_assets.size(); ++i) {
        if (const Status added = project.addAsset(m_assets[i]); !added) {
            for (size_t j = 0; j < i; ++j) {
                (void)project.removeAsset(m_assets[j].id);
            }
            return added.error();
        }
    }
    ChangeSet changes;
    changes.media = !m_assets.empty();
    return changes;
}

ChangeSet AddMediaCommand::undo(Project& project)
{
    for (auto it = m_assets.rbegin(); it != m_assets.rend(); ++it) {
        (void)project.removeAsset(it->id);
    }
    ChangeSet changes;
    changes.media = true;
    return changes;
}

RemoveMediaCommand::RemoveMediaCommand(Id assetId)
    : m_assetId(std::move(assetId))
{}

Result<ChangeSet> RemoveMediaCommand::redo(Project& project)
{
    m_index = project.assetIndex(m_assetId);
    Result<MediaAsset> removed = project.removeAsset(m_assetId);
    if (!removed) {
        return removed.error();
    }
    m_removed = std::move(removed).value();
    ChangeSet changes;
    changes.media = true;
    return changes;
}

ChangeSet RemoveMediaCommand::undo(Project& project)
{
    if (m_removed) {
        (void)project.addAsset(*m_removed, m_index);
    }
    ChangeSet changes;
    changes.media = true;
    return changes;
}

Result<ChangeSet> RemoveUnusedMediaCommand::redo(Project& project)
{
    if (m_removed.empty()) {
        const auto& assets = project.media();
        for (int index = 0; index < static_cast<int>(assets.size()); ++index) {
            const MediaAsset& asset = assets[static_cast<size_t>(index)];
            if (!project.timeline().usesAsset(asset.id)) {
                m_removed.push_back({index, asset});
            }
        }
    }
    if (m_removed.empty()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("No unused media was found in this project."));
    }

    // Work on a copy: any validation failure leaves the live project unchanged.
    Project updated = project;
    for (auto it = m_removed.rbegin(); it != m_removed.rend(); ++it) {
        if (updated.assetIndex(it->asset.id) != it->index ||
            updated.timeline().usesAsset(it->asset.id)) {
            return makeError(ErrorCode::Conflict,
                             QStringLiteral("An unused media reference changed."));
        }
        if (const Result<MediaAsset> removed = updated.removeAsset(it->asset.id); !removed) {
            return removed.error();
        }
    }
    project = std::move(updated);
    ChangeSet changes;
    changes.media = true;
    return changes;
}

ChangeSet RemoveUnusedMediaCommand::undo(Project& project)
{
    // Original ascending indices preserve exact media-bin order on undo.
    for (const RemovedAsset& item : m_removed) {
        (void)project.addAsset(item.asset, item.index);
    }
    ChangeSet changes;
    changes.media = true;
    return changes;
}

UpdateMediaCommand::UpdateMediaCommand(MediaAsset updated, QString text)
    : m_after(std::move(updated))
    , m_text(std::move(text))
{}

Result<ChangeSet> UpdateMediaCommand::redo(Project& project)
{
    const MediaAsset* current = project.asset(m_after.id);
    if (!current) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The media was not found."));
    }
    if (!m_before) {
        m_before = *current;
    }
    if (const Status replaced = project.replaceAsset(m_after); !replaced) {
        return replaced.error();
    }
    if (const Status valid = project.timeline().validate(project.sourceLookup()); !valid) {
        (void)project.replaceAsset(*m_before);
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The replacement file does not fit the clips that use this "
                                        "media (it may be shorter or lack audio or video)."),
                         valid.error().toString());
    }
    ChangeSet changes;
    changes.media = true;
    for (const Track& t : project.timeline().tracks()) {
        if (std::any_of(t.clips.begin(), t.clips.end(),
                        [&](const Clip& c) { return c.assetId == m_after.id; })) {
            changes.tracks.insert(t.id);
        }
    }
    return changes;
}

ChangeSet UpdateMediaCommand::undo(Project& project)
{
    if (m_before) {
        (void)project.replaceAsset(*m_before);
    }
    ChangeSet changes;
    changes.media = true;
    for (const Track& t : project.timeline().tracks()) {
        changes.tracks.insert(t.id);
    }
    return changes;
}

ProjectSettingsCommand::ProjectSettingsCommand(const ProjectSettings& settings, QString name)
    : m_settings(settings)
    , m_name(std::move(name))
{}

Result<ChangeSet> ProjectSettingsCommand::redo(Project& project)
{
    m_oldSettings = project.settings();
    m_oldName = project.name();
    if (const Status applied = project.setSettings(m_settings); !applied) {
        return applied.error();
    }
    const QString trimmed = m_name.trimmed();
    project.setName(trimmed.isEmpty() ? m_oldName : trimmed);
    ChangeSet changes;
    changes.settings = true;
    return changes;
}

ChangeSet ProjectSettingsCommand::undo(Project& project)
{
    (void)project.setSettings(m_oldSettings);
    project.setName(m_oldName);
    ChangeSet changes;
    changes.settings = true;
    return changes;
}

} // namespace ft
