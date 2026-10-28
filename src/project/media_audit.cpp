// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/media_audit.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace ft::media_audit {

QJsonObject buildReport(const Project& project)
{
    QHash<Id, int> uses;
    for (const Track& track : project.timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            ++uses[clip.assetId];
        }
    }

    QJsonArray assets;
    int availableCount = 0;
    int missingCount = 0;
    int changedCount = 0;
    int unusedCount = 0;
    for (const MediaAsset& asset : project.media()) {
        const QFileInfo file(asset.filePath);
        const bool exists = file.exists() && file.isFile();
        const bool sizeChanged =
            exists && asset.info.fileSize > 0 && file.size() != asset.info.fileSize;
        const bool timeChanged =
            exists && asset.info.lastModified.isValid() &&
            file.lastModified().toMSecsSinceEpoch() != asset.info.lastModified.toMSecsSinceEpoch();
        const bool changed = sizeChanged || timeChanged;
        const QString status = !exists   ? QStringLiteral("missing")
                               : changed ? QStringLiteral("changed")
                                         : QStringLiteral("online");
        if (!exists) {
            ++missingCount;
        } else if (changed) {
            ++changedCount;
        } else {
            ++availableCount;
        }

        const int clipUses = uses.value(asset.id);
        if (clipUses == 0) {
            ++unusedCount;
        }

        // File lengths are decimal strings so 64-bit values remain exact in JSON clients.
        QJsonObject entry;
        entry.insert(QStringLiteral("assetId"), asset.id);
        entry.insert(QStringLiteral("name"), asset.name);
        entry.insert(QStringLiteral("sourcePath"), asset.filePath);
        entry.insert(QStringLiteral("kind"), mediaKindToString(asset.info.kind));
        entry.insert(QStringLiteral("status"), status);
        entry.insert(QStringLiteral("clipUses"), clipUses);
        entry.insert(QStringLiteral("storedBytes"), QString::number(asset.info.fileSize));
        if (asset.info.lastModified.isValid()) {
            entry.insert(QStringLiteral("storedModifiedUtc"),
                         asset.info.lastModified.toUTC().toString(Qt::ISODateWithMs));
        }
        if (exists) {
            entry.insert(QStringLiteral("currentBytes"), QString::number(file.size()));
            entry.insert(QStringLiteral("currentModifiedUtc"),
                         file.lastModified().toUTC().toString(Qt::ISODateWithMs));
        }
        QJsonArray changeReasons;
        if (sizeChanged) {
            changeReasons.append(QStringLiteral("size"));
        }
        if (timeChanged) {
            changeReasons.append(QStringLiteral("modified"));
        }
        entry.insert(QStringLiteral("changeReasons"), changeReasons);
        assets.append(entry);
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("total"), static_cast<int>(project.media().size()));
    summary.insert(QStringLiteral("online"), availableCount);
    summary.insert(QStringLiteral("missing"), missingCount);
    summary.insert(QStringLiteral("changed"), changedCount);
    summary.insert(QStringLiteral("unused"), unusedCount);
    summary.insert(QStringLiteral("clipUses"), project.timeline().clipCount());

    QJsonObject report;
    report.insert(QStringLiteral("schemaVersion"), 1);
    report.insert(QStringLiteral("projectName"), project.name());
    report.insert(QStringLiteral("projectId"), project.id());
    report.insert(QStringLiteral("checkedAtUtc"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert(QStringLiteral("summary"), summary);
    report.insert(QStringLiteral("assets"), assets);
    return report;
}

Status saveReport(const Project& project, const QString& path, const QString& projectFilePath)
{
    if (path.trimmed().isEmpty() ||
        QFileInfo(path).suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The media audit filename must end in .json."));
    }

    const QString destination = canonicalMediaKey(path);
    if ((!projectFilePath.isEmpty() && destination == canonicalMediaKey(projectFilePath)) ||
        project.assetByPath(path)) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("A media audit cannot overwrite the project or source media."));
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Cannot open media audit destination."),
                         output.errorString());
    }
    const QByteArray bytes = QJsonDocument(buildReport(project)).toJson();
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return makeError(ErrorCode::Io, QStringLiteral("Cannot write media audit report."),
                         output.errorString());
    }
    if (!output.commit()) {
        return makeError(ErrorCode::Io, QStringLiteral("Cannot finish media audit report."),
                         output.errorString());
    }
    return {};
}

} // namespace ft::media_audit
