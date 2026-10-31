// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/timeline_controller.h"

#include "core/timecode.h"
#include "editing/marker_edits.h"
#include "editing/timeline_edits.h"
#include "engine/mlt_runtime.h"
#include "project/edl_export.h"
#include "project/marker_chapters.h"
#include "ui/media_bin_model.h"
#include "ui/playback_controller.h"
#include "ui/project_controller.h"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ft::ui {

namespace {
constexpr double kZoomStep = 1.4;
constexpr int kStillSeconds = 5;

bool validTimelineFrame(double frame)
{
    return std::isfinite(frame) && frame >= 0.0 && frame <= static_cast<double>(kMaxTimelineFrames);
}
} // namespace

TimelineController::TimelineController(ProjectController* project, PlaybackController* playback,
                                       QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_playback(playback)
{
    connect(m_project, &ProjectController::projectChanged, this,
            &TimelineController::onProjectChanged);
    connect(this, &TimelineController::selectedClipChanged, this,
            &TimelineController::audioAutomationChanged);
    connect(m_playback, &PlaybackController::positionChanged, this,
            &TimelineController::audioAutomationChanged);
    connect(m_playback, &PlaybackController::playingChanged, this,
            &TimelineController::audioAutomationChanged);
    connect(this, &TimelineController::selectedClipChanged, this,
            &TimelineController::opacityAutomationChanged);
    connect(m_playback, &PlaybackController::positionChanged, this,
            &TimelineController::opacityAutomationChanged);
    connect(m_playback, &PlaybackController::playingChanged, this,
            &TimelineController::opacityAutomationChanged);
    connect(this, &TimelineController::selectedClipChanged, this,
            &TimelineController::transformAutomationChanged);
    connect(m_playback, &PlaybackController::positionChanged, this,
            &TimelineController::transformAutomationChanged);
    connect(m_playback, &PlaybackController::playingChanged, this,
            &TimelineController::transformAutomationChanged);
    connect(m_project, &ProjectController::documentReplaced, this, [this] {
        m_selection.clear();
        setTransitionSelection({});
        emit selectionChanged();
        emit selectedClipChanged();
    });
    onProjectChanged(ChangeSet::everything());
}

const Project& TimelineController::project() const
{
    return m_project->project();
}

void TimelineController::onProjectChanged(const ChangeSet& changes)
{
    pruneSelection();
    m_model.update(project(), changes.trackStructure, changes.media || changes.settings,
                   changes.tracks, m_selection);
    emit durationChanged();
    if (changes.markers) {
        emit markersChanged();
    }
    if (changes.trackStructure || !changes.tracks.isEmpty()) {
        emit canAppendTitleChanged();
    }
    emit selectedClipChanged();
    emit selectedTransitionChanged();
    if (changes.settings) {
        emit zoomChanged();
    }
}

void TimelineController::pruneSelection()
{
    bool changed = false;
    for (auto it = m_selection.begin(); it != m_selection.end();) {
        if (!project().timeline().clip(*it)) {
            it = m_selection.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        emit selectionChanged();
    }
    if (!m_selectedTransition.isEmpty() && !project().timeline().transition(m_selectedTransition)) {
        setTransitionSelection({});
    }
}

void TimelineController::setTransitionSelection(const Id& transitionId)
{
    if (transitionId == m_selectedTransition) {
        return;
    }
    m_selectedTransition = transitionId;
    m_model.setSelectedTransition(transitionId);
    emit selectedTransitionChanged();
}

bool TimelineController::addMarker(double frame, const QString& label)
{
    if (!std::isfinite(frame) || frame < 0 || frame > kMaxTimelineFrames ||
        std::trunc(frame) != frame || frame > duration()) {
        emit statusMessage(QStringLiteral("Place the marker at a frame within the sequence."));
        return false;
    }
    return m_project->execute(
        edits::addMarker({createId(), static_cast<FrameCount>(frame), label.trimmed()}));
}

bool TimelineController::updateMarker(const QString& id, double frame, const QString& label)
{
    if (!std::isfinite(frame) || frame < 0 || frame > kMaxTimelineFrames ||
        std::trunc(frame) != frame) {
        emit statusMessage(QStringLiteral("The marker frame is invalid."));
        return false;
    }
    return m_project->execute(edits::updateMarker(id, static_cast<FrameCount>(frame), label));
}

bool TimelineController::removeMarker(const QString& id)
{
    return m_project->execute(edits::removeMarker(id));
}

void TimelineController::seekToMarker(const QString& id)
{
    for (const TimelineMarker& marker : project().markers()) {
        if (marker.id == id) {
            if (marker.frame > project().timeline().duration()) {
                emit statusMessage(
                    QStringLiteral("This marker is beyond the current sequence end."));
                return;
            }
            m_playback->seek(static_cast<double>(marker.frame));
            return;
        }
    }
    emit statusMessage(QStringLiteral("The sequence marker was not found."));
}

void TimelineController::previousMarker()
{
    const FrameCount current = m_playback->positionFrames();
    const auto& points = project().markers();
    const auto it = std::lower_bound(
        points.begin(), points.end(), current,
        [](const TimelineMarker& marker, FrameCount frame) { return marker.frame < frame; });
    if (it != points.begin()) {
        m_playback->seek(static_cast<double>(std::prev(it)->frame));
    }
}

void TimelineController::nextMarker()
{
    const FrameCount current = m_playback->positionFrames();
    for (const TimelineMarker& marker : project().markers()) {
        if (marker.frame > current && marker.frame <= project().timeline().duration()) {
            m_playback->seek(static_cast<double>(marker.frame));
            return;
        }
    }
}

bool TimelineController::copyMarkersAsWebVtt()
{
    const auto text = chapters::toWebVtt(project());
    if (!text) {
        emit statusMessage(text.error().message);
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit statusMessage(QStringLiteral("The system clipboard is unavailable."));
        return false;
    }
    clipboard->setText(*text);
    emit statusMessage(QStringLiteral("Copied WebVTT chapters to clipboard."));
    return true;
}

bool TimelineController::copyMarkersAsChapters()
{
    const auto chapters = chapters::toText(project());
    if (!chapters) {
        emit statusMessage(chapters.error().message);
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit statusMessage(QStringLiteral("The system clipboard is unavailable."));
        return false;
    }
    clipboard->setText(*chapters);
    emit statusMessage(QStringLiteral("Copied timeline chapters to clipboard."));
    return true;
}

bool TimelineController::copyPictureAsEdl()
{
    const auto result = interchange::toCmx3600(project());
    if (!result) {
        emit statusMessage(result.error().message);
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit statusMessage(QStringLiteral("The system clipboard is unavailable."));
        return false;
    }
    clipboard->setText(*result);
    emit statusMessage(QStringLiteral("Copied picture edit decision list (CMX3600)."));
    return true;
}

void TimelineController::setPixelsPerFrame(double value)
{
    // Invalid QML or plugin-supplied numbers must not corrupt timeline geometry.
    if (!std::isfinite(value)) {
        return;
    }
    value = std::clamp(value, minPixelsPerFrame(), maxPixelsPerFrame());
    if (!qFuzzyCompare(value, m_pixelsPerFrame)) {
        m_pixelsPerFrame = value;
        emit zoomChanged();
    }
}

double TimelineController::minPixelsPerFrame() const
{
    // Allows roughly four hours in 2000 pixels at 30 fps.
    return 0.004;
}

void TimelineController::setSnapping(bool snapping)
{
    if (snapping != m_snapping) {
        m_snapping = snapping;
        emit snappingChanged();
    }
}

QVariantList TimelineController::markers() const
{
    QVariantList result;
    for (const TimelineMarker& marker : project().markers()) {
        result.append(
            QVariantMap{{QStringLiteral("id"), marker.id},
                        {QStringLiteral("frame"), static_cast<double>(marker.frame)},
                        {QStringLiteral("label"), marker.label},
                        {QStringLiteral("timecode"),
                         timecode::format(marker.frame, project().settings().frameRate)}});
    }
    return result;
}

QStringList TimelineController::selection() const
{
    return QStringList(m_selection.cbegin(), m_selection.cend());
}

double TimelineController::duration() const
{
    return static_cast<double>(project().timeline().duration());
}

double TimelineController::frameRate() const
{
    return project().settings().frameRate.toDouble();
}

bool TimelineController::empty() const
{
    return project().timeline().clipCount() == 0;
}

QString TimelineController::timecode(double frame) const
{
    return timecode::format(static_cast<FrameCount>(std::llround(frame)),
                            project().settings().frameRate);
}

QString TimelineController::clock(double frame) const
{
    return timecode::formatClock(static_cast<FrameCount>(std::llround(frame)),
                                 project().settings().frameRate);
}

int TimelineController::nominalRate() const
{
    return project().settings().frameRate.nominalFramesPerSecond();
}

QVariantMap TimelineController::selectedClip() const
{
    if (m_selection.size() != 1) {
        return {{QStringLiteral("count"), static_cast<int>(m_selection.size())}};
    }
    const Id clipId = *m_selection.cbegin();
    const Clip* clip = project().timeline().clip(clipId);
    const Track* track = project().timeline().trackOfClip(clipId);
    if (!clip || !track) {
        return {{QStringLiteral("count"), 0}};
    }
    const MediaAsset* asset = project().asset(clip->assetId);
    const ClipProperties& p = clip->properties;
    const FrameRate& rate = project().settings().frameRate;
    bool rollCutAvailable = false;
    if (const auto location = project().timeline().findClip(clipId)) {
        const auto& clips = track->clips;
        const size_t index = static_cast<size_t>(location->clip);
        if (index + 1 < clips.size() && clips[index].end() == clips[index + 1].start) {
            const MediaAsset* nextAsset = project().asset(clips[index + 1].assetId);
            rollCutAvailable = asset && nextAsset && asset->lengthInFrames(rate).has_value() &&
                               nextAsset->lengthInFrames(rate).has_value();
        }
    }
    return {
        {QStringLiteral("count"), 1},
        {QStringLiteral("clipId"), clip->id},
        {QStringLiteral("name"), asset ? asset->name : QStringLiteral("(missing media)")},
        {QStringLiteral("filePath"), asset ? asset->filePath : QString()},
        {QStringLiteral("details"), asset ? describeMedia(*asset) : QString()},
        {QStringLiteral("kind"), asset ? mediaKindToString(asset->info.kind) : QString()},
        {QStringLiteral("trackName"), track->name},
        {QStringLiteral("trackKind"), trackKindToString(track->kind)},
        {QStringLiteral("locked"), track->locked},
        {QStringLiteral("rollCutAvailable"), rollCutAvailable},
        {QStringLiteral("offline"), !asset || asset->status == MediaStatus::Missing},
        {QStringLiteral("start"), static_cast<double>(clip->start)},
        {QStringLiteral("duration"), static_cast<double>(clip->duration)},
        {QStringLiteral("startText"), timecode::format(clip->start, rate)},
        {QStringLiteral("endText"), timecode::format(clip->end(), rate)},
        {QStringLiteral("durationText"), timecode::formatClock(clip->duration, rate)},
        {QStringLiteral("sourceIn"), static_cast<double>(clip->sourceIn)},
        {QStringLiteral("sourceLength"),
         asset ? static_cast<double>(asset->lengthInFrames(rate).value_or(-1)) : -1.0},
        {QStringLiteral("sourceInText"), timecode::format(clip->sourceIn, rate)},
        {QStringLiteral("sourceOutText"), timecode::format(clip->sourceOut(), rate)},
        {QStringLiteral("hasAudio"), asset && asset->info.hasAudio},
        {QStringLiteral("hasVideo"),
         asset && asset->info.hasVideo && track->kind == TrackKind::Video},
        {QStringLiteral("gain"), p.gain},
        {QStringLiteral("muted"), p.muted},
        {QStringLiteral("fadeIn"), static_cast<double>(p.fadeIn)},
        {QStringLiteral("fadeOut"), static_cast<double>(p.fadeOut)},
        {QStringLiteral("brightness"), p.video.brightness},
        {QStringLiteral("contrast"), p.video.contrast},
        {QStringLiteral("saturation"), p.video.saturation},
        {QStringLiteral("opacity"), p.video.opacity},
        {QStringLiteral("adjustmentsAvailable"), engine::MltRuntime::capabilities().eqFilter},
    };
}

FrameCount TimelineController::stillDuration() const
{
    return static_cast<FrameCount>(project().settings().frameRate.nominalFramesPerSecond()) *
           kStillSeconds;
}

std::optional<FrameCount> TimelineController::placementLength(const MediaAsset& asset) const
{
    if (asset.info.kind == MediaKind::Image) {
        return stillDuration();
    }
    return asset.lengthInFrames(project().settings().frameRate);
}

double TimelineController::assetDuration(const QString& assetId) const
{
    const MediaAsset* asset = project().asset(assetId);
    if (!asset) {
        return 0;
    }
    return static_cast<double>(placementLength(*asset).value_or(0));
}

bool TimelineController::acceptsAsset(const QString& trackId, const QString& assetId) const
{
    const Track* track = project().timeline().track(trackId);
    const MediaAsset* asset = project().asset(assetId);
    if (!track || !asset || track->locked) {
        return false;
    }
    return track->kind == TrackKind::Video ? asset->info.hasVideo : asset->info.hasAudio;
}

bool TimelineController::canAppendTitle() const
{
    for (const Track& track : project().timeline().tracks()) {
        if (track.kind == TrackKind::Video && !track.locked) {
            return true;
        }
    }
    return false;
}

bool TimelineController::appendTitleAsset(const QString& assetId)
{
    const MediaAsset* asset = project().asset(assetId);
    if (!asset || !asset->info.hasVideo) {
        emit statusMessage(QStringLiteral("The title image is not available."));
        return false;
    }
    const Track* upper = nullptr;
    for (const Track& track : project().timeline().tracks()) {
        if (track.kind == TrackKind::Video && !track.locked) {
            upper = &track;
        }
    }
    if (!upper) {
        emit statusMessage(QStringLiteral("Add an unlocked video track for the title first."));
        return false;
    }
    return dropAsset(assetId, upper->id, static_cast<double>(upper->end()));
}

bool TimelineController::appendAsset(const QString& assetId)
{
    const MediaAsset* asset = project().asset(assetId);
    if (!asset) {
        return false;
    }
    const TrackKind wanted = asset->info.hasVideo ? TrackKind::Video : TrackKind::Audio;
    for (const Track& track : project().timeline().tracks()) {
        if (track.kind == wanted && !track.locked) {
            return dropAsset(assetId, track.id, static_cast<double>(track.end()));
        }
    }
    emit statusMessage(wanted == TrackKind::Video
                           ? QStringLiteral("Add an unlocked video track first.")
                           : QStringLiteral("Add an unlocked audio track first."));
    return false;
}

bool TimelineController::appendAssetRange(const QString& assetId, int sourceInFrame,
                                          int sourceOutFrame)
{
    const MediaAsset* asset = project().asset(assetId);
    if (!asset || asset->info.kind == MediaKind::Image) {
        emit statusMessage(QStringLiteral("Choose a video or audio source for an excerpt."));
        return false;
    }
    const std::optional<FrameCount> available = placementLength(*asset);
    if (!available || sourceInFrame < 0 || sourceOutFrame <= sourceInFrame ||
        static_cast<FrameCount>(sourceOutFrame) > *available) {
        emit statusMessage(QStringLiteral("Choose valid In and Out frames within the source."));
        return false;
    }

    const TrackKind wanted = asset->info.hasVideo ? TrackKind::Video : TrackKind::Audio;
    for (const Track& track : project().timeline().tracks()) {
        if (track.kind != wanted || track.locked) {
            continue;
        }
        Clip clip;
        clip.id = createId();
        clip.assetId = assetId;
        clip.start = track.end();
        clip.sourceIn = sourceInFrame;
        clip.duration = sourceOutFrame - sourceInFrame;
        if (!m_project->execute(edits::addClip(track.id, clip))) {
            return false;
        }
        m_selection = {clip.id};
        m_model.updateSelection(m_selection);
        emit selectionChanged();
        emit selectedClipChanged();
        return true;
    }
    emit statusMessage(QStringLiteral("Add an unlocked track for this media first."));
    return false;
}

bool TimelineController::dropAsset(const QString& assetId, const QString& trackId, double frame)
{
    // Negative drops are clamped to the beginning, but non-finite/huge values
    // must not reach llround(), which has undefined behaviour out of range.
    if (!std::isfinite(frame) || frame > static_cast<double>(kMaxTimelineFrames)) {
        return false;
    }
    const MediaAsset* asset = project().asset(assetId);
    const Track* track = project().timeline().track(trackId);
    if (!asset || !track) {
        return false;
    }
    if (!acceptsAsset(trackId, assetId)) {
        emit statusMessage(
            track->locked ? QStringLiteral("Track %1 is locked.").arg(track->name)
            : track->kind == TrackKind::Video
                ? QStringLiteral("%1 has no picture; drop it on an audio track.").arg(asset->name)
                : QStringLiteral("%1 has no sound; drop it on a video track.").arg(asset->name));
        return false;
    }
    Clip clip;
    clip.id = createId();
    clip.assetId = assetId;
    clip.start = static_cast<FrameCount>(std::llround(std::max(0.0, frame)));
    clip.duration = placementLength(*asset).value_or(stillDuration());
    if (!m_project->execute(edits::addClip(trackId, clip))) {
        return false;
    }
    m_selection = {clip.id};
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    return true;
}

bool TimelineController::canPlace(const QString& trackId, double start, double duration,
                                  const QString& ignoreClipId) const
{
    const Track* track = project().timeline().track(trackId);
    if (!track || track->locked || !validTimelineFrame(start) || !validTimelineFrame(duration) ||
        duration <= 0.0) {
        return false;
    }
    if (const Clip* moving = project().timeline().clip(ignoreClipId)) {
        const Track* from = project().timeline().trackOfClip(ignoreClipId);
        if (from && from->kind != track->kind) {
            const auto limits = project().sourceLimits(moving->assetId);
            if (!limits ||
                (track->kind == TrackKind::Video ? !limits->hasVideo : !limits->hasAudio)) {
                return false;
            }
        }
    }
    return project().timeline().isRangeFree(trackId, static_cast<FrameCount>(std::llround(start)),
                                            static_cast<FrameCount>(std::llround(duration)),
                                            ignoreClipId);
}

double TimelineController::snapFrame(double frame, const QString& excludeClipId,
                                     double toleranceFrames) const
{
    if (!validTimelineFrame(frame)) {
        return 0.0;
    }
    const auto candidate = static_cast<FrameCount>(std::llround(frame));
    if (!m_snapping) {
        return static_cast<double>(candidate);
    }
    std::vector<FrameCount> points = project().timeline().editPoints(excludeClipId);
    for (const TimelineMarker& marker : project().markers()) {
        points.push_back(marker.frame);
    }
    points.push_back(m_playback->positionFrames());
    std::sort(points.begin(), points.end());
    const double tolerance = validTimelineFrame(toleranceFrames) ? toleranceFrames : 0.0;
    const auto snapped =
        snapToNearest(candidate, static_cast<FrameCount>(std::ceil(tolerance)), points);
    return static_cast<double>(snapped.value_or(candidate));
}

double TimelineController::snapClipStart(double start, double duration,
                                         const QString& excludeClipId, double toleranceFrames) const
{
    if (!validTimelineFrame(start)) {
        return 0.0;
    }
    if (!validTimelineFrame(duration)) {
        return start;
    }
    const auto candidate = static_cast<FrameCount>(std::llround(start));
    const auto length = static_cast<FrameCount>(std::llround(duration));
    if (!m_snapping) {
        return static_cast<double>(std::max<FrameCount>(candidate, 0));
    }
    std::vector<FrameCount> points = project().timeline().editPoints(excludeClipId);
    for (const TimelineMarker& marker : project().markers()) {
        points.push_back(marker.frame);
    }
    points.push_back(m_playback->positionFrames());
    std::sort(points.begin(), points.end());
    const double safeTolerance = validTimelineFrame(toleranceFrames) ? toleranceFrames : 0.0;
    const auto tolerance = static_cast<FrameCount>(std::ceil(safeTolerance));
    const auto byStart = snapToNearest(candidate, tolerance, points);
    const auto byEnd = snapToNearest(candidate + length, tolerance, points);
    FrameCount result = candidate;
    if (byStart && byEnd) {
        result = std::abs(*byStart - candidate) <= std::abs(*byEnd - (candidate + length))
                     ? *byStart
                     : *byEnd - length;
    } else if (byStart) {
        result = *byStart;
    } else if (byEnd) {
        result = *byEnd - length;
    }
    return static_cast<double>(std::max<FrameCount>(result, 0));
}

bool TimelineController::moveClip(const QString& clipId, const QString& trackId, double start)
{
    if (!validTimelineFrame(start)) {
        return false;
    }
    return m_project->execute(
        edits::moveClip(clipId, trackId, static_cast<FrameCount>(std::llround(start))));
}

bool TimelineController::alignSelectionToPlayhead(bool alignEnd)
{
    if (m_selection.size() != 1) {
        emit statusMessage(QStringLiteral("Select one clip to align to the playhead."));
        return false;
    }

    const Id clipId = *m_selection.cbegin();
    const Timeline& timeline = project().timeline();
    const Clip* clip = timeline.clip(clipId);
    const Track* track = timeline.trackOfClip(clipId);
    if (!clip || !track) {
        return false;
    }

    const FrameCount frame = m_playback->positionFrames();
    if (alignEnd && frame < clip->duration) {
        emit statusMessage(QStringLiteral("The clip cannot end before its own duration."));
        return false;
    }
    const FrameCount newStart = alignEnd ? frame - clip->duration : frame;
    if (newStart == clip->start) {
        return false;
    }

    return m_project->execute(edits::moveClip(clipId, track->id, newStart));
}

bool TimelineController::alignSelectionStartToPlayhead()
{
    const QVariantMap bounds = selectedFrameRange();
    if (bounds.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to align."));
        return false;
    }

    const FrameCount first =
        static_cast<FrameCount>(bounds.value(QStringLiteral("start")).toDouble());
    const FrameCount delta = m_playback->positionFrames() - first;
    if (delta == 0) {
        return false;
    }
    return m_project->execute(
        edits::nudgeClips(QList<Id>(m_selection.cbegin(), m_selection.cend()), delta));
}

bool TimelineController::alignSelectionEndToPlayhead()
{
    const QVariantMap bounds = selectedFrameRange();
    if (bounds.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to align."));
        return false;
    }

    // Clip intervals are half-open: end is the first frame after the selected range.
    const FrameCount end = static_cast<FrameCount>(bounds.value(QStringLiteral("end")).toDouble());
    const FrameCount delta = m_playback->positionFrames() - end;
    if (delta == 0) {
        return false;
    }
    return m_project->execute(
        edits::nudgeClips(QList<Id>(m_selection.cbegin(), m_selection.cend()), delta));
}

bool TimelineController::trimSelectionToPlayhead(bool trimEnd)
{
    if (m_selection.size() != 1) {
        emit statusMessage(QStringLiteral("Select one clip to trim to the playhead."));
        return false;
    }
    const Id clipId = *m_selection.cbegin();
    const Timeline& timeline = project().timeline();
    const Clip* clip = timeline.clip(clipId);
    const Track* track = timeline.trackOfClip(clipId);
    if (!clip || !track || track->locked) {
        return false;
    }

    const FrameCount frame = m_playback->positionFrames();
    if (frame <= clip->start || frame >= clip->end()) {
        emit statusMessage(QStringLiteral("Place the playhead inside the selected clip."));
        return false;
    }
    for (const Transition& transition : track->transitions) {
        if (transition.fromClip == clipId || transition.toClip == clipId) {
            emit statusMessage(QStringLiteral("Remove the clip's transition before trimming."));
            return false;
        }
    }

    return trimEnd ? m_project->execute(edits::trimClipEnd(clipId, frame))
                   : m_project->execute(edits::trimClipStart(clipId, frame));
}

bool TimelineController::trimStart(const QString& clipId, double newStart)
{
    if (!validTimelineFrame(newStart)) {
        return false;
    }
    return m_project->execute(
        edits::trimClipStart(clipId, static_cast<FrameCount>(std::llround(newStart))));
}

bool TimelineController::trimEnd(const QString& clipId, double newEnd)
{
    if (!validTimelineFrame(newEnd)) {
        return false;
    }
    return m_project->execute(
        edits::trimClipEnd(clipId, static_cast<FrameCount>(std::llround(newEnd))));
}

bool TimelineController::insertGapAtPlayhead(int durationFrames)
{
    if (durationFrames <= 0) {
        return false;
    }
    return m_project->execute(
        edits::insertGap(m_playback->positionFrames(), static_cast<FrameCount>(durationFrames)));
}

bool TimelineController::removeEmptyTimeAtPlayhead(int durationFrames)
{
    if (durationFrames <= 0) {
        return false;
    }
    return m_project->execute(edits::removeEmptyTime(m_playback->positionFrames(),
                                                     static_cast<FrameCount>(durationFrames)));
}

bool TimelineController::closeEmptyGapToNextClip()
{
    const FrameCount frame = m_playback->positionFrames();
    FrameCount next = kMaxTimelineFrames;
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            if (clip.start <= frame && clip.end() > frame) {
                emit statusMessage(QStringLiteral("Move the playhead into empty timeline space."));
                return false;
            }
            if (clip.start > frame) {
                next = std::min(next, clip.start);
            }
        }
    }
    if (next == kMaxTimelineFrames) {
        emit statusMessage(QStringLiteral("No later clip follows the playhead."));
        return false;
    }
    return m_project->execute(edits::removeEmptyTime(frame, next - frame));
}

