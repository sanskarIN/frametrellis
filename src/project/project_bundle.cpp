// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_bundle.h"

#include "project/project_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QTemporaryDir>

#include <limits>

namespace ft::project_bundle {
namespace {

constexpr auto kFormat = "org.frametrellis.bundle";
constexpr auto kProjectFile = "project.ftproj";
constexpr auto kManifest = "manifest.json";
constexpr qint64 kChunkSize = 1024LL * 1024;
constexpr qint64 kMaxManifestBytes = 16LL * 1024 * 1024;
constexpr qint64 kMaxExactJsonInteger = 9007199254740991LL;

bool stopped(const Cancel& cancelled)
{
    return cancelled && cancelled();
}

Error cancellation()
{
    return makeError(ErrorCode::Cancelled, QStringLiteral("Project bundle operation cancelled."));
}

Error ioError(const QString& message, const QString& detail = {})
{
    return makeError(ErrorCode::Io, message, detail);
}

bool validSuffix(const QString& suffix)
{
    if (suffix.isEmpty() || suffix.size() > 16) {
        return false;
    }
    for (const QChar c : suffix) {
        if (!((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
              (c >= QLatin1Char('0') && c <= QLatin1Char('9')))) {
            return false;
        }
    }
    return true;
}

QString mediaName(int index, const QString& source)
{
    QString name = QStringLiteral("media/asset-%1").arg(index, 5, 10, QLatin1Char('0'));
    const QString suffix = QFileInfo(source).suffix().toLower();
    if (validSuffix(suffix)) {
        name += QLatin1Char('.') + suffix;
    }
    return name;
}

bool safeMediaPath(const QString& path)
{
    // The manifest may be edited or received from another machine. Only the
    // exact generated filename grammar is accepted; no arbitrary relative paths.
    if (!path.startsWith(QLatin1String("media/asset-"))) {
        return false;
    }
    const QString name = path.mid(12);
    const QString digits = name.left(5);
    if (digits.size() != 5) {
        return false;
    }
    for (const QChar c : digits) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            return false;
        }
    }
    if (name.size() == 5) {
        return true;
    }
    return name.at(5) == QLatin1Char('.') && validSuffix(name.mid(6));
}

bool regularFile(const QFileInfo& info)
{
    return info.isFile() && !info.isSymLink();
}

bool sameFingerprint(const QFileInfo& info, const File& file)
{
    return info.isFile() && info.size() == file.bytes &&
           info.lastModified() == file.modified;
}

Status writeBytes(const QString& path, const QByteArray& data)
{
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() ||
        !output.commit()) {
        return ioError(QStringLiteral("Could not write bundle metadata."), output.errorString());
    }
    return {};
}

Result<QByteArray> readBounded(const QString& path, qint64 limit)
{
    const QFileInfo info(path);
    if (!regularFile(info) || info.size() > limit) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("Missing or oversized bundle metadata."), path);
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        return ioError(QStringLiteral("Could not read bundle metadata."), input.errorString());
    }
    const QByteArray bytes = input.read(limit + 1);
    if (input.error() != QFileDevice::NoError || bytes.size() > limit) {
        return ioError(QStringLiteral("Could not read complete bundle metadata."), path);
    }
    return bytes;
}

Result<QByteArray> copyFile(const File& file, const QString& target, const Cancel& cancelled,
                            const std::function<void(qint64)>& advanced)
{
    if (!sameFingerprint(QFileInfo(file.sourcePath), file)) {
        return makeError(ErrorCode::Conflict, QStringLiteral("Source changed since planning."),
                         file.sourcePath);
    }
    QFile input(file.sourcePath);
    QFile output(target);
    if (!input.open(QIODevice::ReadOnly) ||
        !output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return ioError(QStringLiteral("Could not open media for copying."), file.sourcePath);
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    qint64 remaining = file.bytes;
    while (remaining > 0) {
        if (stopped(cancelled)) {
            return cancellation();
        }
        const QByteArray bytes = input.read(qMin(remaining, kChunkSize));
        if (bytes.isEmpty() || output.write(bytes) != bytes.size()) {
            return ioError(QStringLiteral("Could not copy complete media file."), file.sourcePath);
        }
        digest.addData(bytes);
        remaining -= bytes.size();
        advanced(bytes.size());
    }
    if (!input.read(1).isEmpty() || input.error() != QFileDevice::NoError || !output.flush() ||
        !sameFingerprint(QFileInfo(file.sourcePath), file)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Source changed or copying failed."), file.sourcePath);
    }
    output.close();
    if (output.error() != QFileDevice::NoError || QFileInfo(target).size() != file.bytes) {
        return ioError(QStringLiteral("Copied file length is incorrect."), target);
    }
    return digest.result().toHex();
}

Result<QByteArray> hashFile(const QString& path, qint64 expected, const Cancel& cancelled)
{
    const QFileInfo before(path);
    if (!regularFile(before) || before.size() != expected) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("Missing or changed file."),
                         path);
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        return ioError(QStringLiteral("Could not open bundle file."), path);
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    qint64 remaining = expected;
    while (remaining > 0) {
        if (stopped(cancelled)) {
            return cancellation();
        }
        const QByteArray bytes = input.read(qMin(remaining, kChunkSize));
        if (bytes.isEmpty()) {
            return ioError(QStringLiteral("Could not read complete bundle file."), path);
        }
        digest.addData(bytes);
        remaining -= bytes.size();
    }
    const QFileInfo after(path);
    if (!input.read(1).isEmpty() || input.error() != QFileDevice::NoError ||
        !regularFile(after) || after.size() != expected ||
        after.lastModified() != before.lastModified()) {
        return makeError(ErrorCode::Conflict, QStringLiteral("Bundle file changed during verification."),
                         path);
    }
    return digest.result().toHex();
}

