// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/timeline_graph.h"

#include "core/logging.h"
#include "engine/mlt_runtime.h"
#include "engine/still_producer.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <mlt++/Mlt.h>

#include <cmath>

namespace ft::engine {

namespace {

constexpr double kSilenceDb = -90.0;

QString keyframes(std::initializer_list<std::pair<FrameCount, double>> points)
{
    QStringList parts;
    for (const auto& [frame, value] : points) {
        parts << QStringLiteral("%1=%2").arg(frame).arg(value, 0, 'g', 6);
    }
    return parts.join(QLatin1Char(';'));
}

/// Applies the clip's audio gain, mute and fades. @p offset is the number of extra frames the
/// cut starts before the clip's visible start (the handle of an incoming transition).
void attachAudioFilters(Mlt::Profile& profile, Mlt::Producer& cut, const Clip& clip,
                        FrameCount offset)
{
    const ClipProperties& p = clip.properties;
    const bool silent = p.muted || p.gain <= 0.0;
    if (!silent && p.gain == 1.0 && p.fadeIn == 0 && p.fadeOut == 0) {
        return;
    }
    if (!MltRuntime::capabilities().volumeFilter) {
        return;
    }
    Mlt::Filter volume(profile, "volume");
    if (!volume.is_valid()) {
        return;
    }
    volume.set_in_and_out(cut.get_in(), cut.get_out());
    if (silent) {
        volume.set("gain", 0.0);
    } else if (p.fadeIn == 0 && p.fadeOut == 0) {
        volume.set("gain", p.gain);
    } else {
        const double base = gainToDecibels(p.gain);
        const FrameCount first = offset;
        const FrameCount last = offset + clip.duration - 1;
        QStringList points;
        if (p.fadeIn > 0) {
            points << keyframes({{first, kSilenceDb}, {first + p.fadeIn, base}});
        } else {
            points << keyframes({{first, base}});
        }
        if (p.fadeOut > 0) {
            points << keyframes({{std::max<FrameCount>(last - p.fadeOut, first + p.fadeIn), base},
                                 {last, kSilenceDb}});
        } else {
            points << keyframes({{last, base}});
        }
        volume.set("level", points.join(QLatin1Char(';')).toUtf8().constData());
    }
    cut.attach(volume);
}

/// A second gain stage keeps automation independent of the clip's overall volume and fades.
/// Filter animation positions are relative to filter-in, including transition source handles.
Status attachAudioKeyframes(Mlt::Profile& profile, Mlt::Producer& cut, const Clip& clip)
{
    if (clip.audioKeyframes.empty()) {
        return {};
    }
    if (!MltRuntime::capabilities().volumeFilter) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("Volume keyframes need the audio volume filter."));
    }
    Mlt::Filter volume(profile, "volume");
    if (!volume.is_valid()) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("Could not prepare the volume keyframe filter."));
    }
    volume.set_in_and_out(cut.get_in(), cut.get_out());
    const FrameCount first = cut.get_in();
    const FrameCount last = cut.get_out();
    QStringList points;
    points << keyframes({{0, audioKeyframeDecibels(clip.audioKeyframes, first)}});
    for (const AudioKeyframe& point : clip.audioKeyframes) {
        if (point.sourceFrame > first && point.sourceFrame < last) {
            points << keyframes({{point.sourceFrame - first, point.decibels}});
        }
    }
    if (last > first) {
        points << keyframes({{last - first, audioKeyframeDecibels(clip.audioKeyframes, last)}});
    }
    volume.set("level", points.join(QLatin1Char(';')).toUtf8().constData());
    cut.attach(volume);
    return {};
}

/// Multiply source alpha after scalar opacity/fades; never replace the image's own alpha.
Status attachOpacityKeyframes(Mlt::Profile& profile, Mlt::Producer& cut, const Clip& clip)
{
    if (clip.opacityKeyframes.empty()) {
        return {};
    }
    if (!MltRuntime::capabilities().brightnessFilter) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("Opacity keyframes need the video alpha filter."));
    }
    Mlt::Filter alpha(profile, "brightness");
    if (!alpha.is_valid()) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("Could not prepare the opacity keyframe filter."));
    }
    alpha.set_in_and_out(cut.get_in(), cut.get_out());
    alpha.set("level", 1.0);
    const FrameCount first = cut.get_in();
    const FrameCount last = cut.get_out();
    QStringList points;
    points << keyframes({{0, opacityKeyframeValue(clip.opacityKeyframes, first)}});
    for (const OpacityKeyframe& point : clip.opacityKeyframes) {
        if (point.sourceFrame > first && point.sourceFrame < last) {
            points << keyframes({{point.sourceFrame - first, point.opacity}});
        }
    }
    if (last > first) {
        points << keyframes({{last - first, opacityKeyframeValue(clip.opacityKeyframes, last)}});
    }
    alpha.set("alpha", points.join(QLatin1Char(';')).toUtf8().constData());
    cut.attach(alpha);
    return {};
}

