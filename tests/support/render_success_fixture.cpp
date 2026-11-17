// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
// Synthetic protocol-compliant helper that deliberately lies about completing.
// It never runs codecs; only the editor-side existence/size gate is under test.
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char* argv[])
{
    std::string output;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--output") {
            output = argv[i + 1];
            break;
        }
    }
    if (output.empty()) {
        return 2;
    }

    const bool missingPath = output.find("fixture-no-path-success") != std::string::npos;
    const bool mismatchedPath = output.find("fixture-mismatch-success") != std::string::npos;
    if (output.find("fixture-empty-success") != std::string::npos) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
    } else if (output.find("fixture-nonempty-success") != std::string::npos ||
               missingPath || mismatchedPath) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
        file.write("test", 4);
    }
    std::fputs("@ft {\"event\":\"started\",\"encoder\":\"synthetic-fixture\",\"total\":25}\n",
               stdout);
    QJsonObject finished{{QStringLiteral("event"), QStringLiteral("finished")}};
    if (!missingPath) {
        finished.insert(QStringLiteral("output"),
                        QString::fromStdString(mismatchedPath ? output + ".wrong" : output));
    }
    const QByteArray line = QByteArrayLiteral("@ft ") +
                            QJsonDocument(finished).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
    return 0;
}