bool validDigest(const QString& digest)
{
    if (digest.size() != 64) {
        return false;
    }
    for (const QChar c : digest) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
              (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

Result<qint64> byteCount(const QJsonValue& value)
{
    bool ok = false;
    const QString text = value.toString();
    const qint64 number = text.toLongLong(&ok);
    if (!ok || number < 0 || QString::number(number) != text) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("Invalid bundle byte count."));
    }
    return number;
}

QJsonObject record(const QString& path, qint64 bytes, const QByteArray& sha)
{
    return {{QStringLiteral("path"), path},
            {QStringLiteral("bytes"), QString::number(bytes)},
            {QStringLiteral("sha256"), QString::fromLatin1(sha)}};
}

} // namespace

Result<Plan> plan(const Project& project, Options options, const Cancel& cancelled)
{
    if (const Status valid = project.validate(); !valid) {
        return valid.error();
    }
    QSet<Id> used;
    for (const Track& track : project.timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            used.insert(clip.assetId);
        }
    }
    Plan result;
    for (const MediaAsset& asset : project.media()) {
        if (stopped(cancelled)) {
            return cancellation();
        }
        if (options.usedOnly && !used.contains(asset.id)) {
            ++result.excludedAssets;
            continue;
        }
        const QFileInfo info(asset.filePath);
        if (!info.isAbsolute() || !info.isFile() || !info.isReadable() ||
            info.canonicalFilePath().isEmpty()) {
            return makeError(ErrorCode::NotFound, QStringLiteral("Source media is unavailable."),
                             asset.name);
        }
        const bool sizeChanged = asset.info.fileSize > 0 && asset.info.fileSize != info.size();
        const bool timeChanged = asset.info.lastModified.isValid() &&
                                 asset.info.lastModified.toSecsSinceEpoch() !=
                                     info.lastModified().toSecsSinceEpoch();
        if (sizeChanged || timeChanged) {
            return makeError(ErrorCode::Conflict,
                             QStringLiteral("Source media changed since import. Relink it first."),
                             asset.name);
        }
        if (info.size() < 0 || info.size() > kMaxExactJsonInteger ||
            result.bytes > std::numeric_limits<qint64>::max() - info.size()) {
            return makeError(ErrorCode::OutOfRange, QStringLiteral("Bundle size is out of range."));
        }
        result.files.push_back(File{asset.id, info.canonicalFilePath(),
                                    mediaName(static_cast<int>(result.files.size()) + 1,
                                              asset.filePath),
                                    info.size(), info.lastModified()});
        result.bytes += info.size();
    }
    return result;
}