/// Applies video fades, opacity and picture adjustments (see attachAudioFilters for @p offset).
void attachVideoFilters(Mlt::Profile& profile, Mlt::Producer& cut, const Clip& clip,
                        FrameCount offset)
{
    const ClipProperties& p = clip.properties;
    const auto& caps = MltRuntime::capabilities();
    const VideoAdjustments& v = p.video;
    if (caps.eqFilter && (v.brightness != 0.0 || v.contrast != 1.0 || v.saturation != 1.0)) {
        Mlt::Filter eq(profile, "avfilter.eq");
        if (eq.is_valid()) {
            eq.set_in_and_out(cut.get_in(), cut.get_out());
            eq.set("av.brightness", v.brightness);
            eq.set("av.contrast", v.contrast);
            eq.set("av.saturation", v.saturation);
            cut.attach(eq);
        }
    }
    if (caps.brightnessFilter && (p.fadeIn > 0 || p.fadeOut > 0 || v.opacity < 1.0)) {
        // Fades and opacity are applied to the alpha channel so that a fading clip reveals the
        // tracks below it; over the black background this is the usual fade to black.
        Mlt::Filter alpha(profile, "brightness");
        if (alpha.is_valid()) {
            alpha.set_in_and_out(cut.get_in(), cut.get_out());
            alpha.set("level", 1.0);
            const FrameCount first = offset;
            const FrameCount last = offset + clip.duration - 1;
            const double full = v.opacity;
            QStringList points;
            points << (p.fadeIn > 0 ? keyframes({{first, 0.0}, {first + p.fadeIn, full}})
                                    : keyframes({{first, full}}));
            points << (p.fadeOut > 0
                           ? keyframes(
                                 {{std::max<FrameCount>(last - p.fadeOut, first + p.fadeIn), full},
                                  {last, 0.0}})
                           : keyframes({{last, full}}));
            alpha.set("alpha", points.join(QLatin1Char(';')).toUtf8().constData());
            cut.attach(alpha);
        }
    }
}

} // namespace

double gainToDecibels(double gain)
{
    if (gain <= 0.0) {
        return kSilenceDb;
    }
    return std::max(kSilenceDb, 20.0 * std::log10(gain));
}

struct TimelineGraphBuilder::CacheEntry
{
    QString path;
    qint64 size = 0;
    QDateTime modified;
    std::shared_ptr<Mlt::Producer> producer;
};

TimelineGraphBuilder::TimelineGraphBuilder(Mlt::Profile& profile)
    : m_profile(profile)
{}

TimelineGraphBuilder::~TimelineGraphBuilder() = default;

void TimelineGraphBuilder::clearCache()
{
    m_cache.clear();
}

std::unique_ptr<Mlt::Producer> TimelineGraphBuilder::openProducer(Mlt::Profile& profile,
                                                                  const QString& path,
                                                                  MediaKind kind, bool audioOnly)
{
    if (kind == MediaKind::Image) {
        return createStillProducer(profile, path);
    }
    // The "avformat:" prefix makes the loader use the FFmpeg producer no matter how the file is
    // named (a crafted name can never select another MLT service), while still adding the
    // normalising filters (scaling, padding, format conversion) that consumers expect.
    const QByteArray resource = QByteArrayLiteral("avformat:") + path.toUtf8();
    auto producer = std::make_unique<Mlt::Producer>(profile, resource.constData());
    if (!producer->is_valid()) {
        return nullptr;
    }
    if (audioOnly) {
        producer->set("video_index", -1);
    }
    return producer;
}

