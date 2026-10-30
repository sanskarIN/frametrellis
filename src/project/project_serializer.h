// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"

#include <QDir>
#include <QJsonObject>
#include <QString>

namespace ft {

/// Reads and writes the versioned FrameTrellis project format (.ftproj, UTF-8 JSON).
///
/// Documents are treated as untrusted input: every field is type- and range-checked and the
/// resulting project must pass Project::validate(). Saving uses an atomic replace so that a
/// failed write (for example, a full disk) never damages the previous file.
namespace project_format {

inline constexpr const char* kFormatName = "org.frametrellis.project";
inline constexpr int kCurrentVersion = 6;
inline constexpr qint64 kMaxDocumentBytes = 64LL * 1024 * 1024;
inline constexpr const char* kFileSuffix = "ftproj";

/// Serialises a project. When projectDir is non-empty, media paths are additionally stored
/// relative to it so that moved project folders can be reopened.
QJsonObject toJson(const Project& project, const QString& projectDir = {});

/// Parses a document. projectDir is used to resolve relative media paths.
Result<Project> fromJson(const QJsonObject& document, const QString& projectDir = {});

QByteArray toBytes(const Project& project, const QString& projectDir = {});
Result<Project> fromBytes(const QByteArray& data, const QString& projectDir = {});

Status saveToFile(const Project& project, const QString& filePath);
Result<Project> loadFromFile(const QString& filePath);

} // namespace project_format
} // namespace ft
