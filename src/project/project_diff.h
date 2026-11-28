// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QJsonObject>
#include <QStringList>

#include <array>
#include <vector>

namespace ft::project_diff {

enum class Category {
    Project,
    Settings,
    Media,
    Track,
    Clip,
    Transition,
    Marker,
    Subtitle,
    SubtitleStyle,
    Count,
};

enum class Action { Added, Removed, Changed };

struct Counts
{
    int added = 0;
    int removed = 0;
    int changed = 0;
    int unchanged = 0;

    [[nodiscard]] int totalChanges() const { return added + removed + changed; }
};

struct Detail
{
    Category category = Category::Project;
    Action action = Action::Changed;
    Id id;
    QString label;
    QStringList fields;
    QString before;
    QString after;
};

inline constexpr int kMaxComparisonDetails = 1000;

struct Report
{
    Id projectId;
    QString previousName;
    QString currentName;
    FrameCount previousDuration = 0;
    FrameCount currentDuration = 0;
    std::array<Counts, static_cast<size_t>(Category::Count)> counts{};
    int totalChanges = 0;
    std::vector<Detail> details;
};

QString categoryCode(Category category);
QString categoryTitle(Category category);
QString actionCode(Action action);

/// Compare two validated versions of the same project by stable object identity.
/// Runtime media availability and the creation timestamp are not edit differences.
/// Fields list semantic changes, not raw media paths or caption text.
[[nodiscard]] Result<Report> compare(const Project& previous, const Project& current);

/// Full category counts and a bounded deterministic detail sample.
[[nodiscard]] QJsonObject toJson(const Report& report);

} // namespace ft::project_diff
