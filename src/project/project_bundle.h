// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QStringList>

#include <functional>
#include <vector>

namespace ft::project_bundle {

struct Options
{
    bool usedOnly = false;
};

struct File
{
    Id assetId;
    QString sourcePath;
    QString relativePath;
    qint64 bytes = 0;
    QDateTime modified;
};

struct Plan
{
    std::vector<File> files;
    int excludedAssets = 0;
    qint64 bytes = 0;
};

enum class Phase { Planning, Copying, Finalizing };

struct Progress
{
    Phase phase = Phase::Planning;
    int filesCompleted = 0;
    int totalFiles = 0;
    qint64 bytesCopied = 0;
    qint64 totalBytes = 0;
};

using Cancel = std::function<bool()>;
using Notify = std::function<void(const Progress&)>;

struct Bundle
{
    QString directory;
    QString projectFile;
    Id projectId;
    int files = 0;
    qint64 bytes = 0;
};

/// Inspect source availability and known import fingerprints. No files are written.
[[nodiscard]] Result<Plan> plan(const Project& project, Options options = {},
                                const Cancel& cancelled = {});

/// Create an independent editable project and copy original media into a fresh folder.
/// Work is staged beside the destination and published only after all files and the
/// project/manifest have been written. The destination must not exist. No original
/// media or project files are modified. Call from a worker for large copies.
[[nodiscard]] Result<Bundle> create(const Project& project, const QString& destination,
                                    Options options = {}, const Cancel& cancelled = {},
                                    const Notify& notify = {});

/// Verify the manifest and every copied file's SHA-256, without opening media decoders.
/// A changed project document is reported separately from missing/changed media.
struct Verification
{
    int files = 0;
    qint64 bytes = 0;
    bool projectMatches = false;
    QStringList problems;
};

[[nodiscard]] Result<Verification> verify(const QString& directory,
                                            const Cancel& cancelled = {});

} // namespace ft::project_bundle
