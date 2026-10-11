// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/result.h"

#include <QList>
#include <QString>

namespace ft {

/// Output format of the edited sequence. The frame rate defines the timeline's time base.
struct ProjectSettings
{
    int width = 1920;
    int height = 1080;
    FrameRate frameRate = FrameRate::integer(30);
    int sampleRate = 48000;
    int audioChannels = 2;

    [[nodiscard]] Status validate() const;
    [[nodiscard]] QString summary() const;

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct ProjectPreset
{
    QString name;
    ProjectSettings settings;
};

/// Common sequence formats offered when creating a project.
QList<ProjectPreset> projectPresets();

inline constexpr int kMinFrameDimension = 16;
inline constexpr int kMaxFrameDimension = 8192;

} // namespace ft
