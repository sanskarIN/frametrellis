// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QByteArray>
#include <QJsonObject>

#include <optional>

/// The render helper reports progress as JSON objects on stdout, one per line, each prefixed with
/// "@ft " so that unrelated output from libraries can never be mistaken for an event.
///
///   @ft {"event":"started","encoder":"libx264","total":250}
///   @ft {"event":"progress","frame":120,"total":250}
///   @ft {"event":"finished","output":"/path/out.mp4"}
///   @ft {"event":"error","message":"...","detail":"..."}
///   @ft {"event":"cancelled"}
///
/// Writing "cancel" (or closing stdin) asks the helper to stop and clean up.
namespace ft::rendering::protocol {

inline constexpr char kPrefix[] = "@ft ";
// Upper bound for a single protocol line, including its prefix and line ending.
inline constexpr qsizetype kMaxEventBytes = qsizetype{64} * 1024;

QByteArray encode(const QJsonObject& event);
std::optional<QJsonObject> decode(const QByteArray& line);

enum ExitCode { Success = 0, Failure = 1, BadArguments = 2, Cancelled = 3 };

} // namespace ft::rendering::protocol