bool TimelineController::splitAtPlayhead()
{
    const FrameCount frame = m_playback->positionFrames();
    QList<Id> targets(m_selection.cbegin(), m_selection.cend());
    if (targets.isEmpty()) {
        // Without a selection, split every unlocked clip under the playhead.
        for (const Track& track : project().timeline().tracks()) {
            for (const Clip& clip : track.clips) {
                if (!track.locked && frame > clip.start && frame < clip.end()) {
                    targets.append(clip.id);
                }
            }
        }
    }
    if (targets.isEmpty()) {
        emit statusMessage(QStringLiteral("Move the playhead over a clip to split it."));
        return false;
    }
    return m_project->execute(edits::splitClips(targets, frame));
}

bool TimelineController::duplicateSelection()
{
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to duplicate."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    QList<Id> copies;
    copies.reserve(ids.size());
    for (qsizetype i = 0; i < ids.size(); ++i) {
        copies.append(createId());
    }
    if (ids.size() == 1) {
        if (!m_project->execute(edits::duplicateClip(ids.front(), copies.front()))) {
            return false;
        }
    } else if (!m_project->execute(edits::duplicateClips(ids, copies))) {
        return false;
    }
    m_selection.clear();
    for (const Id& id : copies) {
        m_selection.insert(id);
    }
    setTransitionSelection({});
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(QStringLiteral("Duplicated %1 clip(s).").arg(copies.size()));
    return true;
}

