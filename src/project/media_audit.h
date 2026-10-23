// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QJsonObject>

namespace ft::media_audit {

/// Read-only snapshot of on-disk media availability and timeline references.
/// No media is opened, decoded, modified, or copied.
QJsonObject buildReport(const Project& project);

/// Atomically save a UTF-8 JSON report. Never overwrite the project or any
/// media source, including symlink and normalized-path aliases.
Status saveReport(const Project& project, const QString& path,
                  const QString& projectFilePath = {});

} // namespace ft::media_audit
