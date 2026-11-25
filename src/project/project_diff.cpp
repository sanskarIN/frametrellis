// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_diff.h"

#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <utility>

namespace ft::project_diff {

namespace {

struct TrackEntry
{
    const Track* track = nullptr;
    int index = 0;
};

struct ClipEntry
{
    const Track* track = nullptr;
    const Clip* clip = nullptr;
    const MediaAsset* media = nullptr;
};

struct TransitionEntry
{
    const Track* track = nullptr;
    const Transition* transition = nullptr;
};

struct Index
{
    QHash<Id, const MediaAsset*> media;
    QHash<Id, TrackEntry> tracks;
    QHash<Id, ClipEntry> clips;
    QHash<Id, TransitionEntry> transitions;
    QHash<Id, const TimelineMarker*> markers;
    QHash<Id, const SubtitleCue*> subtitles;
};

Index indexProject(const Project& project)
{
    Index index;
    for (const MediaAsset& asset : project.media()) {
        index.media.insert(asset.id, &asset);
    }
    int trackIndex = 0;
    for (const Track& track : project.timeline().tracks()) {
        index.tracks.insert(track.id, TrackEntry{&track, trackIndex++});
        for (const Clip& clip : track.clips) {
            index.clips.insert(clip.id, ClipEntry{&track, &clip, index.media.value(clip.assetId)});
        }
        for (const Transition& transition : track.transitions) {
            index.transitions.insert(transition.id, TransitionEntry{&track, &transition});
        }
    }
    for (const TimelineMarker& marker : project.markers()) {
        index.markers.insert(marker.id, &marker);
    }
    for (const SubtitleCue& cue : project.subtitles().cues) {
        index.subtitles.insert(cue.id, &cue);
    }
    return index;
}

Counts& countsFor(Report& report, Category category)
{
    return report.counts[static_cast<size_t>(category)];
}

void addChange(Report& report, Detail detail)
{
    Counts& counts = countsFor(report, detail.category);
    switch (detail.action) {
    case Action::Added:
        ++counts.added;
        break;
    case Action::Removed:
        ++counts.removed;
        break;
    case Action::Changed:
        ++counts.changed;
        break;
    }
    ++report.totalChanges;
    if (report.details.size() < static_cast<size_t>(kMaxComparisonDetails)) {
        report.details.push_back(std::move(detail));
    }
}

void addSingleton(Report& report, Category category, const QString& label, QStringList fields,
                  const QString& before, const QString& after)
{
    if (fields.isEmpty()) {
        ++countsFor(report, category).unchanged;
        return;
    }
    addChange(report,
              Detail{category, Action::Changed, {}, label, std::move(fields), before, after});
}

void field(QStringList& fields, bool changed, const QString& name)
{
    if (changed) {
        fields.append(name);
    }
}

// Stable-ID matching avoids treating a rename or track move as deletion and
// recreation. Iterate sorted IDs so truncated detail samples are reproducible.
template <typename Item, typename Fields, typename Label, typename Display>
void compareItems(Report& report, Category category, const QHash<Id, Item>& previous,
                  const QHash<Id, Item>& current, Fields changedFields, Label label,
                  Display display)
{
    QList<Id> ids = previous.keys();
    ids.reserve(previous.size() + current.size());
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (!previous.contains(it.key())) {
            ids.append(it.key());
        }
    }
    std::sort(ids.begin(), ids.end());
    for (const Id& id : ids) {
        const auto old = previous.constFind(id);
        const auto now = current.constFind(id);
        if (old == previous.cend()) {
            addChange(report,
                      Detail{category, Action::Added, id, label(*now), {}, {}, display(*now)});
        } else if (now == current.cend()) {
            addChange(report,
                      Detail{category, Action::Removed, id, label(*old), {}, display(*old), {}});
        } else {
            QStringList fields = changedFields(*old, *now);
            if (fields.isEmpty()) {
                ++countsFor(report, category).unchanged;
            } else {
                addChange(report, Detail{category, Action::Changed, id, label(*now),
                                         std::move(fields), display(*old), display(*now)});
            }
        }
    }
}

QStringList settingsFields(const ProjectSettings& old, const ProjectSettings& now)
{
    QStringList fields;
    field(fields, old.width != now.width || old.height != now.height, QStringLiteral("Resolution"));
    field(fields, old.frameRate != now.frameRate, QStringLiteral("Frame rate"));
    field(fields, old.sampleRate != now.sampleRate, QStringLiteral("Audio sample rate"));
    field(fields, old.audioChannels != now.audioChannels, QStringLiteral("Audio channels"));
    return fields;
}

QStringList mediaFields(const MediaAsset* old, const MediaAsset* now)
{
    QStringList fields;
    field(fields, old->name != now->name, QStringLiteral("Display name"));
    field(fields, old->filePath != now->filePath, QStringLiteral("Source location"));
    const MediaInfo& a = old->info;
    const MediaInfo& b = now->info;
    field(fields, a.kind != b.kind, QStringLiteral("Media kind"));
    field(fields, a.hasAudio != b.hasAudio || a.hasVideo != b.hasVideo,
          QStringLiteral("Stream availability"));
    field(fields, a.durationUs != b.durationUs, QStringLiteral("Source duration"));
    field(fields, a.width != b.width || a.height != b.height, QStringLiteral("Source dimensions"));
    field(fields, a.frameRate != b.frameRate, QStringLiteral("Native frame rate"));
    field(fields, a.audioChannels != b.audioChannels || a.audioSampleRate != b.audioSampleRate,
          QStringLiteral("Audio format"));
    field(fields,
          a.container != b.container || a.videoCodec != b.videoCodec ||
              a.audioCodec != b.audioCodec,
          QStringLiteral("Container or codec"));
    field(fields, a.fileSize != b.fileSize || a.lastModified != b.lastModified,
          QStringLiteral("Stored file fingerprint"));
    // Runtime MediaStatus is deliberately not a project edit.
    return fields;
}

QString mediaDisplay(const MediaAsset* asset)
{
    return QStringLiteral("%1 · %2").arg(asset->name, mediaKindToString(asset->info.kind));
}

QStringList trackFields(const TrackEntry& old, const TrackEntry& now)
{
    QStringList fields;
    field(fields, old.track->name != now.track->name, QStringLiteral("Name"));
    field(fields, old.track->kind != now.track->kind, QStringLiteral("Track type"));
    field(fields, old.index != now.index, QStringLiteral("Track order"));
    field(fields, old.track->hidden != now.track->hidden, QStringLiteral("Picture visibility"));
    field(fields, old.track->muted != now.track->muted, QStringLiteral("Audio mute"));
    field(fields, old.track->locked != now.track->locked, QStringLiteral("Edit lock"));
    // Child clips and transitions have their own independent stable identities.
    return fields;
}

QString trackDisplay(const TrackEntry& entry)
{
    return QStringLiteral("%1 · %2 · #%3")
        .arg(entry.track->name, trackKindToString(entry.track->kind))
        .arg(entry.index + 1);
}

QStringList clipFields(const ClipEntry& old, const ClipEntry& now)
{
    QStringList fields;
    const Clip& a = *old.clip;
    const Clip& b = *now.clip;
    field(fields, old.track->id != now.track->id, QStringLiteral("Destination track"));
    field(fields, a.assetId != b.assetId, QStringLiteral("Source media"));
    field(fields, a.start != b.start, QStringLiteral("Timeline start"));
    field(fields, a.duration != b.duration, QStringLiteral("Duration"));
    field(fields, a.sourceIn != b.sourceIn, QStringLiteral("Source in"));
    field(fields, a.properties.gain != b.properties.gain, QStringLiteral("Audio gain"));
    field(fields, a.properties.balance != b.properties.balance, QStringLiteral("Stereo balance"));
    field(fields, a.properties.muted != b.properties.muted, QStringLiteral("Clip mute"));
    field(fields,
          a.properties.fadeIn != b.properties.fadeIn ||
              a.properties.fadeOut != b.properties.fadeOut,
          QStringLiteral("Fades"));
    field(fields, a.properties.video != b.properties.video, QStringLiteral("Picture adjustments"));
    field(fields, a.audioKeyframes != b.audioKeyframes, QStringLiteral("Volume keyframes"));
    field(fields, a.opacityKeyframes != b.opacityKeyframes, QStringLiteral("Opacity keyframes"));
    field(fields, a.transformKeyframes != b.transformKeyframes,
          QStringLiteral("Transform keyframes"));
    return fields;
}

QString clipDisplay(const ClipEntry& entry)
{
    return QStringLiteral("%1 · [%2, %3) · source [%4, %5)")
        .arg(entry.track->name)
        .arg(entry.clip->start)
        .arg(entry.clip->end())
        .arg(entry.clip->sourceIn)
        .arg(entry.clip->sourceOut());
}

QString clipLabel(const ClipEntry& entry)
{
    return entry.media ? entry.media->name : entry.clip->id;
}

QStringList transitionFields(const TransitionEntry& old, const TransitionEntry& now)
{
    QStringList fields;
    field(fields, old.track->id != now.track->id, QStringLiteral("Track"));
    field(fields,
          old.transition->fromClip != now.transition->fromClip ||
              old.transition->toClip != now.transition->toClip,
          QStringLiteral("Cut endpoints"));
    field(fields, old.transition->duration != now.transition->duration, QStringLiteral("Duration"));
    return fields;
}

QString transitionDisplay(const TransitionEntry& entry)
{
    return QStringLiteral("%1 · %2 frames").arg(entry.track->name).arg(entry.transition->duration);
}

QStringList markerFields(const TimelineMarker* old, const TimelineMarker* now)
{
    QStringList fields;
    field(fields, old->frame != now->frame, QStringLiteral("Sequence frame"));
    field(fields, old->label != now->label, QStringLiteral("Label"));
    return fields;
}

QString markerDisplay(const TimelineMarker* marker)
{
    return QStringLiteral("%1 · frame %2").arg(marker->label).arg(marker->frame);
}

QStringList subtitleFields(const SubtitleCue* old, const SubtitleCue* now)
{
    QStringList fields;
    field(fields, old->startMs != now->startMs || old->endMs != now->endMs,
          QStringLiteral("Cue timing"));
    field(fields, old->text != now->text, QStringLiteral("Cue text"));
    return fields;
}

QString subtitleDisplay(const SubtitleCue* cue)
{
    // Text changes are flagged, but caption contents are not copied into the
    // comparison detail model. Only the exact time interval is presented.
    return QStringLiteral("%1 – %2").arg(formatSubtitleTime(cue->startMs),
                                         formatSubtitleTime(cue->endMs));
}

QString subtitleStyleDisplay(const SubtitleTrack& track)
{
    return QStringLiteral("Burn-in %1 · Font %2% · Background %3")
        .arg(track.burnIn ? QStringLiteral("on") : QStringLiteral("off"))
        .arg(track.fontPercent)
        .arg(track.background ? QStringLiteral("on") : QStringLiteral("off"));
}

QJsonObject countsJson(Category category, const Counts& counts)
{
    return {{QStringLiteral("code"), categoryCode(category)},
            {QStringLiteral("title"), categoryTitle(category)},
            {QStringLiteral("added"), counts.added},
            {QStringLiteral("removed"), counts.removed},
            {QStringLiteral("changed"), counts.changed},
            {QStringLiteral("unchanged"), counts.unchanged},
            {QStringLiteral("totalChanges"), counts.totalChanges()}};
}

} // namespace