bool TimelineController::nudgeSelection(int frames)
{
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to nudge."));
        return false;
    }
    if (frames == 0) {
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::nudgeClips(ids, static_cast<FrameCount>(frames)))) {
        return false;
    }
    emit statusMessage(
        QStringLiteral("Moved %1 clip(s) by %2 frame(s).").arg(ids.size()).arg(frames));
    return true;
}

bool TimelineController::slipSelection(int frames)
{
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to slip."));
        return false;
    }
    if (frames == 0) {
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::slipClips(ids, static_cast<FrameCount>(frames)))) {
        return false;
    }
    emit statusMessage(
        QStringLiteral("Slipped %1 clip(s) by %2 source frame(s).").arg(ids.size()).arg(frames));
    return true;
}

bool TimelineController::rollSelection(int frames)
{
    if (m_selection.size() != 1) {
        emit statusMessage(
            QStringLiteral("Select exactly one outgoing (left) clip to roll its following cut."));
        return false;
    }
    if (frames == 0) {
        return false;
    }
    const Id outgoing = *m_selection.cbegin();
    if (!m_project->execute(edits::rollCut(outgoing, static_cast<FrameCount>(frames)))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Rolled the cut by %1 frame(s).").arg(frames));
    return true;
}

bool TimelineController::copySelectionAttributes()
{
    if (m_selection.size() != 1) {
        emit statusMessage(QStringLiteral("Select one source clip to copy its attributes."));
        return false;
    }
    const Clip* clip = project().timeline().clip(*m_selection.cbegin());
    if (!clip) {
        return false;
    }
    m_copiedClipAttributes = clip->properties;
    emit copiedClipAttributesChanged();
    emit statusMessage(QStringLiteral("Copied volume, fades and picture adjustments."));
    return true;
}

bool TimelineController::pasteSelectionAttributes()
{
    if (!m_copiedClipAttributes) {
        emit statusMessage(QStringLiteral("Copy attributes from a clip first."));
        return false;
    }
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select destination clips to paste attributes."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::pasteClipProperties(ids, *m_copiedClipAttributes))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Applied clip attributes to %1 clip(s).").arg(ids.size()));
    return true;
}

