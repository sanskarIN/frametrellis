// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/ids.h"
#include "core/result.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace ft {

/// Sequence-level plain text with exact SRT milliseconds and an exclusive end.
/// Cues do not follow clip edits. A single subtitle track does not permit overlaps.
struct SubtitleCue
{
    Id id;
    std::int64_t startMs = 0;
    std::int64_t endMs = 2000;
    QString text;
    friend bool operator==(const SubtitleCue&, const SubtitleCue&) = default;
};

struct SubtitleTrack
{
    std::vector<SubtitleCue> cues;
    bool burnIn = true;     ///< affects both preview and rendered video, not SRT export
    int fontPercent = 5;    ///< preferred font height as a percentage of the project canvas
    bool background = true; ///< translucent black plate behind white text
    friend bool operator==(const SubtitleTrack&, const SubtitleTrack&) = default;
};

inline constexpr int kMaxSubtitleCues = 10'000;
inline constexpr int kMaxSubtitleTextLength = 2000;
inline constexpr int kMaxSubtitleTotalTextLength = 1'000'000;
inline constexpr std::int64_t kMaxSubtitleTimeMs = 3'599'999'999; // 999:59:59,999
inline constexpr qint64 kMaxSrtBytes = 8LL * 1024 * 1024;

/// Validates canonical order, unique ids, text, limits and non-overlapping intervals.
[[nodiscard]] Status validateSubtitles(const SubtitleTrack& track);
[[nodiscard]] std::optional<std::int64_t> subtitleMillisecondsAtFrame(FrameCount frame,
                                                                      const FrameRate& rate);
/// The first sampled frame whose timestamp is at or after a subtitle boundary.
[[nodiscard]] std::optional<FrameCount> subtitleFrameAtMilliseconds(std::int64_t milliseconds,
                                                                    const FrameRate& rate);
[[nodiscard]] const SubtitleCue* subtitleAtFrame(const SubtitleTrack& track, FrameCount frame,
                                                 const FrameRate& rate);
[[nodiscard]] QString formatSubtitleTime(std::int64_t milliseconds);
[[nodiscard]] Result<std::int64_t> parseSubtitleTime(const QString& text);

namespace srt {
/// Strict UTF-8 (optional BOM), normalised newlines, numeric counters and plain text.
/// Input may be unordered; output is sorted and renumbered. Overlaps fail atomically.
[[nodiscard]] Result<SubtitleTrack> fromBytes(const QByteArray& bytes);
[[nodiscard]] Result<QByteArray> toBytes(const SubtitleTrack& track);
} // namespace srt

namespace webvtt {
/// Writes UTF-8 WebVTT sidecar captions with exact millisecond cue boundaries.
/// Cue text is HTML-escaped so angle brackets and ampersands remain literal.
[[nodiscard]] Result<QByteArray> toBytes(const SubtitleTrack& track);
} // namespace webvtt

} // namespace ft