QString categoryCode(Category category)
{
    switch (category) {
    case Category::Project:
        return QStringLiteral("project");
    case Category::Settings:
        return QStringLiteral("settings");
    case Category::Media:
        return QStringLiteral("media");
    case Category::Track:
        return QStringLiteral("track");
    case Category::Clip:
        return QStringLiteral("clip");
    case Category::Transition:
        return QStringLiteral("transition");
    case Category::Marker:
        return QStringLiteral("marker");
    case Category::Subtitle:
        return QStringLiteral("subtitle");
    case Category::SubtitleStyle:
        return QStringLiteral("subtitle-style");
    case Category::Count:
        break;
    }
    return {};
}

QString categoryTitle(Category category)
{
    switch (category) {
    case Category::Project:
        return QStringLiteral("Project");
    case Category::Settings:
        return QStringLiteral("Sequence settings");
    case Category::Media:
        return QStringLiteral("Media");
    case Category::Track:
        return QStringLiteral("Tracks");
    case Category::Clip:
        return QStringLiteral("Clips");
    case Category::Transition:
        return QStringLiteral("Transitions");
    case Category::Marker:
        return QStringLiteral("Markers");
    case Category::Subtitle:
        return QStringLiteral("Subtitle cues");
    case Category::SubtitleStyle:
        return QStringLiteral("Subtitle style");
    case Category::Count:
        break;
    }
    return {};
}