bool TimelineController::muteSelection(bool muted)
{
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips first."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::muteClips(ids, muted))) {
        return false;
    }
    emit statusMessage(muted ? QStringLiteral("Muted selected clips.")
                             : QStringLiteral("Unmuted selected clips."));
    return true;
}

bool TimelineController::setSelectionFade(bool fadeOut, int durationFrames)
{
    if (m_selection.isEmpty() || durationFrames < 0) {
        emit statusMessage(QStringLiteral("Select clips and a nonnegative fade duration."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(
            edits::setClipFades(ids, static_cast<FrameCount>(durationFrames), fadeOut))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Updated fades on %1 clip(s).").arg(ids.size()));
    return true;
}

bool TimelineController::setSelectionGain(double gain)
{
    if (m_selection.isEmpty() || !std::isfinite(gain) || gain < 0.0 || gain > kMaxClipGain) {
        emit statusMessage(QStringLiteral("Select clips and a valid audio gain."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::setClipGains(ids, gain))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Updated audio gain on %1 clip(s).").arg(ids.size()));
    return true;
}

bool TimelineController::setSelectionOpacity(double opacity)
{
    if (m_selection.isEmpty() || !std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        emit statusMessage(QStringLiteral("Select video clips and a valid opacity."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::setClipOpacities(ids, opacity))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Updated video opacity on %1 clip(s).").arg(ids.size()));
    return true;
}

bool TimelineController::resetSelectionPictures()
{
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips first."));
        return false;
    }
    QList<Id> ids(m_selection.cbegin(), m_selection.cend());
    std::sort(ids.begin(), ids.end());
    if (!m_project->execute(edits::resetClipPictures(ids))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Reset picture adjustments for selected clips."));
    return true;
}

bool TimelineController::deleteSelection(bool ripple)
{
    if (m_selection.isEmpty() && !m_selectedTransition.isEmpty()) {
        return removeTransition(m_selectedTransition);
    }
    if (m_selection.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more clips to delete."));
        return false;
    }
    if (ripple) {
        if (m_selection.size() != 1) {
            emit statusMessage(QStringLiteral("Ripple delete works on one clip at a time."));
            return false;
        }
        return m_project->execute(edits::rippleRemoveClip(*m_selection.cbegin()));
    }
    return m_project->execute(
        edits::removeClips(QList<Id>(m_selection.cbegin(), m_selection.cend())));
}

void TimelineController::select(const QString& clipId, bool toggle)
{
    if (!project().timeline().clip(clipId)) {
        return;
    }
    if (toggle) {
        if (m_selection.contains(clipId)) {
            m_selection.remove(clipId);
        } else {
            m_selection.insert(clipId);
        }
    } else {
        m_selection = {clipId};
    }
    setTransitionSelection({});
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
}

void TimelineController::clearSelection()
{
    setTransitionSelection({});
    if (m_selection.isEmpty()) {
        return;
    }
    m_selection.clear();
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
}

void TimelineController::selectAll()
{
    setTransitionSelection({});
    m_selection.clear();
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            m_selection.insert(clip.id);
        }
    }
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
}

bool TimelineController::selectClipsAtPlayhead()
{
    const FrameCount frame = m_playback->positionFrames();
    QSet<Id> matching;
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            if (clip.contains(frame)) {
                matching.insert(clip.id);
            }
        }
    }
    if (matching.isEmpty()) {
        emit statusMessage(QStringLiteral("No clips are under the playhead."));
        return false;
    }
    setTransitionSelection({});
    m_selection = std::move(matching);
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(
        QStringLiteral("Selected %1 clip(s) at the playhead.").arg(m_selection.size()));
    return true;
}

bool TimelineController::selectClipsBeforePlayhead()
{
    const FrameCount frame = m_playback->positionFrames();
    QSet<Id> matching;
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            if (clip.end() <= frame) {
                matching.insert(clip.id);
            }
        }
    }
    if (matching.isEmpty()) {
        emit statusMessage(QStringLiteral("No complete clips end before the playhead."));
        return false;
    }
    setTransitionSelection({});
    m_selection = std::move(matching);
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(QStringLiteral("Selected %1 earlier clip(s).").arg(m_selection.size()));
    return true;
}

bool TimelineController::selectClipsAfterPlayhead()
{
    const FrameCount frame = m_playback->positionFrames();
    QSet<Id> matching;
    for (const Track& track : project().timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            if (clip.start >= frame) {
                matching.insert(clip.id);
            }
        }
    }
    if (matching.isEmpty()) {
        emit statusMessage(QStringLiteral("No complete clips start after the playhead."));
        return false;
    }
    setTransitionSelection({});
    m_selection = std::move(matching);
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(QStringLiteral("Selected %1 later clip(s).").arg(m_selection.size()));
    return true;
}

