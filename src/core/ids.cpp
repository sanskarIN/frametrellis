// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "core/ids.h"

#include <QUuid>

namespace ft {

Id createId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool isValidId(const QString& id)
{
    if (id.isEmpty() || id.size() > 64) {
        return false;
    }
    for (const QChar c : id) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('-') &&
            c != QLatin1Char('_')) {
            return false;
        }
    }
    return true;
}

} // namespace ft