QString actionCode(Action action)
{
    switch (action) {
    case Action::Added:
        return QStringLiteral("added");
    case Action::Removed:
        return QStringLiteral("removed");
    case Action::Changed:
        return QStringLiteral("changed");
    }
    return {};
}

Result<Report> compare(const Project& previous, const Project& current)
{
    if (previous.id() != current.id()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Checkpoints can only be compared with the same project."));
    }
    if (const Status valid = previous.validate(); !valid) {
        return valid.error();
    }
    if (const Status valid = current.validate(); !valid) {
        return valid.error();
    }

    Report report;
    report.projectId = current.id();
    report.previousName = previous.name();
    report.currentName = current.name();
    report.previousDuration = previous.timeline().duration();
    report.currentDuration = current.timeline().duration();

    QStringList projectFields;
    field(projectFields, previous.name() != current.name(), QStringLiteral("Project name"));
    addSingleton(report, Category::Project, QStringLiteral("Project"), std::move(projectFields),
                 previous.name(), current.name());
    addSingleton(report, Category::Settings, QStringLiteral("Sequence"),
                 settingsFields(previous.settings(), current.settings()),
                 previous.settings().summary(), current.settings().summary());

    const Index old = indexProject(previous);
    const Index now = indexProject(current);
    compareItems(
        report, Category::Media, old.media, now.media, mediaFields,
        [](const MediaAsset* asset) { return asset->name; }, mediaDisplay);
    compareItems(
        report, Category::Track, old.tracks, now.tracks, trackFields,
        [](const TrackEntry& entry) { return entry.track->name; }, trackDisplay);
    compareItems(report, Category::Clip, old.clips, now.clips, clipFields, clipLabel, clipDisplay);
    compareItems(
        report, Category::Transition, old.transitions, now.transitions, transitionFields,
        [](const TransitionEntry&) { return QStringLiteral("Transition"); }, transitionDisplay);
    compareItems(
        report, Category::Marker, old.markers, now.markers, markerFields,
        [](const TimelineMarker* marker) { return marker->label; }, markerDisplay);
    compareItems(
        report, Category::Subtitle, old.subtitles, now.subtitles, subtitleFields,
        [](const SubtitleCue*) { return QStringLiteral("Subtitle cue"); }, subtitleDisplay);

    const auto& a = previous.subtitles();
    const auto& b = current.subtitles();
    QStringList styleFields;
    field(styleFields, a.burnIn != b.burnIn, QStringLiteral("Burn-in"));
    field(styleFields, a.fontPercent != b.fontPercent, QStringLiteral("Font size"));
    field(styleFields, a.background != b.background, QStringLiteral("Background"));
    addSingleton(report, Category::SubtitleStyle, QStringLiteral("Subtitle style"),
                 std::move(styleFields), subtitleStyleDisplay(a), subtitleStyleDisplay(b));
    return report;
}

