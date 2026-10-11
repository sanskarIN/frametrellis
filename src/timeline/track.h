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

    [[nodiscard]] FrameCount end() const { return clips.empty() ? 0 : clips.back().end(); }

    friend bool operator==(const Track&, const Track&) = default;
};

} // namespace ft
