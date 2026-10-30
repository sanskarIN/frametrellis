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
    Q_PROPERTY(QVariantMap audioAutomation READ audioAutomation NOTIFY audioAutomationChanged)
    Q_PROPERTY(QVariantMap opacityAutomation READ opacityAutomation NOTIFY opacityAutomationChanged)
    Q_PROPERTY(
        QVariantMap transformAutomation READ transformAutomation NOTIFY transformAutomationChanged)
    Q_PROPERTY(bool transformHandlesVisible READ transformHandlesVisible WRITE
                   setTransformHandlesVisible NOTIFY transformHandlesVisibleChanged)
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
    [[nodiscard]] QVariantMap audioAutomation() const;
    [[nodiscard]] QVariantMap opacityAutomation() const;
    [[nodiscard]] QVariantMap transformAutomation() const;
    [[nodiscard]] bool transformHandlesVisible() const { return m_transformHandlesVisible; }
    void setTransformHandlesVisible(bool visible);
    [[nodiscard]] double duration() const;
    [[nodiscard]] double frameRate() const;
    [[nodiscard]] bool empty() const;

    Q_INVOKABLE QString timecode(double frame) const;
    Q_INVOKABLE QString clock(double frame) const;
    /// Frames per second rounded up, for ruler labelling.
    Q_INVOKABLE int nominalRate() const;

    // Placing media
    Q_INVOKABLE bool appendAsset(const QString& assetId);
    /// Append a bounded source excerpt in project frames without changing the source.
    /// The out-frame is exclusive; still images and invalid excerpts are refused.
    Q_INVOKABLE bool appendAssetRange(const QString& assetId, int sourceInFrame,
                                      int sourceOutFrame);
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
    /// Move one selected clip so its start or exclusive end meets the current playhead.
    /// Reject overlaps, locked tracks and negative starts using the undoable edit engine.
    Q_INVOKABLE bool alignSelectionToPlayhead(bool alignEnd);
    /// Trim the selected clip's start or exclusive end to the playhead.
    /// Rejects unsafe transition edits rather than silently removing a transition.
    Q_INVOKABLE bool trimSelectionToPlayhead(bool trimEnd);
    Q_INVOKABLE bool trimStart(const QString& clipId, double newStart);
    Q_INVOKABLE bool trimEnd(const QString& clipId, double newEnd);
    Q_INVOKABLE bool splitAtPlayhead();
    /// Insert an undoable empty interval at a cut across video and audio tracks.
    Q_INVOKABLE bool insertGapAtPlayhead(int durationFrames);
    /// Close a clip-free time range across tracks as a single undoable edit.
    Q_INVOKABLE bool removeEmptyTimeAtPlayhead(int durationFrames);
    /// Duplicates a single selected clip, or a time-aligned block of several selected clips.
    Q_INVOKABLE bool duplicateSelection();
    /// Shift all selected clips by signed project frames without changing their source trims.
    Q_INVOKABLE bool nudgeSelection(int frames);
    /// Slip source time on every selected clip, keeping track placement and clip duration.
    Q_INVOKABLE bool slipSelection(int frames);
    /// Adjust the cut following one selected outgoing clip, without moving outer edit edges.
    Q_INVOKABLE bool rollSelection(int frames);
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
    /// Select all timeline clips covering the playhead, across video and audio tracks.
    /// An empty match leaves the current selection unchanged.
    Q_INVOKABLE bool selectClipsAtPlayhead();
    /// Select every clip using the same media asset as the single selected clip.
    Q_INVOKABLE bool selectAllMediaOccurrences();
    /// Select all clips on the given track, including locked tracks (inspection only).
    Q_INVOKABLE bool selectTrackClips(const QString& trackId);
    /// Bounds of all currently selected clips, [start, end) in timeline frames.
    /// Returns an empty map when no valid clip is selected; this is read-only.
    Q_INVOKABLE QVariantMap selectedFrameRange() const;
    /// Sets one clip property ("gain", "muted", "fadeIn", "fadeOut", "brightness", "contrast",
    /// "saturation" or "opacity"). Repeated changes merge into one undo step until endEdit().
    Q_INVOKABLE bool setClipProperty(const QString& clipId, const QString& name,
                                     const QVariant& value);
    Q_INVOKABLE bool resetPictureAdjustments(const QString& clipId);
    Q_INVOKABLE void endEdit();

    // Volume automation for the single selected clip; point edits require a paused playhead.
    Q_INVOKABLE bool setAudioKeyframeAtPlayhead(double decibels);
    Q_INVOKABLE bool removeAudioKeyframeAtPlayhead();
    Q_INVOKABLE bool clearSelectedAudioKeyframes();
    Q_INVOKABLE void previousAudioKeyframe();
    Q_INVOKABLE void nextAudioKeyframe();
    Q_INVOKABLE bool setOpacityKeyframeAtPlayhead(double opacity);
    Q_INVOKABLE bool removeOpacityKeyframeAtPlayhead();
    Q_INVOKABLE bool clearSelectedOpacityKeyframes();
    Q_INVOKABLE void previousOpacityKeyframe();
    Q_INVOKABLE void nextOpacityKeyframe();
    Q_INVOKABLE bool setTransformAtPlayhead(double x, double y, double scale, double rotation);
    Q_INVOKABLE bool setTransformComponent(const QString& component, double value);
    Q_INVOKABLE bool addTransformKeyframeAtPlayhead();
    Q_INVOKABLE bool removeTransformKeyframeAtPlayhead();
    Q_INVOKABLE bool clearSelectedTransformKeyframes();
    Q_INVOKABLE void previousTransformKeyframe();
    Q_INVOKABLE void nextTransformKeyframe();

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
    /// Copy a track immediately after its source, retaining edits and transitions.
    Q_INVOKABLE bool duplicateTrack(const QString& trackId);
    Q_INVOKABLE bool removeTrack(const QString& trackId);
    /// Reorder adjacent same-kind tracks (one step up/down), preserving edit data.
    Q_INVOKABLE bool canMoveTrack(const QString& trackId, int direction) const;
    Q_INVOKABLE bool moveTrack(const QString& trackId, int direction);
    Q_INVOKABLE bool setTrackFlag(const QString& trackId, const QString& flag, bool value);
    /// Lock/unlock all tracks as one undoable edit; unaffected flags remain unchanged.
    Q_INVOKABLE bool setAllTracksLocked(bool locked);
    Q_INVOKABLE bool renameTrack(const QString& trackId, const QString& name);

    // Persistent timeline review markers, independent of clip and media edits.
    Q_INVOKABLE bool addMarker(double frame, const QString& label);
    Q_INVOKABLE bool updateMarker(const QString& id, double frame, const QString& label);
    Q_INVOKABLE bool removeMarker(const QString& id);
    Q_INVOKABLE void seekToMarker(const QString& id);
    Q_INVOKABLE void previousMarker();
    Q_INVOKABLE void nextMarker();
    /// Copies validated sequence markers as HH:MM:SS chapter lines for video publishing.
    Q_INVOKABLE bool copyMarkersAsChapters();
    /// Place an HTML5/WebVTT chapter-track document on the clipboard.
    Q_INVOKABLE bool copyMarkersAsWebVtt();
    /// Copies a cuts-only CMX3600 picture edit decision list (non-destructive).
    Q_INVOKABLE bool copyPictureAsEdl();

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
    void audioAutomationChanged();
    void opacityAutomationChanged();
    void transformAutomationChanged();
    void transformHandlesVisibleChanged();
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
    [[nodiscard]] const Clip* singleSelectedClip() const;
    void seekAudioKeyframe(bool forward);
    void seekOpacityKeyframe(bool forward);
    void seekTransformKeyframe(bool forward);
    bool m_transformHandlesVisible = false;

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