QJsonObject toJson(const Report& report)
{
    QJsonArray categories;
    for (size_t i = 0; i < report.counts.size(); ++i) {
        categories.append(countsJson(static_cast<Category>(i), report.counts[i]));
    }
    QJsonArray details;
    for (const Detail& detail : report.details) {
        details.append(QJsonObject{
            {QStringLiteral("category"), categoryCode(detail.category)},
            {QStringLiteral("categoryTitle"), categoryTitle(detail.category)},
            {QStringLiteral("action"), actionCode(detail.action)},
            {QStringLiteral("id"), detail.id},
            {QStringLiteral("label"), detail.label},
            {QStringLiteral("fields"), QJsonArray::fromStringList(detail.fields)},
            {QStringLiteral("before"), detail.before},
            {QStringLiteral("after"), detail.after},
        });
    }
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("projectId"), report.projectId},
        {QStringLiteral("previousName"), report.previousName},
        {QStringLiteral("currentName"), report.currentName},
        {QStringLiteral("previousDuration"), static_cast<double>(report.previousDuration)},
        {QStringLiteral("currentDuration"), static_cast<double>(report.currentDuration)},
        {QStringLiteral("totalChanges"), report.totalChanges},
        {QStringLiteral("categories"), categories},
        {QStringLiteral("details"), details},
        {QStringLiteral("omittedDetails"), report.totalChanges - static_cast<int>(details.size())},
    };
}

} // namespace ft::project_diff
