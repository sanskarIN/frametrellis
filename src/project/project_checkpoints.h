// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <vector>

namespace ft::checkpoints {

inline constexpr int kMaxCheckpointsPerProject = 100;
inline constexpr int kMaxCheckpointLabelLength = 120;
inline constexpr qint64 kMaxHeaderBytes = 16 * 1024;
inline constexpr int kMaxHistoryScan = 1000;

struct Entry
{
    Id id;
    Id projectId;
    QString label;
    QString projectName;
    QString originalPath;
    QDateTime createdAt;
    QByteArray payloadSha256; ///< lowercase hexadecimal SHA-256 of the exact project bytes
    qint64 bytes = 0;
    bool readable = true; ///< valid header; the project payload is checked on restore
    QString error;
};

struct History
{
    std::vector<Entry> entries;
    bool truncated = false;
};

/// Local, manual version history. Every checkpoint is an immutable project snapshot,
/// stored in one atomic file below <root>/<project-id>/<checkpoint-id>.ftcheckpoint.
/// The first bounded UTF-8 JSON line describes the snapshot; the remaining bytes are
/// a normal FrameTrellis project document. Listing does not parse the project payload.
///
/// The store does not copy media, change project state, delete old versions automatically,
/// or overwrite the user's original .ftproj file. Opening a version is explicit.
class Store
{
public:
    explicit Store(QString root);

    [[nodiscard]] const QString& root() const { return m_root; }

    [[nodiscard]] Result<History> list(const Id& projectId) const;
    [[nodiscard]] Result<Entry> create(const Project& project, const QString& label,
                                       const QString& originalPath = {}) const;
    [[nodiscard]] Result<Project> restore(const Id& projectId, const Id& checkpointId) const;
    [[nodiscard]] Status remove(const Id& projectId, const Id& checkpointId) const;

private:
    [[nodiscard]] Result<QString> projectDirectory(const Id& projectId, bool create) const;
    [[nodiscard]] Result<QString> checkpointPath(const Id& projectId,
                                                const Id& checkpointId) const;
    QString m_root;
};

} // namespace ft::checkpoints