bool TimelineController::selectAllMediaOccurrences()
{
    if (m_selection.size() != 1) {
        emit statusMessage(QStringLiteral("Select one clip to find its other occurrences."));
        return false;
    }
    const Clip* source = project().timeline().clip(*m_selection.cbegin());
    if (!source) {
        return false;
    }
    const std::vector<Id> ids = project().timeline().clipsUsingAsset(source->assetId);
    if (ids.empty()) {
        return false;
    }
    setTransitionSelection({});
    m_selection.clear();
    for (const Id& id : ids) {
        m_selection.insert(id);
    }
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(
        QStringLiteral("Selected %1 occurrence(s) of this media.").arg(m_selection.size()));
    return true;
}

bool TimelineController::selectTrackClips(const QString& trackId)
{
    const Track* track = project().timeline().track(trackId);
    if (!track || track->clips.empty()) {
        emit statusMessage(QStringLiteral("This track has no clips to select."));
        return false;
    }
    QSet<Id> selected;
    for (const Clip& clip : track->clips) {
        selected.insert(clip.id);
    }
    setTransitionSelection({});
    m_selection = std::move(selected);
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    emit statusMessage(
        QStringLiteral("Selected %1 clips on track %2.").arg(m_selection.size()).arg(track->name));
    return true;
}

QVariantMap TimelineController::selectedFrameRange() const
{
    FrameCount first = kMaxTimelineFrames;
    FrameCount last = 0;
    for (const Id& id : m_selection) {
        const Clip* clip = project().timeline().clip(id);
        if (!clip) {
            continue;
        }
        first = std::min(first, clip->start);
        last = std::max(last, clip->end());
    }
    if (last <= first) {
        return {};
    }
    return {{QStringLiteral("start"), static_cast<double>(first)},
            {QStringLiteral("end"), static_cast<double>(last)}};
}

bool TimelineController::seekToSelectionBoundary(bool lastFrame)
{
    const QVariantMap range = selectedFrameRange();
    if (range.isEmpty()) {
        emit statusMessage(QStringLiteral("Select at least one clip to navigate."));
        return false;
    }
    const FrameCount first =
        static_cast<FrameCount>(range.value(QStringLiteral("start")).toDouble());
    const FrameCount end = static_cast<FrameCount>(range.value(QStringLiteral("end")).toDouble());
    // Timeline clips use half-open [start, end) ranges. Seek to the final
    // visible frame, not the one-past-end boundary.
    m_playback->seek(static_cast<double>(lastFrame ? end - 1 : first));
    return true;
}

bool TimelineController::setClipProperty(const QString& clipId, const QString& name,
                                         const QVariant& value)
{
    const Clip* clip = project().timeline().clip(clipId);
    if (!clip) {
        return false;
    }
    ClipProperties p = clip->properties;
    QString text;
    if (name == QLatin1String("gain")) {
        p.gain = std::clamp(value.toDouble(), 0.0, kMaxClipGain);
        text = QStringLiteral("Change Volume");
    } else if (name == QLatin1String("muted")) {
        p.muted = value.toBool();
        text = p.muted ? QStringLiteral("Mute Clip") : QStringLiteral("Unmute Clip");
    } else if (name == QLatin1String("fadeIn")) {
        p.fadeIn = std::clamp<FrameCount>(value.toLongLong(), 0, clip->duration - p.fadeOut);
        text = QStringLiteral("Change Fade In");
    } else if (name == QLatin1String("fadeOut")) {
        p.fadeOut = std::clamp<FrameCount>(value.toLongLong(), 0, clip->duration - p.fadeIn);
        text = QStringLiteral("Change Fade Out");
    } else if (name == QLatin1String("brightness")) {
        p.video.brightness = std::clamp(value.toDouble(), -1.0, 1.0);
        text = QStringLiteral("Change Brightness");
    } else if (name == QLatin1String("contrast")) {
        p.video.contrast = std::clamp(value.toDouble(), 0.0, 2.0);
        text = QStringLiteral("Change Contrast");
    } else if (name == QLatin1String("saturation")) {
        p.video.saturation = std::clamp(value.toDouble(), 0.0, 3.0);
        text = QStringLiteral("Change Saturation");
    } else if (name == QLatin1String("opacity")) {
        p.video.opacity = std::clamp(value.toDouble(), 0.0, 1.0);
        text = QStringLiteral("Change Opacity");
    } else {
        return false;
    }
    if (p == clip->properties) {
        return true;
    }
    return m_project->execute(edits::setClipProperties(clipId, p, text));
}

