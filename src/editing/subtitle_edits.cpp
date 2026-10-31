// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/subtitle_edits.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace ft::edits {
namespace {

class SubtitleCommand final : public EditCommand
{
public:
    using Operation = std::function<Status(SubtitleTrack&)>;
    SubtitleCommand(QString text, Operation operation)
        : m_text(std::move(text))
        , m_operation(std::move(operation))
    {}
    [[nodiscard]] QString text() const override { return m_text; }
    Result<ChangeSet> redo(Project& project) override
    {
        if (!m_applied) {
            SubtitleTrack after = project.subtitles();
            if (const Status edited = m_operation(after); !edited) {
                return edited.error();
            }
            m_before = project.subtitles();
            if (const Status checked = project.setSubtitles(std::move(after)); !checked) {
                return checked.error();
            }
            m_after = project.subtitles();
            if (m_before == m_after) {
                return ChangeSet{};
            }
            m_applied = true;
        } else {
            (void)project.setSubtitles(m_after);
        }
        ChangeSet changed;
        changed.subtitles = true;
        return changed;
    }
    ChangeSet undo(Project& project) override
    {
        (void)project.setSubtitles(m_before);
        ChangeSet changed;
        changed.subtitles = true;
        return changed;
    }

private:
    QString m_text;
    Operation m_operation;
    SubtitleTrack m_before;
    SubtitleTrack m_after;
    bool m_applied = false;
};

auto findCue(SubtitleTrack& track, const Id& id)
{
    return std::find_if(track.cues.begin(), track.cues.end(),
                        [&](const SubtitleCue& cue) { return cue.id == id; });
}

} // namespace

std::unique_ptr<EditCommand> addSubtitle(SubtitleCue cue)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Add Subtitle"), [cue = std::move(cue)](SubtitleTrack& track) -> Status {
            track.cues.push_back(cue);
            return {};
        });
}

std::unique_ptr<EditCommand> updateSubtitle(SubtitleCue cue)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Edit Subtitle"), [cue = std::move(cue)](SubtitleTrack& track) -> Status {
            const auto it = findCue(track, cue.id);
            if (it == track.cues.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The subtitle was not found."));
            }
            *it = cue;
            return {};
        });
}

std::unique_ptr<EditCommand> removeSubtitle(Id cueId)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Remove Subtitle"),
        [cueId = std::move(cueId)](SubtitleTrack& track) -> Status {
            const auto it = findCue(track, cueId);
            if (it == track.cues.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The subtitle was not found."));
            }
            track.cues.erase(it);
            return {};
        });
}

std::unique_ptr<EditCommand> appendSubtitles(std::vector<SubtitleCue> cues)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Import Subtitles"),
        [cues = std::move(cues)](SubtitleTrack& track) -> Status {
            track.cues.insert(track.cues.end(), cues.begin(), cues.end());
            return {};
        });
}

std::unique_ptr<EditCommand> replaceSubtitles(SubtitleTrack track)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Replace Subtitles"),
        [track = std::move(track)](SubtitleTrack& destination) -> Status {
            destination = track;
            return {};
        });
}

std::unique_ptr<EditCommand> clearSubtitles()
{
    return std::make_unique<SubtitleCommand>(QStringLiteral("Clear Subtitles"),
                                             [](SubtitleTrack& track) -> Status {
                                                 track.cues.clear();
                                                 return {};
                                             });
}

std::unique_ptr<EditCommand> shiftSubtitles(std::int64_t deltaMilliseconds)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Shift Subtitle Timing"),
        [deltaMilliseconds](SubtitleTrack& track) -> Status {
            if (track.cues.empty() || deltaMilliseconds == 0) {
                return makeError(ErrorCode::InvalidArgument,
                                 QStringLiteral("Add subtitles and choose a nonzero time offset."));
            }
            // Check the full track before changing any cue. Bounds use subtraction to avoid
            // signed overflow even for very large user-provided offsets.
            for (const SubtitleCue& cue : track.cues) {
                if (deltaMilliseconds < -cue.startMs ||
                    deltaMilliseconds > kMaxSubtitleTimeMs - cue.endMs) {
                    return makeError(
                        ErrorCode::OutOfRange,
                        QStringLiteral("Moving subtitles would put a cue outside valid time."));
                }
            }
            for (SubtitleCue& cue : track.cues) {
                cue.startMs += deltaMilliseconds;
                cue.endMs += deltaMilliseconds;
            }
            return {};
        });
}

std::unique_ptr<EditCommand> setSubtitleAppearance(bool burnIn, int fontPercent, bool background)
{
    return std::make_unique<SubtitleCommand>(
        QStringLiteral("Change Subtitle Appearance"),
        [burnIn, fontPercent, background](SubtitleTrack& track) -> Status {
            track.burnIn = burnIn;
            track.fontPercent = fontPercent;
            track.background = background;
            return {};
        });
}

} // namespace ft::edits