std::shared_ptr<Mlt::Producer> TimelineGraphBuilder::producerFor(const MediaAsset& asset,
                                                                 const Id& trackId, bool audioOnly,
                                                                 QStringList& warnings)
{
    const QString key = asset.id + QLatin1Char('|') + trackId +
                        (audioOnly ? QStringLiteral("|a") : QStringLiteral("|av"));
    const QFileInfo info(asset.filePath);
    if (const auto it = m_cache.constFind(key); it != m_cache.constEnd()) {
        const CacheEntry& entry = **it;
        if (entry.path == asset.filePath && entry.size == info.size() &&
            entry.modified == info.lastModified()) {
            return entry.producer;
        }
    }
    std::shared_ptr<Mlt::Producer> producer =
        openProducer(m_profile, asset.filePath, asset.info.kind, audioOnly);
    if (!producer) {
        warnings << QStringLiteral("Could not open \"%1\".").arg(asset.name);
        return nullptr;
    }
    auto entry = std::make_shared<CacheEntry>();
    entry->path = asset.filePath;
    entry->size = info.size();
    entry->modified = info.lastModified();
    entry->producer = producer;
    m_cache.insert(key, entry);
    return producer;
}

std::unique_ptr<Mlt::Producer> TimelineGraphBuilder::placeholder(TrackKind kind, FrameCount length)
{
    // Offline media is shown as a dark red frame and is silent.
    auto producer = std::make_unique<Mlt::Producer>(
        m_profile, "color", kind == TrackKind::Video ? "0x6b1d1dff" : "0x00000000");
    producer->set("length", static_cast<int>(length));
    producer->set_in_and_out(0, static_cast<int>(length) - 1);
    producer->set("ft.placeholder", 1);
    return producer;
}

