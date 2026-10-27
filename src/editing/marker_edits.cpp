// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/marker_edits.h"

#include <algorithm>
#include <functional>
#include <utility>

namespace ft::edits {

namespace {

class MarkerEditCommand final : public EditCommand
{
public:
    using Operation = std::function<Status(Project&)>;
    MarkerEditCommand(QString label, Operation operation)
        : m_label(std::move(label))
        , m_operation(std::move(operation))
    {}

    [[nodiscard]] QString text() const override { return m_label; }

    Result<ChangeSet> redo(Project& project) override
    {
        if (m_applied) {
            (void)project.setMarkers(m_after);
        } else {
            Project working = project;
            if (const Status result = m_operation(working); !result) {
                return result.error();
            }
            m_before = project.markers();
            m_after = working.markers();
            if (m_before == m_after) {
                return ChangeSet{};
            }
            (void)project.setMarkers(m_after);
            m_applied = true;
        }
        ChangeSet changes;
        changes.markers = true;
        return changes;
    }

    ChangeSet undo(Project& project) override
    {
        (void)project.setMarkers(m_before);
        ChangeSet changes;
        changes.markers = true;
        return changes;
    }

private:
    QString m_label;
    Operation m_operation;
    std::vector<TimelineMarker> m_before;
    std::vector<TimelineMarker> m_after;
    bool m_applied = false;
};

auto findMarker(std::vector<TimelineMarker>& markers, const Id& id)
{
    return std::find_if(markers.begin(), markers.end(),
                        [&](const TimelineMarker& marker) { return marker.id == id; });
}

} // namespace

std::unique_ptr<EditCommand> addMarker(TimelineMarker marker)
{
    return std::make_unique<MarkerEditCommand>(QStringLiteral("Add Marker"),
                                               [marker = std::move(marker)](Project& project) -> Status {
                                                   auto markers = project.markers();
                                                   markers.push_back(marker);
                                                   return project.setMarkers(std::move(markers));
                                               });
}

std::unique_ptr<EditCommand> removeMarker(Id markerId)
{
    return std::make_unique<MarkerEditCommand>(
        QStringLiteral("Remove Marker"), [markerId = std::move(markerId)](Project& project) -> Status {
            auto markers = project.markers();
            const auto it = findMarker(markers, markerId);
            if (it == markers.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The sequence marker was not found."));
            }
            markers.erase(it);
            return project.setMarkers(std::move(markers));
        });
}

std::unique_ptr<EditCommand> renameMarker(Id markerId, QString newLabel)
{
    return std::make_unique<MarkerEditCommand>(
        QStringLiteral("Rename Marker"),
        [markerId = std::move(markerId), newLabel = std::move(newLabel)](Project& project) -> Status {
            auto markers = project.markers();
            const auto it = findMarker(markers, markerId);
            if (it == markers.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The sequence marker was not found."));
            }
            it->label = newLabel.trimmed();
            return project.setMarkers(std::move(markers));
        });
}

std::unique_ptr<EditCommand> moveMarker(Id markerId, FrameCount newFrame)
{
    return std::make_unique<MarkerEditCommand>(
        QStringLiteral("Move Marker"),
        [markerId = std::move(markerId), newFrame](Project& project) -> Status {
            auto markers = project.markers();
            const auto it = findMarker(markers, markerId);
            if (it == markers.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The sequence marker was not found."));
            }
            it->frame = newFrame;
            return project.setMarkers(std::move(markers));
        });
}

std::unique_ptr<EditCommand> updateMarker(Id markerId, FrameCount newFrame, QString newLabel)
{
    return std::make_unique<MarkerEditCommand>(
        QStringLiteral("Edit Marker"), [markerId = std::move(markerId), newFrame,
                                        newLabel = std::move(newLabel)](Project& project) -> Status {
            auto markers = project.markers();
            const auto it = findMarker(markers, markerId);
            if (it == markers.end()) {
                return makeError(ErrorCode::NotFound,
                                 QStringLiteral("The sequence marker was not found."));
            }
            it->frame = newFrame;
            it->label = newLabel.trimmed();
            return project.setMarkers(std::move(markers));
        });
}

} // namespace ft::edits
