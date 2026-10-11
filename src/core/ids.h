// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

namespace ft {

/// Stable identifiers for project entities (media, tracks, clips). They are UUID strings so that
/// they remain unique when content is copied between projects.
using Id = QString;

Id createId();

/// True when the string has the shape of an identifier produced by createId(). Used to reject
/// malformed project documents early.
bool isValidId(const QString& id);

} // namespace ft
