// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/render_protocol.h"

#include <QJsonDocument>

namespace ft::rendering::protocol {

QByteArray encode(const QJsonObject& event)
{
    return QByteArray(kPrefix) + QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<QJsonObject> decode(const QByteArray& line)
{
    const QByteArray trimmed = line.trimmed();
    if (!trimmed.startsWith(kPrefix)) {
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(trimmed.mid(int(sizeof(kPrefix)) - 1), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    QJsonObject object = document.object();
    if (!object.value(QLatin1String("event")).isString()) {
        return std::nullopt;
    }
    return object;
}

} // namespace ft::rendering::protocol
