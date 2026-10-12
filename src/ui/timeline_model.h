// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>

#include <memory>
#include <vector>

namespace ft::ui {

/// The clips of one track, for one row of the timeline view.
class ClipListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the timeline model")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AssetIdRole,
        NameRole,
        StartRole,
        DurationRole,
        SourceInRole,
        SourceLengthRole,
        KindRole,
        SelectedRole,
        OfflineRole,
        MutedRole,
        GainRole,
        FadeInRole,
        FadeOutRole,
        ThumbnailRole,
        WaveformRole,
    };

    explicit ClipListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setClips(const Track& track, const Project& project, const QSet<Id>& selection);
    void updateSelection(const QSet<Id>& selection);

signals:
    void countChanged();

private:
    struct Row
    {
        Clip clip;
        QString name;
        QString kind;
        bool offline = false;
        qint64 sourceLength = -1;
        bool selected = false;
        bool onAudioTrack = false;
        QString waveform; ///< "<assetId>/<startUs>/<durationUs>" for clips whose sound is shown
    };
    std::vector<Row> m_rows;
};

/// Tracks in display order: video tracks from the top layer down, then audio tracks.
class TimelineModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        MutedRole,
        HiddenRole,
        LockedRole,
        ClipsRole,
        ProjectIndexRole,
        TransitionsRole,
    };

    explicit TimelineModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Structural changes rebuild the rows; otherwise only the listed tracks are refreshed, or
    /// every track when refreshAll is set (for example after media was relinked).
    void update(const Project& project, bool structural, bool refreshAll, const QSet<Id>& tracks,
                const QSet<Id>& selection);
    void updateSelection(const QSet<Id>& selection);
    void setSelectedTransition(const Id& transitionId);
    Q_INVOKABLE int rowOfTrack(const QString& trackId) const;

signals:
    void countChanged();

private:
    struct Row
    {
        Track track;
        int projectIndex = 0;
        std::unique_ptr<ClipListModel> clips;
    };
    std::vector<Row> m_rows;
    Id m_selectedTransition;
};

} // namespace ft::ui
