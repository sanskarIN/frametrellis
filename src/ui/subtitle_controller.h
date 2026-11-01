// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace ft::ui {
class ProjectController;
class PlaybackController;

class SubtitleController : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY cuesChanged)
    Q_PROPERTY(int revision READ revision NOTIFY cuesChanged)
    Q_PROPERTY(QVariantMap selectedCue READ selectedCue NOTIFY selectedCueChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedCueChanged)
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY stateChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY stateChanged)
    Q_PROPERTY(bool burnIn READ burnIn NOTIFY stateChanged)
    Q_PROPERTY(int fontPercent READ fontPercent NOTIFY stateChanged)
    Q_PROPERTY(bool background READ background NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    enum Role {
        CueIdRole = Qt::UserRole + 1,
        StartLabelRole,
        EndLabelRole,
        CaptionRole,
        ActiveRole,
        SelectedRole,
        OutsideRole
    };
    SubtitleController(ProjectController* project, PlaybackController* playback,
                       QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QVariantMap selectedCue() const;
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] bool canAdd() const;
    [[nodiscard]] bool canSeek() const;
    [[nodiscard]] bool burnIn() const { return m_track.burnIn; }
    [[nodiscard]] int fontPercent() const { return m_track.fontPercent; }
    [[nodiscard]] bool background() const { return m_track.background; }
    [[nodiscard]] QString error() const { return m_error; }
    Q_INVOKABLE bool selectCue(const QString& id, bool seek = true);
    Q_INVOKABLE bool seekSelected();
    Q_INVOKABLE void previousCue();
    Q_INVOKABLE void nextCue();
    Q_INVOKABLE bool addAtPlayhead();
    Q_INVOKABLE bool updateSelected(const QString& start, const QString& end, const QString& text);
    Q_INVOKABLE bool removeSelected();
    Q_INVOKABLE bool clearAll();
    Q_INVOKABLE bool shiftAll(int deltaMilliseconds);
    Q_INVOKABLE bool setAppearance(bool burnIn, int fontPercent, bool background);
    Q_INVOKABLE bool resetAppearance();
    Q_INVOKABLE void clearError() { setError({}); }
    Q_INVOKABLE bool importSrt(const QString& pathOrUrl);
    Q_INVOKABLE bool exportSrt(const QString& pathOrUrl);
    Q_INVOKABLE bool exportWebVtt(const QString& pathOrUrl);
    Q_INVOKABLE QString playheadTimestamp() const;
    /// Returns only visible sampled cue spans, keeping timeline item count proportional to the
    /// viewport.
    Q_INVOKABLE QVariantList visibleCues(double firstFrame, double lastFrame) const;
signals:
    void cuesChanged();
    void selectedCueChanged();
    void stateChanged();
    void errorChanged();
    void statusMessage(const QString& message);

private:
    void refresh(bool replaceDocument = false);
    void onPositionChanged();
    bool execute(std::unique_ptr<EditCommand> command);
    bool fail(const QString& error);
    void setError(const QString& error);
    [[nodiscard]] std::optional<SubtitleCue> cueAtPlayhead() const;
    [[nodiscard]] bool cueCanSeek(const SubtitleCue& cue) const;
    [[nodiscard]] int activeRow() const;
    ProjectController* m_project;
    PlaybackController* m_playback;
    SubtitleTrack m_track;
    QHash<Id, int> m_rows;
    Id m_selected;
    QString m_error;
    int m_activeRow = -1;
    int m_revision = 0;
};
} // namespace ft::ui