Result<Bundle> create(const Project& project, const QString& destination, Options options,
                      const Cancel& cancelled, const Notify& notify)
{
    Progress progress;
    const auto report = [&] {
        if (notify) {
            notify(progress);
        }
    };
    report();
    if (stopped(cancelled)) {
        return cancellation();
    }
    const QFileInfo target(destination);
    if (destination.trimmed().isEmpty() || QDir::cleanPath(destination) != destination ||
        !target.isAbsolute() ||
        target.fileName().isEmpty() || target.exists() || target.isSymLink()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Choose a new, nonexistent bundle folder."));
    }
    QDir parent(target.absolutePath());
    if (!parent.exists()) {
        return makeError(ErrorCode::NotFound, QStringLiteral("Bundle parent folder does not exist."));
    }
    QLockFile lock(parent.filePath(QStringLiteral(".frametrellis-bundle.lock")));
    if (!lock.tryLock(0)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Another bundle operation is active in this folder."));
    }
    const auto prepared = plan(project, options, cancelled);
    if (!prepared) {
        return prepared.error();
    }
    progress.phase = Phase::Copying;
    progress.totalFiles = static_cast<int>(prepared->files.size());
    progress.totalBytes = prepared->bytes;
    report();

    const QStorageInfo storage(parent.absolutePath());
    if (storage.isValid() && storage.isReady() && storage.bytesAvailable() >= 0 &&
        storage.bytesAvailable() < prepared->bytes) {
        return ioError(QStringLiteral("Not enough free space for the copied media."));
    }
    QTemporaryDir staging(parent.filePath(QStringLiteral(".ft-bundle-XXXXXX")));
    if (!staging.isValid()) {
        return ioError(QStringLiteral("Could not create bundle staging folder."), staging.errorString());
    }
    const QDir stage(staging.path());
    if (!stage.mkdir(QStringLiteral("media"))) {
        return ioError(QStringLiteral("Could not create bundle media folder."));
    }

    Project copy = project;
    copy.setId(createId());
    copy.setCreated(QDateTime::currentDateTimeUtc());
    QSet<Id> included;
    QJsonArray manifestFiles;
    for (const File& file : prepared->files) {
        if (stopped(cancelled)) {
            return cancellation();
        }
        const QString path = stage.filePath(file.relativePath);
        const auto sha = copyFile(file, path, cancelled, [&](qint64 bytes) {
            progress.bytesCopied += bytes;
            report();
        });
        if (!sha) {
            return sha.error();
        }
        included.insert(file.assetId);
        MediaAsset asset = *copy.asset(file.assetId);
        asset.filePath = path;
        asset.info.fileSize = file.bytes;
        asset.info.lastModified = QFileInfo(path).lastModified().toUTC();
        asset.status = MediaStatus::Online;
        if (const Status replaced = copy.replaceAsset(asset); !replaced) {
            return replaced.error();
        }
        QJsonObject entry = record(file.relativePath, file.bytes, *sha);
        entry[QStringLiteral("assetId")] = file.assetId;
        manifestFiles.append(entry);
        ++progress.filesCompleted;
        report();
    }
    if (options.usedOnly) {
        for (const MediaAsset& asset : project.media()) {
            if (!included.contains(asset.id)) {
                if (const auto removed = copy.removeAsset(asset.id); !removed) {
                    return removed.error();
                }
            }
        }
    }
    if (stopped(cancelled)) {
        return cancellation();
    }
    progress.phase = Phase::Finalizing;
    report();

    // A bundle uses primary relative paths so that even a COPY of a bundle,
    // while the original remains on disk, opens its own media. The ordinary
    // serializer continues to support existing absolute-path projects.
    QJsonObject document = project_format::toJson(copy, stage.absolutePath());
    QJsonObject body = document.value(QStringLiteral("project")).toObject();
    QJsonArray media = body.value(QStringLiteral("media")).toArray();
    for (qsizetype i = 0; i < media.size(); ++i) {
        QJsonObject asset = media.at(i).toObject();
        const QString relative = asset.value(QStringLiteral("relativePath")).toString();
        if (!safeMediaPath(relative)) {
            return makeError(ErrorCode::Internal, QStringLiteral("Invalid generated bundle path."));
        }
        asset[QStringLiteral("path")] = relative;
        media[i] = asset;
    }
    body[QStringLiteral("media")] = media;
    document[QStringLiteral("project")] = body;
    const QByteArray projectBytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    const auto reopened = project_format::fromBytes(projectBytes, stage.absolutePath());
    if (!reopened) {
        return reopened.error();
    }
    if (const Status saved = writeBytes(stage.filePath(QLatin1String(kProjectFile)), projectBytes);
        !saved) {
        return saved.error();
    }
    const QByteArray projectSha =
        QCryptographicHash::hash(projectBytes, QCryptographicHash::Sha256).toHex();
    const QJsonObject manifest{
        {QStringLiteral("format"), QLatin1String(kFormat)},
        {QStringLiteral("version"), 1},
        {QStringLiteral("projectId"), copy.id()},
        {QStringLiteral("project"), record(QLatin1String(kProjectFile), projectBytes.size(), projectSha)},
        {QStringLiteral("files"), manifestFiles},
        {QStringLiteral("usedOnly"), options.usedOnly},
    };
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (manifestBytes.size() > kMaxManifestBytes) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("Bundle manifest is too large."));
    }
    if (const Status saved = writeBytes(stage.filePath(QLatin1String(kManifest)), manifestBytes);
        !saved) {
        return saved.error();
    }
    if (stopped(cancelled)) {
        return cancellation();
    }

    // Publish the completed directory with a same-parent rename. Refuse an
    // existing destination, including one created while copying.
    const QFileInfo finalTarget(destination);
    if (finalTarget.exists() || finalTarget.isSymLink() ||
        !parent.rename(QFileInfo(staging.path()).fileName(), target.fileName())) {
        return ioError(QStringLiteral("Could not publish bundle. The destination may already exist."),
                       destination);
    }
    staging.setAutoRemove(false);
    const QString directory = target.absoluteFilePath();
    return Bundle{directory, QDir(directory).filePath(QLatin1String(kProjectFile)), copy.id(),
                  progress.totalFiles, progress.totalBytes};
}

