// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "timeline/clip.h"

#include <QString>

#include <vector>

namespace ft {

enum class TrackKind { Video, Audio };

QString trackKindToString(TrackKind kind);
std::optional<TrackKind> trackKindFromString(const QString& text);

/// A dissolve (video tracks) or crossfade (audio tracks) between two adjacent clips on a track.
/// It is centred on the cut: before() frames of the outgoing clip and after() frames of the
/// incoming clip are blended, so the outgoing clip's media must continue after() frames past its
/// end and the incoming clip's media must start before() frames earlier ("handles").
struct Transition
{
    Id id;
    Id fromClip; ///< the clip that ends at the cut
    Id toClip;   ///< the clip that starts at the cut
    FrameCount duration = 0;

    [[nodiscard]] FrameCount before() const { return duration / 2; }
    [[nodiscard]] FrameCount after() const { return duration - duration / 2; }

    friend bool operator==(const Transition&, const Transition&) = default;
};

inline constexpr FrameCount kMinTransitionFrames = 2;

/// A horizontal lane of non-overlapping clips, kept sorted by start position.
struct Track
{
    Id id;
    TrackKind kind = TrackKind::Video;
    QString name;
    bool muted = false;  ///< silences the track's audio
    bool hidden = false; ///< removes the track's picture from the output
    bool locked = false; ///< prevents edits to the track's clips
    std::vector<Clip> clips;
    std::vector<Transition> transitions; ///< at most one per cut, in timeline order

    [[nodiscard]] FrameCount end() const { return clips.empty() ? 0 : clips.back().end(); }

    friend bool operator==(const Track&, const Track&) = default;
};

} // namespace ft
