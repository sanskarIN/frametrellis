// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"

#include <QHash>
#include <QStringList>

#include <memory>

namespace Mlt {
class Producer;
class Profile;
class Tractor;
} // namespace Mlt

namespace ft::engine {

struct GraphOptions
{
    /// Replace missing media with a visible placeholder (preview). When false, missing media is
    /// an error (export).
    bool placeholdersForMissingMedia = true;
};

struct GraphBuildResult
{
    std::unique_ptr<Mlt::Tractor> tractor;
    FrameCount duration = 0; ///< frames of real content (the tractor is at least 1 frame long)
    QStringList warnings;
};

/// Translates a Project into an MLT tractor:
///
///   track 0              black background covering the whole sequence
///   tracks 1..n          one playlist per project track (clips as producer cuts, gaps as blanks)
///   video transitions    each video track is composited over the result of the tracks below
///   audio transitions    every track is mixed into track 0
///
/// Track mute/hide map to MLT's per-track "hide" flags; clip gain, fades, opacity and picture
/// adjustments become filters on the clip cut. Source producers are cached per asset and track so
/// rebuilding after an edit does not reopen files.
///
/// Not thread-safe: use one builder per thread, together with the objects it produces.
class TimelineGraphBuilder
{
public:
    explicit TimelineGraphBuilder(Mlt::Profile& profile);
    ~TimelineGraphBuilder();
    TimelineGraphBuilder(const TimelineGraphBuilder&) = delete;
    TimelineGraphBuilder& operator=(const TimelineGraphBuilder&) = delete;

    Result<GraphBuildResult> build(const Project& project, const GraphOptions& options = {});
    void clearCache();
    [[nodiscard]] int cachedProducerCount() const { return static_cast<int>(m_cache.size()); }

    /// Opens a standalone producer for a media file. Stills use FrameTrellis' Qt-based image
    /// producer; everything else uses MLT's FFmpeg producer.
    static std::unique_ptr<Mlt::Producer> openProducer(Mlt::Profile& profile, const QString& path,
                                                       MediaKind kind, bool audioOnly);

private:
    struct CacheEntry;
    std::shared_ptr<Mlt::Producer> producerFor(const MediaAsset& asset, const Id& trackId,
                                               bool audioOnly, QStringList& warnings);
    std::unique_ptr<Mlt::Producer> placeholder(TrackKind kind, FrameCount length);

    Mlt::Profile& m_profile;
    QHash<QString, std::shared_ptr<CacheEntry>> m_cache;
};

/// Linear gain to decibels, clamped to a practical floor for silence.
double gainToDecibels(double gain);

} // namespace ft::engine
