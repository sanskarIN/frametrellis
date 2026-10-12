// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

namespace ft::ui {

/// Sends Qt log output to stderr and to a rotating log file in the application data folder.
void installFileLogger();
QString logFilePath();

} // namespace ft::ui