bool TimelineController::resetPictureAdjustments(const QString& clipId)
{
    const Clip* clip = project().timeline().clip(clipId);
    if (!clip) {
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::resetClipPicture(clipId));
}

const Clip* TimelineController::singleSelectedClip() const
{
    return m_selection.size() == 1 ? project().timeline().clip(*m_selection.cbegin()) : nullptr;
}

QVariantMap TimelineController::audioAutomation() const
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return {{QStringLiteral("available"), false}, {QStringLiteral("count"), 0}};
    }
    const Track* track = project().timeline().trackOfClip(clip->id);
    const MediaAsset* asset = project().asset(clip->assetId);
    const FrameCount frame = m_playback->positionFrames();
    const FrameCount sourceFrame =
        clip->sourceIn + std::clamp<FrameCount>(frame - clip->start, 0, clip->duration - 1);
    const bool available =
        asset && asset->info.hasAudio && engine::MltRuntime::capabilities().volumeFilter;
    bool atKeyframe = false;
    bool hasPrevious = false;
    bool hasNext = false;
    int visibleCount = 0;
    for (const AudioKeyframe& point : clip->audioKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount timelineFrame = clip->start + point.sourceFrame - clip->sourceIn;
        ++visibleCount;
        atKeyframe = atKeyframe || timelineFrame == frame;
        hasPrevious = hasPrevious || timelineFrame < frame;
        hasNext = hasNext || timelineFrame > frame;
    }
    return {{QStringLiteral("available"), available},
            {QStringLiteral("canEdit"), available && track && !track->locked &&
                                            clip->contains(frame) && !m_playback->playing()},
            {QStringLiteral("atKeyframe"), atKeyframe},
            {QStringLiteral("count"), static_cast<int>(clip->audioKeyframes.size())},
            {QStringLiteral("visibleCount"), visibleCount},
            {QStringLiteral("decibels"), audioKeyframeDecibels(clip->audioKeyframes, sourceFrame)},
            {QStringLiteral("hasPrevious"), hasPrevious},
            {QStringLiteral("hasNext"), hasNext}};
}

bool TimelineController::setAudioKeyframeAtPlayhead(double decibels)
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !audioAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(QStringLiteral("Pause with the playhead inside one unlocked audio clip "
                                          "or video clip with audio to set a volume keyframe."));
        return false;
    }
    const FrameCount sourceFrame = clip->sourceIn + m_playback->positionFrames() - clip->start;
    // Avoid adding an undo entry for an unchanged point, but always validate numeric input.
    if (std::isfinite(decibels)) {
        for (const AudioKeyframe& point : clip->audioKeyframes) {
            if (point.sourceFrame == sourceFrame && point.decibels == decibels) {
                return true;
            }
        }
    }
    return m_project->execute(edits::setAudioKeyframe(clip->id, sourceFrame, decibels));
}

bool TimelineController::removeAudioKeyframeAtPlayhead()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !audioAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(QStringLiteral("Pause with the playhead inside an unlocked audio clip "
                                          "to remove its volume keyframe."));
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::removeAudioKeyframe(
        clip->id, clip->sourceIn + m_playback->positionFrames() - clip->start));
}

bool TimelineController::clearSelectedAudioKeyframes()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || clip->audioKeyframes.empty()) {
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::clearAudioKeyframes(clip->id));
}

void TimelineController::seekAudioKeyframe(bool forward)
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return;
    }
    std::optional<FrameCount> target;
    const FrameCount frame = m_playback->positionFrames();
    for (const AudioKeyframe& point : clip->audioKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount candidate = clip->start + point.sourceFrame - clip->sourceIn;
        if ((forward && candidate > frame && (!target || candidate < *target)) ||
            (!forward && candidate < frame && (!target || candidate > *target))) {
            target = candidate;
        }
    }
    if (target) {
        m_playback->pause();
        m_project->document()->breakMerge();
        m_playback->seek(static_cast<double>(*target));
    }
}

void TimelineController::previousAudioKeyframe()
{
    seekAudioKeyframe(false);
}

void TimelineController::nextAudioKeyframe()
{
    seekAudioKeyframe(true);
}

QVariantMap TimelineController::opacityAutomation() const
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return {{QStringLiteral("available"), false}, {QStringLiteral("count"), 0}};
    }
    const Track* track = project().timeline().trackOfClip(clip->id);
    const MediaAsset* asset = project().asset(clip->assetId);
    const FrameCount frame = m_playback->positionFrames();
    const FrameCount sourceFrame =
        clip->sourceIn + std::clamp<FrameCount>(frame - clip->start, 0, clip->duration - 1);
    const bool available = asset && asset->info.hasVideo && track &&
                           track->kind == TrackKind::Video &&
                           engine::MltRuntime::capabilities().brightnessFilter;
    bool atKeyframe = false;
    bool hasPrevious = false;
    bool hasNext = false;
    int visibleCount = 0;
    for (const OpacityKeyframe& point : clip->opacityKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount timelineFrame = clip->start + point.sourceFrame - clip->sourceIn;
        ++visibleCount;
        atKeyframe = atKeyframe || timelineFrame == frame;
        hasPrevious = hasPrevious || timelineFrame < frame;
        hasNext = hasNext || timelineFrame > frame;
    }
    return {{QStringLiteral("available"), available},
            {QStringLiteral("canEdit"), available && track && !track->locked &&
                                            clip->contains(frame) && !m_playback->playing()},
            {QStringLiteral("atKeyframe"), atKeyframe},
            {QStringLiteral("count"), static_cast<int>(clip->opacityKeyframes.size())},
            {QStringLiteral("visibleCount"), visibleCount},
            {QStringLiteral("opacity"), opacityKeyframeValue(clip->opacityKeyframes, sourceFrame)},
            {QStringLiteral("hasPrevious"), hasPrevious},
            {QStringLiteral("hasNext"), hasNext}};
}

bool TimelineController::setOpacityKeyframeAtPlayhead(double opacity)
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !opacityAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(
            QStringLiteral("Pause with the playhead inside one unlocked video or image clip "
                           "to set an opacity keyframe."));
        return false;
    }
    const FrameCount sourceFrame = clip->sourceIn + m_playback->positionFrames() - clip->start;
    // Avoid adding an undo entry for an unchanged point, but always validate numeric input.
    if (std::isfinite(opacity)) {
        for (const OpacityKeyframe& point : clip->opacityKeyframes) {
            if (point.sourceFrame == sourceFrame && point.opacity == opacity) {
                return true;
            }
        }
    }
    return m_project->execute(edits::setOpacityKeyframe(clip->id, sourceFrame, opacity));
}

bool TimelineController::removeOpacityKeyframeAtPlayhead()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !opacityAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(
            QStringLiteral("Pause with the playhead inside an unlocked video or image clip "
                           "to remove its opacity keyframe."));
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::removeOpacityKeyframe(
        clip->id, clip->sourceIn + m_playback->positionFrames() - clip->start));
}

bool TimelineController::clearSelectedOpacityKeyframes()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || clip->opacityKeyframes.empty()) {
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::clearOpacityKeyframes(clip->id));
}

