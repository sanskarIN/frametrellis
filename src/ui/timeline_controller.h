// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/change_set.h"
#include "ui/timeline_model.h"

#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace ft::ui {

class PlaybackController;
class ProjectController;

/// Timeline interactions: selection, snapping, zoom and every clip and track edit. All edits go
/// through undoable commands; failed edits leave the project unchanged and explain why.
class TimelineController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(ft::ui::TimelineModel* model READ model CONSTANT)
    Q_PROPERTY(double pixelsPerFrame READ pixelsPerFrame WRITE setPixelsPerFrame NOTIFY zoomChanged)
    Q_PROPERTY(double minPixelsPerFrame READ minPixelsPerFrame NOTIFY zoomChanged)
    Q_PROPERTY(double maxPixelsPerFrame READ maxPixelsPerFrame CONSTANT)
    Q_PROPERTY(bool snapping READ snapping WRITE setSnapping NOTIFY snappingChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(bool hasCopiedClipAttributes READ hasCopiedClipAttributes NOTIFY
                   copiedClipAttributesChanged)
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY selectedClipChanged)
    Q_PROPERTY(
        QVariantMap selectedTransition READ selectedTransition NOTIFY selectedTransitionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY durationChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY durationChanged)
    Q_PROPERTY(bool canAppendTitle READ canAppendTitle NOTIFY canAppendTitleChanged)