Result<Verification> verify(const QString& directory, const Cancel& cancelled)
{
    const QFileInfo root(directory);
    const QDir dir(directory);
    const QFileInfo mediaDir(dir.filePath(QStringLiteral("media")));
    if (!root.isAbsolute() || !root.isDir() || root.isSymLink() ||
        !mediaDir.isDir() || mediaDir.isSymLink()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("Invalid bundle directory."));
    }
    const auto bytes = readBounded(dir.filePath(QLatin1String(kManifest)), kMaxManifestBytes);
    if (!bytes) {
        return bytes.error();
    }
    const auto doc = QJsonDocument::fromJson(*bytes);
    const auto manifest = doc.object();
    if (!doc.isObject() || manifest.value(QStringLiteral("format")).toString() != QLatin1String(kFormat) ||
        manifest.value(QStringLiteral("version")).toDouble() != 1 ||
        !isValidId(manifest.value(QStringLiteral("projectId")).toString()) ||
        !manifest.value(QStringLiteral("files")).isArray() ||
        !manifest.value(QStringLiteral("project")).isObject()) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("Invalid bundle manifest."));
    }
    const auto files = manifest.value(QStringLiteral("files")).toArray();
    if (files.size() > kMaxMediaAssets) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("Too many bundle files."));
    }
    // Validate all entries before opening any referenced file.
    QSet<QString> paths;
    QSet<Id> ids;
    for (const auto& value : files) {
        const auto entry = value.toObject();
        const QString path = entry.value(QStringLiteral("path")).toString();
        const Id id = entry.value(QStringLiteral("assetId")).toString();
        if (!value.isObject() || !safeMediaPath(path) || paths.contains(path) ||
            !isValidId(id) || ids.contains(id) ||
            !validDigest(entry.value(QStringLiteral("sha256")).toString()) ||
            !byteCount(entry.value(QStringLiteral("bytes")))) {
            return makeError(ErrorCode::InvalidDocument, QStringLiteral("Invalid bundle media entry."));
        }
        paths.insert(path);
        ids.insert(id);
    }
    const auto project = manifest.value(QStringLiteral("project")).toObject();
    const auto projectSize = byteCount(project.value(QStringLiteral("bytes")));
    if (project.value(QStringLiteral("path")).toString() != QLatin1String(kProjectFile) ||
        !projectSize || *projectSize > project_format::kMaxDocumentBytes ||
        !validDigest(project.value(QStringLiteral("sha256")).toString())) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("Invalid bundle project entry."));
    }
    Verification result;
    const auto check = [&](const QJsonObject& entry) -> Result<bool> {
        const QString path = entry.value(QStringLiteral("path")).toString();
        const qint64 size = *byteCount(entry.value(QStringLiteral("bytes")));
        const auto sha = hashFile(dir.filePath(path), size, cancelled);
        if (!sha && sha.error().code == ErrorCode::Cancelled) {
            return sha.error();
        }
        const bool matches = sha && QString::fromLatin1(*sha) ==
                                        entry.value(QStringLiteral("sha256")).toString();
        if (!matches) {
            result.problems.append(path);
        }
        return matches;
    };
    if (stopped(cancelled)) {
        return cancellation();
    }
    const auto projectCheck = check(project);
    if (!projectCheck) {
        return projectCheck.error();
    }
    result.projectMatches = *projectCheck;
    for (const auto& value : files) {
        if (stopped(cancelled)) {
            return cancellation();
        }
        const auto entry = value.toObject();
        const auto matches = check(entry);
        if (!matches) {
            return matches.error();
        }
        ++result.files;
        const qint64 size = *byteCount(entry.value(QStringLiteral("bytes")));
        if (result.bytes > std::numeric_limits<qint64>::max() - size) {
            return makeError(ErrorCode::OutOfRange, QStringLiteral("Bundle size is out of range."));
        }
        result.bytes += size;
    }
    return result;
}

} // namespace ft::project_bundle
