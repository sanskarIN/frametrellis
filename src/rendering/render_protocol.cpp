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
    // Reject oversized payloads before allocating or parsing a large JSON document.
    if (line.size() > kMaxEventBytes) {
        return std::nullopt;
    }
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

bool CancelCommandReader::ingest(std::string_view bytes)
{
    for (const char character : bytes) {
        if (character == '\n') {
            const bool cancelled =
                !m_discarding && (m_line == "cancel" || m_line == "cancel\r");
            m_line.clear();
            m_discarding = false;
            if (cancelled) {
                return true;
            }
        } else if (!m_discarding) {
            // A command can contain at most 16 bytes; never buffer untrusted stdin indefinitely.
            if (m_line.size() < 16) {
                m_line.push_back(character);
            } else {
                m_line.clear();
                m_discarding = true;
            }
        }
    }
    return false;
}

} // namespace ft::rendering::protocol