void TimelineController::seekOpacityKeyframe(bool forward)
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return;
    }
    std::optional<FrameCount> target;
    const FrameCount frame = m_playback->positionFrames();
    for (const OpacityKeyframe& point : clip->opacityKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount candidate = clip->start + point.sourceFrame - clip->sourceIn;
        if ((forward && candidate > frame && (!target || candidate < *target)) ||
            (!forward && candidate < frame && (!target || candidate > *target))) {
            target = candidate;
        }
    }
    if (target) {
        m_playback->pause();
        m_project->document()->breakMerge();
        m_playback->seek(static_cast<double>(*target));
    }
}

void TimelineController::previousOpacityKeyframe()
{
    seekOpacityKeyframe(false);
}

void TimelineController::nextOpacityKeyframe()
{
    seekOpacityKeyframe(true);
}

QVariantMap TimelineController::transformAutomation() const
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return {{QStringLiteral("available"), false}, {QStringLiteral("count"), 0}};
    }
    const Track* track = project().timeline().trackOfClip(clip->id);
    const MediaAsset* asset = project().asset(clip->assetId);
    const FrameCount frame = m_playback->positionFrames();
    const FrameCount sourceFrame =
        clip->sourceIn + std::clamp<FrameCount>(frame - clip->start, 0, clip->duration - 1);
    const bool available = asset && asset->info.hasVideo && track &&
                           track->kind == TrackKind::Video &&
                           engine::MltRuntime::capabilities().transformFilter;
    bool atKeyframe = false;
    bool hasPrevious = false;
    bool hasNext = false;
    int visibleCount = 0;
    for (const TransformKeyframe& point : clip->transformKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount timelineFrame = clip->start + point.sourceFrame - clip->sourceIn;
        ++visibleCount;
        atKeyframe = atKeyframe || timelineFrame == frame;
        hasPrevious = hasPrevious || timelineFrame < frame;
        hasNext = hasNext || timelineFrame > frame;
    }
    const VideoTransform value = transformKeyframeValue(clip->transformKeyframes, sourceFrame);
    return {{QStringLiteral("clipId"), clip->id},
            {QStringLiteral("sourceFrame"), static_cast<double>(sourceFrame)},
            {QStringLiteral("available"), available},
            {QStringLiteral("canEdit"), available && track && !track->locked &&
                                            clip->contains(frame) && !m_playback->playing()},
            {QStringLiteral("atKeyframe"), atKeyframe},
            {QStringLiteral("count"), static_cast<int>(clip->transformKeyframes.size())},
            {QStringLiteral("visibleCount"), visibleCount},
            {QStringLiteral("x"), value.x},
            {QStringLiteral("y"), value.y},
            {QStringLiteral("scale"), value.scale},
            {QStringLiteral("rotation"), value.rotation},
            {QStringLiteral("hasPrevious"), hasPrevious},
            {QStringLiteral("hasNext"), hasNext}};
}

bool TimelineController::setTransformAtPlayhead(double x, double y, double scale, double rotation)
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !transformAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(QStringLiteral("Pause with the playhead inside one unlocked video or "
                                          "image clip to edit its transform."));
        return false;
    }
    const VideoTransform value{x, y, scale, rotation};
    const FrameCount sourceFrame = clip->sourceIn + m_playback->positionFrames() - clip->start;
    if (validateVideoTransform(value)) {
        for (const TransformKeyframe& point : clip->transformKeyframes) {
            if (point.sourceFrame == sourceFrame && point.value == value) {
                return true;
            }
        }
    }
    return m_project->execute(edits::setTransformKeyframe(clip->id, sourceFrame, value));
}

bool TimelineController::setTransformComponent(const QString& component, double value)
{
    const QVariantMap current = transformAutomation();
    VideoTransform transform{current.value(QStringLiteral("x")).toDouble(),
                             current.value(QStringLiteral("y")).toDouble(),
                             current.value(QStringLiteral("scale"), 1.0).toDouble(),
                             current.value(QStringLiteral("rotation")).toDouble()};
    if (component == QLatin1String("x")) {
        transform.x = value;
    } else if (component == QLatin1String("y")) {
        transform.y = value;
    } else if (component == QLatin1String("scale")) {
        transform.scale = value;
    } else if (component == QLatin1String("rotation")) {
        transform.rotation = value;
    } else {
        return false;
    }
    return setTransformAtPlayhead(transform.x, transform.y, transform.scale, transform.rotation);
}

bool TimelineController::addTransformKeyframeAtPlayhead()
{
    const QVariantMap current = transformAutomation();
    m_project->document()->breakMerge();
    const bool added =
        setTransformAtPlayhead(current.value(QStringLiteral("x")).toDouble(),
                               current.value(QStringLiteral("y")).toDouble(),
                               current.value(QStringLiteral("scale"), 1.0).toDouble(),
                               current.value(QStringLiteral("rotation")).toDouble());
    m_project->document()->breakMerge();
    return added;
}

bool TimelineController::removeTransformKeyframeAtPlayhead()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || !transformAutomation().value(QStringLiteral("canEdit")).toBool()) {
        emit statusMessage(
            QStringLiteral("Pause with the playhead inside an unlocked video or image clip "
                           "to remove its transform keyframe."));
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::removeTransformKeyframe(
        clip->id, clip->sourceIn + m_playback->positionFrames() - clip->start));
}

bool TimelineController::clearSelectedTransformKeyframes()
{
    const Clip* clip = singleSelectedClip();
    if (!clip || clip->transformKeyframes.empty()) {
        return false;
    }
    m_project->document()->breakMerge();
    return m_project->execute(edits::clearTransformKeyframes(clip->id));
}

void TimelineController::seekTransformKeyframe(bool forward)
{
    const Clip* clip = singleSelectedClip();
    if (!clip) {
        return;
    }
    std::optional<FrameCount> target;
    const FrameCount frame = m_playback->positionFrames();
    for (const TransformKeyframe& point : clip->transformKeyframes) {
        if (point.sourceFrame < clip->sourceIn || point.sourceFrame >= clip->sourceOut()) {
            continue;
        }
        const FrameCount candidate = clip->start + point.sourceFrame - clip->sourceIn;
        if ((forward && candidate > frame && (!target || candidate < *target)) ||
            (!forward && candidate < frame && (!target || candidate > *target))) {
            target = candidate;
        }
    }
    if (target) {
        m_playback->pause();
        m_project->document()->breakMerge();
        m_playback->seek(static_cast<double>(*target));
    }
}

void TimelineController::previousTransformKeyframe()
{
    seekTransformKeyframe(false);
}

void TimelineController::nextTransformKeyframe()
{
    seekTransformKeyframe(true);
}

void TimelineController::setTransformHandlesVisible(bool visible)
{
    if (m_transformHandlesVisible == visible) {
        return;
    }
    m_transformHandlesVisible = visible;
    emit transformHandlesVisibleChanged();
}

void TimelineController::endEdit()
{
    m_project->document()->breakMerge();
}

