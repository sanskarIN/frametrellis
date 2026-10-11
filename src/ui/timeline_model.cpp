// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/timeline_model.h"

#include <QFileInfo>

namespace ft::ui {

ClipListModel::ClipListModel(QObject* parent) : QAbstractListModel(parent) {}

int ClipListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ClipListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Row& row = m_rows[static_cast<size_t>(index.row())];
    const Clip& c = row.clip;
    switch (role) {
    case IdRole:
        return c.id;
    case AssetIdRole:
        return c.assetId;
    case NameRole:
        return row.name;
    case StartRole:
        return static_cast<double>(c.start);
    case DurationRole:
        return static_cast<double>(c.duration);
    case SourceInRole:
        return static_cast<double>(c.sourceIn);
    case SourceLengthRole:
        return static_cast<double>(row.sourceLength);
    case KindRole:
        return row.kind;
    case SelectedRole:
        return row.selected;
    case OfflineRole:
        return row.offline;
    case MutedRole:
        return c.properties.muted;
    case GainRole:
        return c.properties.gain;
    case FadeInRole:
        return static_cast<double>(c.properties.fadeIn);
    case FadeOutRole:
        return static_cast<double>(c.properties.fadeOut);
    case ThumbnailRole:
        return row.kind == QLatin1String("audio") || row.offline
                   ? QString()
                   : QStringLiteral("image://ft-thumbnail/%1").arg(c.assetId);
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipListModel::roleNames() const
{
    return {{IdRole, "clipId"},         {AssetIdRole, "assetId"},      {NameRole, "name"},
            {StartRole, "start"},       {DurationRole, "duration"},    {SourceInRole, "sourceIn"},
            {SourceLengthRole, "sourceLength"}, {KindRole, "kind"},    {SelectedRole, "selected"},
            {OfflineRole, "offline"},   {MutedRole, "muted"},          {GainRole, "gain"},
            {FadeInRole, "fadeIn"},     {FadeOutRole, "fadeOut"},      {ThumbnailRole, "thumbnail"}};
}

void ClipListModel::setClips(const Track& track, const Project& project, const QSet<Id>& selection)
{
    std::vector<Row> rows;
    rows.reserve(track.clips.size());
    for (const Clip& clip : track.clips) {
        Row row;
        row.clip = clip;
        const MediaAsset* asset = project.asset(clip.assetId);
        row.name = asset ? asset->name : QStringLiteral("(missing media)");
        row.kind = asset ? mediaKindToString(asset->info.kind) : QStringLiteral("video");
        row.offline = !asset || asset->status == MediaStatus::Missing;
        row.sourceLength = asset ? asset->lengthInFrames(project.settings().frameRate).value_or(-1) : -1;
        row.selected = selection.contains(clip.id);
        rows.push_back(std::move(row));
    }
    const bool sameIds = rows.size() == m_rows.size()
                         && std::equal(rows.begin(), rows.end(), m_rows.begin(),
                                       [](const Row& a, const Row& b) { return a.clip.id == b.clip.id; });
    if (sameIds) {
        m_rows = std::move(rows);
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
        return;
    }
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

void ClipListModel::updateSelection(const QSet<Id>& selection)
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const bool selected = selection.contains(m_rows[i].clip.id);
        if (selected != m_rows[i].selected) {
            m_rows[i].selected = selected;
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed, {SelectedRole});
        }
    }
}

TimelineModel::TimelineModel(QObject* parent) : QAbstractListModel(parent) {}

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Row& row = m_rows[static_cast<size_t>(index.row())];
    switch (role) {
    case IdRole:
        return row.track.id;
    case NameRole:
        return row.track.name;
    case KindRole:
        return trackKindToString(row.track.kind);
    case MutedRole:
        return row.track.muted;
    case HiddenRole:
        return row.track.hidden;
    case LockedRole:
        return row.track.locked;
    case ClipsRole:
        return QVariant::fromValue(static_cast<QObject*>(row.clips.get()));
    case ProjectIndexRole:
        return row.projectIndex;
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{IdRole, "trackId"}, {NameRole, "name"},     {KindRole, "kind"},   {MutedRole, "muted"},
            {HiddenRole, "hidden"}, {LockedRole, "locked"}, {ClipsRole, "clips"}, {ProjectIndexRole, "projectIndex"}};
}

void TimelineModel::update(const Project& project, bool structural, bool refreshAll, const QSet<Id>& tracks,
                           const QSet<Id>& selection)
{
    const std::vector<Track>& all = project.timeline().tracks();
    std::vector<int> order;
    for (int i = static_cast<int>(all.size()) - 1; i >= 0; --i) {
        if (all[static_cast<size_t>(i)].kind == TrackKind::Video) {
            order.push_back(i);
        }
    }
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        if (all[static_cast<size_t>(i)].kind == TrackKind::Audio) {
            order.push_back(i);
        }
    }

    bool sameShape = !structural && order.size() == m_rows.size();
    for (size_t r = 0; sameShape && r < order.size(); ++r) {
        sameShape = m_rows[r].track.id == all[static_cast<size_t>(order[r])].id;
    }
    if (!sameShape) {
        beginResetModel();
        std::vector<Row> rows;
        for (const int projectIndex : order) {
            Row row;
            row.track = all[static_cast<size_t>(projectIndex)];
            row.projectIndex = projectIndex;
            row.clips = std::make_unique<ClipListModel>(this);
            row.clips->setClips(row.track, project, selection);
            rows.push_back(std::move(row));
        }
        m_rows = std::move(rows);
        endResetModel();
        emit countChanged();
        return;
    }
    for (size_t r = 0; r < m_rows.size(); ++r) {
        Row& row = m_rows[r];
        const Track& track = all[static_cast<size_t>(order[r])];
        if (!refreshAll && !tracks.contains(track.id)) {
            continue;
        }
        row.track = track;
        row.clips->setClips(track, project, selection);
        emit dataChanged(index(static_cast<int>(r)), index(static_cast<int>(r)));
    }
}

void TimelineModel::updateSelection(const QSet<Id>& selection)
{
    for (Row& row : m_rows) {
        row.clips->updateSelection(selection);
    }
}

int TimelineModel::rowOfTrack(const QString& trackId) const
{
    for (size_t r = 0; r < m_rows.size(); ++r) {
        if (m_rows[r].track.id == trackId) {
            return static_cast<int>(r);
        }
    }
    return -1;
}

} // namespace ft::ui
