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
    const bool relativePath = output.find("fixture-relative-success") != std::string::npos;
    const bool contradictoryPath =
        output.find("fixture-contradictory-success") != std::string::npos;
    const bool noStart = output.find("fixture-no-start-success") != std::string::npos;
    const bool wrongTotal = output.find("fixture-wrong-total-success") != std::string::npos;
    if (output.find("fixture-empty-success") != std::string::npos) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
    } else if (output.find("fixture-nonempty-success") != std::string::npos || missingPath ||
               mismatchedPath || relativePath || contradictoryPath || noStart) {
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return 2;
        }
        file.write("test", 4);
    }
    if (!noStart) {
        if (wrongTotal) {
            std::fputs("@ft {\"event\":\"started\",\"encoder\":\"synthetic-fixture\",\"total\":26}\n",
                       stdout);
        } else {
            std::fputs("@ft {\"event\":\"started\",\"encoder\":\"synthetic-fixture\",\"total\":25}\n",
                       stdout);
        }
    }
    auto emitFinished = [](const QJsonObject& event) {
        const QByteArray line =
            QByteArrayLiteral("@ft ") + QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    };
    QJsonObject finished{{QStringLiteral("event"), QStringLiteral("finished")}};
    if (!missingPath) {
        const std::string reported = relativePath     ? "relative.mp4"
                                     : mismatchedPath ? output + ".wrong"
                                                      : output;
        finished.insert(QStringLiteral("output"), QString::fromStdString(reported));
    }
    if (contradictoryPath) {
        QJsonObject unexpected = finished;
        unexpected.insert(QStringLiteral("output"), QStringLiteral("unrelated.mp4"));
        emitFinished(unexpected);
    }
    emitFinished(finished);
    std::fflush(stdout);
    return 0;
}