public:
    TimelineController(ProjectController* project, PlaybackController* playback,
                       QObject* parent = nullptr);

    [[nodiscard]] TimelineModel* model() { return &m_model; }
    [[nodiscard]] double pixelsPerFrame() const { return m_pixelsPerFrame; }
    void setPixelsPerFrame(double value);
    [[nodiscard]] double minPixelsPerFrame() const;
    [[nodiscard]] double maxPixelsPerFrame() const { return 24.0; }
    [[nodiscard]] bool snapping() const { return m_snapping; }
    void setSnapping(bool snapping);
    [[nodiscard]] QStringList selection() const;
    [[nodiscard]] QVariantList markers() const;
    [[nodiscard]] bool hasCopiedClipAttributes() const
    {
        return m_copiedClipAttributes.has_value();
    }
    [[nodiscard]] QVariantMap selectedClip() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] double frameRate() const;
    [[nodiscard]] bool empty() const;

    Q_INVOKABLE QString timecode(double frame) const;
    Q_INVOKABLE QString clock(double frame) const;
    /// Frames per second rounded up, for ruler labelling.
    Q_INVOKABLE int nominalRate() const;

    // Placing media
    Q_INVOKABLE bool appendAsset(const QString& assetId);
    /// Whether at least one unlocked video track can receive a new title.
    [[nodiscard]] bool canAppendTitle() const;
    /// Append a title still to the uppermost unlocked video track.
    Q_INVOKABLE bool appendTitleAsset(const QString& assetId);
    Q_INVOKABLE bool dropAsset(const QString& assetId, const QString& trackId, double frame);
    Q_INVOKABLE double assetDuration(const QString& assetId) const;
    Q_INVOKABLE bool acceptsAsset(const QString& trackId, const QString& assetId) const;

    // Clip edits
    Q_INVOKABLE bool canPlace(const QString& trackId, double start, double duration,
                              const QString& ignoreClipId) const;
    Q_INVOKABLE double snapFrame(double frame, const QString& excludeClipId,
                                 double toleranceFrames) const;
    /// Snaps a moving clip so that either its start or its end lands on an edit point.
    Q_INVOKABLE double snapClipStart(double start, double duration, const QString& excludeClipId,
                                     double toleranceFrames) const;
    Q_INVOKABLE bool moveClip(const QString& clipId, const QString& trackId, double start);
    Q_INVOKABLE bool trimStart(const QString& clipId, double newStart);
    Q_INVOKABLE bool trimEnd(const QString& clipId, double newEnd);
    Q_INVOKABLE bool splitAtPlayhead();
    /// Duplicates a single selected clip, or a time-aligned block of several selected clips.
    Q_INVOKABLE bool duplicateSelection();
    /// Shift all selected clips by signed project frames without changing their source trims.
    Q_INVOKABLE bool nudgeSelection(int frames);
    /// Slip source time on every selected clip, keeping track placement and clip duration.
    Q_INVOKABLE bool slipSelection(int frames);
    /// Stores the attributes of one selected clip and applies them to later selections.
    Q_INVOKABLE bool copySelectionAttributes();
    Q_INVOKABLE bool pasteSelectionAttributes();
    /// Audio muting and picture-reset batch commands; each operation is a single undo step.
    Q_INVOKABLE bool muteSelection(bool muted);
    Q_INVOKABLE bool resetSelectionPictures();
    Q_INVOKABLE bool deleteSelection(bool ripple);
    Q_INVOKABLE void select(const QString& clipId, bool toggle);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    /// Sets one clip property ("gain", "muted", "fadeIn", "fadeOut", "brightness", "contrast",
    /// "saturation" or "opacity"). Repeated changes merge into one undo step until endEdit().
    Q_INVOKABLE bool setClipProperty(const QString& clipId, const QString& name,
                                     const QVariant& value);
    Q_INVOKABLE bool resetPictureAdjustments(const QString& clipId);
    Q_INVOKABLE void endEdit();

    // Transitions
    [[nodiscard]] QVariantMap selectedTransition() const;
    /// Adds a transition (one second, or less if the media does not allow it) at the cut after
    /// the clip, or after the single selected clip when clipId is empty, and selects it.
    Q_INVOKABLE bool addTransition(const QString& clipId = {});
    Q_INVOKABLE void selectTransition(const QString& transitionId);
    Q_INVOKABLE bool setTransitionDuration(const QString& transitionId, double frames);
    Q_INVOKABLE bool removeTransition(const QString& transitionId);

    // Tracks
    Q_INVOKABLE bool addTrack(const QString& kind);
    Q_INVOKABLE bool removeTrack(const QString& trackId);
    Q_INVOKABLE bool setTrackFlag(const QString& trackId, const QString& flag, bool value);
    Q_INVOKABLE bool renameTrack(const QString& trackId, const QString& name);

    // Persistent timeline review markers, independent of clip and media edits.
    Q_INVOKABLE bool addMarker(double frame, const QString& label);
    Q_INVOKABLE bool updateMarker(const QString& id, double frame, const QString& label);
    Q_INVOKABLE bool removeMarker(const QString& id);
    Q_INVOKABLE void seekToMarker(const QString& id);
    Q_INVOKABLE void previousMarker();
    Q_INVOKABLE void nextMarker();

    // Navigation and zoom
    Q_INVOKABLE void previousEditPoint();
    Q_INVOKABLE void nextEditPoint();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit(double viewWidth);

signals:
    void zoomChanged();
    void snappingChanged();
    void markersChanged();
    void selectionChanged();
    void copiedClipAttributesChanged();
    void selectedClipChanged();
    void selectedTransitionChanged();
    void durationChanged();
    void canAppendTitleChanged();
    void statusMessage(const QString& message);

private:
    void onProjectChanged(const ChangeSet& changes);
    void pruneSelection();
    void setTransitionSelection(const Id& transitionId);
    [[nodiscard]] FrameCount stillDuration() const;
    [[nodiscard]] const Project& project() const;
    [[nodiscard]] std::optional<FrameCount> placementLength(const MediaAsset& asset) const;

    ProjectController* m_project;
    PlaybackController* m_playback;
    TimelineModel m_model;
    QSet<Id> m_selection;
    std::optional<ClipProperties> m_copiedClipAttributes;
    Id m_selectedTransition;
    double m_pixelsPerFrame = 2.0;
    bool m_snapping = true;
    double m_lastFit = 0;
};

} // namespace ft::ui
