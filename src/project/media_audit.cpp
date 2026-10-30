// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/media_audit.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

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
        // Older project versions stored only whole seconds. Respect that precision
        // instead of marking every legacy source with milliseconds as modified.
        const bool timeChanged = exists && asset.info.lastModified.isValid() &&
                                 (asset.info.lastModified.time().msec() == 0
                                      ? file.lastModified().toSecsSinceEpoch() !=
                                            asset.info.lastModified.toSecsSinceEpoch()
                                      : file.lastModified().toMSecsSinceEpoch() !=
                                            asset.info.lastModified.toMSecsSinceEpoch());
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

namespace {

QString csvValue(QString field)
{
    // Spreadsheets can interpret untrusted names/paths as formulas, even when
    // a field is CSV-quoted. Prefix risky values with a literal apostrophe.
    const QString stripped = field.trimmed();
    if (!stripped.isEmpty() && QStringLiteral("=+-@").contains(stripped.front())) {
        field.prepend(QLatin1Char('\''));
    }
    field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + field + QLatin1Char('"');
}

} // namespace

Status saveCsvReport(const Project& project, const QString& path, const QString& projectFilePath)
{
    if (path.trimmed().isEmpty() ||
        QFileInfo(path).suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The media audit filename must end in .csv."));
    }
    const QString destination = canonicalMediaKey(path);
    if ((!projectFilePath.isEmpty() && destination == canonicalMediaKey(projectFilePath)) ||
        project.assetByPath(path)) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("A media audit cannot overwrite the project or source media."));
    }

    QString csv = QStringLiteral(
        "assetId,name,kind,status,clipUses,storedBytes,currentBytes,sourcePath,changeReasons\r\n");
    const QJsonArray assets = buildReport(project).value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject asset = value.toObject();
        QStringList reasons;
        for (const QJsonValue& reason : asset.value(QStringLiteral("changeReasons")).toArray()) {
            reasons.append(reason.toString());
        }
        const QStringList fields = {
            asset.value(QStringLiteral("assetId")).toString(),
            asset.value(QStringLiteral("name")).toString(),
            asset.value(QStringLiteral("kind")).toString(),
            asset.value(QStringLiteral("status")).toString(),
            QString::number(asset.value(QStringLiteral("clipUses")).toInt()),
            asset.value(QStringLiteral("storedBytes")).toString(),
            asset.value(QStringLiteral("currentBytes")).toString(),
            asset.value(QStringLiteral("sourcePath")).toString(),
            reasons.join(QLatin1Char('|'))};
        QStringList escaped;
        escaped.reserve(fields.size());
        for (const QString& field : fields) {
            escaped.append(csvValue(field));
        }
        csv += escaped.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Cannot open CSV audit destination."),
                         output.errorString());
    }
    const QByteArray bytes = QByteArray("\xef\xbb\xbf", 3) + csv.toUtf8();
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return makeError(ErrorCode::Io, QStringLiteral("Cannot write CSV media audit."),
                         output.errorString());
    }
    if (!output.commit()) {
        return makeError(ErrorCode::Io, QStringLiteral("Cannot finish CSV media audit."),
                         output.errorString());
    }
    return {};
}

} // namespace ft::media_audit