QVariantMap TimelineController::selectedTransition() const
{
    const Timeline& timeline = project().timeline();
    const Transition* tr = timeline.transition(m_selectedTransition);
    const Track* track = tr ? timeline.trackOfClip(tr->fromClip) : nullptr;
    const Clip* from = tr ? timeline.clip(tr->fromClip) : nullptr;
    const Clip* to = tr ? timeline.clip(tr->toClip) : nullptr;
    if (!tr || !track || !from || !to) {
        return {{QStringLiteral("count"), 0}};
    }
    const auto nameOf = [&](const Clip* clip) {
        const MediaAsset* asset = project().asset(clip->assetId);
        return asset ? asset->name : QStringLiteral("(missing media)");
    };
    const FrameRate& rate = project().settings().frameRate;
    const FrameCount longest = std::max(
        timeline.maxTransitionDuration(tr->fromClip, project().sourceLookup()), tr->duration);
    return {
        {QStringLiteral("count"), 1},
        {QStringLiteral("transitionId"), tr->id},
        {QStringLiteral("kind"), track->kind == TrackKind::Video ? QStringLiteral("Dissolve")
                                                                 : QStringLiteral("Crossfade")},
        {QStringLiteral("trackName"), track->name},
        {QStringLiteral("locked"), track->locked},
        {QStringLiteral("fromName"), nameOf(from)},
        {QStringLiteral("toName"), nameOf(to)},
        {QStringLiteral("duration"), static_cast<double>(tr->duration)},
        {QStringLiteral("durationText"), timecode::formatClock(tr->duration, rate)},
        {QStringLiteral("minDuration"), static_cast<double>(kMinTransitionFrames)},
        {QStringLiteral("maxDuration"), static_cast<double>(longest)},
        {QStringLiteral("cutText"), timecode::format(from->end(), rate)},
    };
}

bool TimelineController::addTransition(const QString& clipId)
{
    Id fromClip = clipId;
    if (fromClip.isEmpty() && m_selection.size() == 1) {
        fromClip = *m_selection.cbegin();
    }
    const Timeline& timeline = project().timeline();
    if (fromClip.isEmpty() || !timeline.clip(fromClip)) {
        emit statusMessage(
            QStringLiteral("Select the clip before a cut to add a transition after it."));
        return false;
    }
    if (const Transition* existing = timeline.transitionAfter(fromClip)) {
        selectTransition(existing->id);
        emit statusMessage(QStringLiteral("This cut already has a transition."));
        return false;
    }
    const FrameCount longest = timeline.maxTransitionDuration(fromClip, project().sourceLookup());
    if (longest < kMinTransitionFrames) {
        emit statusMessage(QStringLiteral(
            "A transition needs a next clip that starts exactly where this one ends, and unused "
            "media on both sides of the cut. Trim the clips to make room."));
        return false;
    }
    // One second by default, or as much as the media allows.
    const FrameCount length = std::min<FrameCount>(longest, nominalRate());
    const Id id = createId();
    if (!m_project->execute(edits::addTransition(fromClip, length, id))) {
        return false;
    }
    m_selection.clear();
    m_model.updateSelection(m_selection);
    emit selectionChanged();
    emit selectedClipChanged();
    setTransitionSelection(id);
    emit statusMessage(QStringLiteral("Added a %1 transition.")
                           .arg(timecode::formatClock(length, project().settings().frameRate)));
    return true;
}

void TimelineController::selectTransition(const QString& transitionId)
{
    if (!project().timeline().transition(transitionId)) {
        return;
    }
    if (!m_selection.isEmpty()) {
        m_selection.clear();
        m_model.updateSelection(m_selection);
        emit selectionChanged();
        emit selectedClipChanged();
    }
    setTransitionSelection(transitionId);
}

bool TimelineController::setTransitionDuration(const QString& transitionId, double frames)
{
    if (!validTimelineFrame(frames)) {
        return false;
    }
    return m_project->execute(
        edits::setTransitionDuration(transitionId, static_cast<FrameCount>(std::llround(frames))));
}

bool TimelineController::removeTransition(const QString& transitionId)
{
    if (!m_project->execute(edits::removeTransition(transitionId))) {
        return false;
    }
    setTransitionSelection({});
    return true;
}

bool TimelineController::addTrack(const QString& kind)
{
    const TrackKind trackKind =
        kind == QLatin1String("audio") ? TrackKind::Audio : TrackKind::Video;
    // New video tracks go above the existing ones; new audio tracks below.
    int index = -1;
    if (trackKind == TrackKind::Video) {
        const auto& tracks = project().timeline().tracks();
        int lastVideo = -1;
        for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
            if (tracks[static_cast<size_t>(i)].kind == TrackKind::Video) {
                lastVideo = i;
            }
        }
        index = lastVideo + 1;
    }
    return m_project->execute(
        edits::addTrack(trackKind, project().nextTrackName(trackKind), index));
}

bool TimelineController::canMoveTrack(const QString& trackId, int direction) const
{
    if (direction != -1 && direction != 1) {
        return false;
    }
    const Timeline& timeline = project().timeline();
    const int current = timeline.trackIndex(trackId);
    if (current < 0) {
        return false;
    }
    const int next = current + direction;
    if (next < 0 || next >= timeline.trackCount()) {
        return false;
    }
    const Track* source = timeline.track(trackId);
    const Track& target = timeline.tracks()[static_cast<size_t>(next)];
    return source && source->kind == target.kind;
}

bool TimelineController::moveTrack(const QString& trackId, int direction)
{
    if (!canMoveTrack(trackId, direction)) {
        emit statusMessage(QStringLiteral("Cannot move the track in that direction."));
        return false;
    }
    return m_project->execute(
        edits::moveTrack(trackId, project().timeline().trackIndex(trackId) + direction));
}

bool TimelineController::duplicateTrack(const QString& trackId)
{
    const Track* original = project().timeline().track(trackId);
    if (!original) {
        emit statusMessage(QStringLiteral("Choose an existing track to duplicate."));
        return false;
    }
    if (!m_project->execute(
            edits::duplicateTrack(trackId, project().nextTrackName(original->kind)))) {
        return false;
    }
    emit statusMessage(QStringLiteral("Duplicated track with its clips and transitions."));
    return true;
}

bool TimelineController::removeTrack(const QString& trackId)
{
    return m_project->execute(edits::removeTrack(trackId));
}

bool TimelineController::setTrackFlag(const QString& trackId, const QString& flag, bool value)
{
    if (flag == QLatin1String("muted")) {
        return m_project->execute(edits::setTrackMuted(trackId, value));
    }
    if (flag == QLatin1String("hidden")) {
        return m_project->execute(edits::setTrackHidden(trackId, value));
    }
    if (flag == QLatin1String("locked")) {
        return m_project->execute(edits::setTrackLocked(trackId, value));
    }
    return false;
}

bool TimelineController::setAllTracksLocked(bool locked)
{
    if (!m_project->execute(edits::setAllTracksLocked(locked))) {
        return false;
    }
    emit statusMessage(locked ? QStringLiteral("Locked all tracks.")
                              : QStringLiteral("Unlocked all tracks."));
    return true;
}

bool TimelineController::renameTrack(const QString& trackId, const QString& name)
{
    return m_project->execute(edits::renameTrack(trackId, name));
}

void TimelineController::previousEditPoint()
{
    const FrameCount position = m_playback->positionFrames();
    const std::vector<FrameCount> points = project().timeline().editPoints();
    const auto it = std::lower_bound(points.begin(), points.end(), position);
    if (it != points.begin()) {
        m_playback->seek(static_cast<double>(*(it - 1)));
    }
}

void TimelineController::nextEditPoint()
{
    const FrameCount position = m_playback->positionFrames();
    const std::vector<FrameCount> points = project().timeline().editPoints();
    const auto it = std::upper_bound(points.begin(), points.end(), position);
    if (it != points.end()) {
        m_playback->seek(static_cast<double>(*it));
    }
}

void TimelineController::zoomIn()
{
    setPixelsPerFrame(m_pixelsPerFrame * kZoomStep);
}

void TimelineController::zoomOut()
{
    setPixelsPerFrame(m_pixelsPerFrame / kZoomStep);
}

void TimelineController::zoomToFit(double viewWidth)
{
    const double frames = std::max(duration(), frameRate() * 10.0);
    setPixelsPerFrame(std::max(viewWidth - 40.0, 100.0) / frames);
}

} // namespace ft::ui