Result<GraphBuildResult> TimelineGraphBuilder::build(const Project& project,
                                                     const GraphOptions& options)
{
    if (!MltRuntime::isInitialized()) {
        return makeError(ErrorCode::Engine, QStringLiteral("The media engine is not initialised."));
    }
    const EngineCapabilities& caps = MltRuntime::capabilities();
    GraphBuildResult result;
    const Timeline& timeline = project.timeline();
    result.duration = timeline.duration();
    const FrameCount total = std::max<FrameCount>(result.duration, 1);
    if (total > std::numeric_limits<int>::max() / 2) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The sequence is too long to play."));
    }
    const int totalFrames = static_cast<int>(total);

    auto tractor = std::make_unique<Mlt::Tractor>(m_profile);
    Mlt::Producer background(m_profile, "color", "0x000000ff");
    if (!background.is_valid()) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("The media engine could not create the background."));
    }
    background.set("length", totalFrames);
    background.set_in_and_out(0, totalFrames - 1);
    Mlt::Playlist backgroundTrack(m_profile);
    backgroundTrack.append(background, 0, totalFrames - 1);
    tractor->set_track(backgroundTrack, 0);

    std::unique_ptr<Mlt::Field> field(tractor->field());
    QSet<QString> usedKeys;
    int mltTrack = 1;
    for (const Track& track : timeline.tracks()) {
        Mlt::Playlist playlist(m_profile);
        playlist.set("ft.track", track.id.toUtf8().constData());

        // Transitions overlap the clips around a cut: the outgoing clip continues after() frames
        // past its end and the incoming clip starts before() frames early. The extended clips are
        // laid out one after another and then blended with MLT's playlist mixes, which shortens
        // the playlist back to the timeline's length.
        QHash<Id, FrameCount> extendBefore;
        QHash<Id, FrameCount> extendAfter;
        QHash<Id, const Transition*> incoming;
        for (const Transition& tr : track.transitions) {
            extendAfter.insert(tr.fromClip, tr.after());
            extendBefore.insert(tr.toClip, tr.before());
            incoming.insert(tr.toClip, &tr);
        }
        struct PendingMix
        {
            int entry = 0; ///< playlist index of the outgoing clip
            int length = 0;
        };
        std::vector<PendingMix> mixes;
        QHash<Id, int> entryOf;
        FrameCount cursor = 0; // position in the playlist before mixing
        FrameCount shift = 0;  // frames of overlap added by the transitions placed so far
        for (const Clip& clip : track.clips) {
            const FrameCount before = extendBefore.value(clip.id, 0);
            const FrameCount after = extendAfter.value(clip.id, 0);
            const Transition* in = incoming.value(clip.id, nullptr);
            if (in) {
                shift += in->duration;
            }
            const FrameCount position = clip.start - before + shift;
            if (position > cursor) {
                playlist.blank(static_cast<int>(position - cursor - 1));
            }
            const MediaAsset* asset = project.asset(clip.assetId);
            const bool missing = !asset || asset->status == MediaStatus::Missing ||
                                 !QFileInfo::exists(asset->filePath);
            std::shared_ptr<Mlt::Producer> source;
            if (!missing) {
                source =
                    producerFor(*asset, track.id, track.kind == TrackKind::Audio, result.warnings);
                usedKeys.insert(asset->id + QLatin1Char('|') + track.id);
            }
            const QString assetName = asset ? asset->name : clip.assetId;
            const FrameCount length = clip.duration + before + after;
            if (!source) {
                if (!options.placeholdersForMissingMedia) {
                    return makeError(
                        ErrorCode::Media,
                        QStringLiteral("The media file \"%1\" is missing or cannot be opened.")
                            .arg(assetName));
                }
                if (missing) {
                    result.warnings << QStringLiteral("\"%1\" is offline.").arg(assetName);
                }
                std::unique_ptr<Mlt::Producer> stand = placeholder(track.kind, length);
                playlist.append(*stand, 0, static_cast<int>(length) - 1);
            } else {
                std::unique_ptr<Mlt::Producer> cut(
                    source->cut(static_cast<int>(clip.sourceIn - before),
                                static_cast<int>(clip.sourceOut() - 1 + after)));
                if (!cut || !cut->is_valid()) {
                    return makeError(
                        ErrorCode::Engine,
                        QStringLiteral("Could not prepare clip \"%1\".").arg(assetName));
                }
                cut->set("ft.clip", clip.id.toUtf8().constData());
                attachAudioFilters(m_profile, *cut, clip, before);
                if (const Status automation = attachAudioKeyframes(m_profile, *cut, clip);
                    !automation) {
                    return automation.error();
                }
                if (track.kind == TrackKind::Video) {
                    attachVideoFilters(m_profile, *cut, clip, before);
                    if (const Status opacity = attachOpacityKeyframes(m_profile, *cut, clip);
                        !opacity) {
                        return opacity.error();
                    }
                }
                if (before > 0 || after > 0) {
                    // Mixing cuts the clip again from its source; wrapping it keeps the clip's
                    // filters (volume, fades, picture) active inside the transition.
                    Mlt::Playlist wrapper(m_profile);
                    wrapper.append(*cut);
                    playlist.append(wrapper);
                } else {
                    playlist.append(*cut);
                }
            }
            entryOf.insert(clip.id, playlist.count() - 1);
            if (in) {
                mixes.push_back({entryOf.value(in->fromClip), static_cast<int>(in->duration)});
            }
            cursor = position + length;
        }
        // Mix from the end so that earlier playlist indices stay valid.
        std::sort(mixes.begin(), mixes.end(),
                  [](const PendingMix& a, const PendingMix& b) { return a.entry > b.entry; });
        for (const PendingMix& mix : mixes) {
            if (playlist.mix(mix.entry, mix.length) != 0) {
                return makeError(ErrorCode::Engine,
                                 QStringLiteral("Could not prepare a transition."));
            }
            const int region = mix.entry + 1;
            if (track.kind == TrackKind::Video) {
                Mlt::Transition dissolve(m_profile, "luma");
                if (dissolve.is_valid()) {
                    playlist.mix_add(region, &dissolve);
                }
            }
            Mlt::Transition crossfade(m_profile, "mix");
            if (crossfade.is_valid()) {
                crossfade.set("start", 0.0);
                crossfade.set("end", 1.0);
                playlist.mix_add(region, &crossfade);
            }
        }

        int hide = 0;
        if (track.kind == TrackKind::Audio || track.hidden) {
            hide |= 1;
        }
        if (track.muted) {
            hide |= 2;
        }
        tractor->set_track(playlist, mltTrack);
        std::unique_ptr<Mlt::Producer> trackProducer(tractor->track(mltTrack));
        if (trackProducer) {
            trackProducer->set("hide", hide);
        }

        Mlt::Transition mix(m_profile, "mix");
        mix.set("always_active", 1);
        mix.set("sum", 1);
        field->plant_transition(mix, 0, mltTrack);

        if (track.kind == TrackKind::Video) {
            Mlt::Transition composite(m_profile, caps.compositor.toUtf8().constData());
            composite.set("always_active", 1);
            composite.set("fill", 1);
            composite.set("distort", 0);
            composite.set("rect", "0 0 100% 100% 1");
            field->plant_transition(composite, 0, mltTrack);
        }
        ++mltTrack;
    }

    // Drop cached producers for clips that no longer exist.
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        const QString key = it.key();
        const QString shortKey = key.section(QLatin1Char('|'), 0, 1);
        if (!usedKeys.contains(shortKey)) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }

    tractor->set("ft.duration", totalFrames);
    result.tractor = std::move(tractor);
    return result;
}

} // namespace ft::engine
