// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"
#include "rendering/export_settings.h"

#include <QStringList>

#include <atomic>
#include <functional>

namespace ft::rendering {

/// Lists the video encoders available to MLT's avformat consumer.
QStringList availableVideoEncoders();

/// Encodes a project with MLT's avformat consumer (in the calling process).
///
/// The output is written to a temporary file in the destination folder and verified by probing
/// it before it replaces the destination, so a failed or cancelled render never leaves a partial
/// file under the requested name and never touches an existing file until it succeeds.
class RenderRunner
{
public:
    struct Progress
    {
        FrameCount frame = 0;
        FrameCount total = 0;
    };
    using ProgressCallback = std::function<void(const Progress&)>;
    using StartedCallback = std::function<void(const QString& encoder, FrameCount total)>;

    RenderRunner(Project project, ExportSettings settings, QString projectFilePath = {});

    /// Blocks until the render finishes, fails or is cancelled through the flag.
    Status run(const std::atomic<bool>& cancelRequested, const StartedCallback& started = {},
               const ProgressCallback& progress = {});

    [[nodiscard]] const QString& encoder() const { return m_encoder; }

private:
    Project m_project;
    ExportSettings m_settings;
    QString m_projectFilePath;
    QString m_encoder;
};

} // namespace ft::rendering
