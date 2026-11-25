// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_checkpoints.h"

#include "project/project_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>
#include <utility>

namespace ft::checkpoints {

namespace {

constexpr auto kSuffix = ".ftcheckpoint";
constexpr auto kFormat = "org.frametrellis.checkpoint";
constexpr int kVersion = 1;
constexpr qint64 kMaxFileBytes = project_format::kMaxDocumentBytes + kMaxHeaderBytes;

bool validLabel(const QString& label)
{
    if (label.isEmpty() || label.size() > kMaxCheckpointLabelLength || label.trimmed() != label) {
        return false;
    }
    return std::none_of(label.cbegin(), label.cend(), [](QChar c) {
        return c.isNull() || c.category() == QChar::Other_Control ||
               c.category() == QChar::Separator_Line || c.category() == QChar::Separator_Paragraph;
    });
}

bool validOriginalPath(const QString& path)
{
    return path.isEmpty() || (path.size() <= kMaxMediaPathLength &&
                              !path.contains(QChar(QChar::Null)) && QFileInfo(path).isAbsolute());
}

Status checkDirectory(const QString& path, bool create)
{
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isDir())) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("A checkpoint storage path is not a regular directory."),
                         path);
    }
    if (create && !info.exists() && !QDir().mkpath(path)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not create checkpoint storage."),
                         path);
    }
    return {};
}

bool validDigest(const QByteArray& digest)
{
    return digest.size() == 64 && std::all_of(digest.cbegin(), digest.cend(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

QByteArray payloadDigest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

Error invalidHeader(const QString& detail)
{
    return makeError(ErrorCode::InvalidDocument,
                     QStringLiteral("The checkpoint metadata is invalid."), detail);
}

Result<Entry> readHeader(QFile& file, const Id& expectedProject, const Id& expectedCheckpoint)
{
    if (file.size() <= 0 || file.size() > kMaxFileBytes) {
        return invalidHeader(QStringLiteral("Invalid checkpoint file length."));
    }
    // Never read a large malformed first line or decode an unbounded header.
    const QByteArray line = file.readLine(kMaxHeaderBytes + 1);
    if (line.size() > kMaxHeaderBytes || !line.endsWith('\n') || file.atEnd()) {
        return invalidHeader(QStringLiteral("Missing bounded metadata header or project payload."));
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        return invalidHeader(parse.errorString());
    }
    const QJsonObject header = document.object();
    if (header.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        return invalidHeader(QStringLiteral("Unexpected checkpoint format."));
    }
    const QJsonValue version = header.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != kVersion) {
        return makeError(ErrorCode::UnsupportedVersion,
                         QStringLiteral("This checkpoint format version is not supported."));
    }
    Entry entry;
    entry.id = header.value(QStringLiteral("checkpointId")).toString();
    entry.projectId = header.value(QStringLiteral("projectId")).toString();
    entry.label = header.value(QStringLiteral("label")).toString();
    entry.projectName = header.value(QStringLiteral("projectName")).toString();
    entry.createdAt = QDateTime::fromString(header.value(QStringLiteral("createdAtUtc")).toString(),
                                            Qt::ISODateWithMs);
    const QJsonValue originalPath = header.value(QStringLiteral("originalPath"));
    if (!originalPath.isString()) {
        return invalidHeader(QStringLiteral("Invalid original project path field."));
    }
    entry.originalPath = originalPath.toString();
    entry.payloadSha256 = header.value(QStringLiteral("payloadSha256")).toString().toLatin1();
    if (entry.id != expectedCheckpoint || entry.projectId != expectedProject ||
        !isValidId(entry.id) || !isValidId(entry.projectId) || !validLabel(entry.label) ||
        entry.projectName.trimmed().isEmpty() || entry.projectName.size() > 120 ||
        !entry.createdAt.isValid() || !validOriginalPath(entry.originalPath) ||
        !validDigest(entry.payloadSha256)) {
        return invalidHeader(QStringLiteral("Identity, name, timestamp or path check failed."));
    }
    entry.createdAt = entry.createdAt.toUTC();
    entry.bytes = file.size();
    return entry;
}

QByteArray encodeHeader(const Entry& entry)
{
    const QJsonObject header{
        {QStringLiteral("format"), QLatin1String(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("checkpointId"), entry.id},
        {QStringLiteral("projectId"), entry.projectId},
        {QStringLiteral("label"), entry.label},
        {QStringLiteral("projectName"), entry.projectName},
        {QStringLiteral("originalPath"), entry.originalPath},
        {QStringLiteral("createdAtUtc"), entry.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("payloadSha256"), QString::fromLatin1(entry.payloadSha256)},
    };
    return QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
}

bool newerFirst(const Entry& a, const Entry& b)
{
    if (a.createdAt != b.createdAt) {
        return a.createdAt > b.createdAt;
    }
    return a.id < b.id;
}

Error lockError(const QString& path)
{
    return makeError(ErrorCode::Conflict,
                     QStringLiteral("Checkpoint storage is busy. Try again after the other "
                                    "operation has completed."),
                     path);
}

} // namespace

Store::Store(QString root)
    : m_root(root.trimmed().isEmpty() ? QString() : QDir::cleanPath(std::move(root)))
{}

Result<QString> Store::projectDirectory(const Id& projectId, bool create) const
{
    if (m_root.isEmpty() || !QFileInfo(m_root).isAbsolute() || !isValidId(projectId)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Invalid checkpoint storage or project identity."));
    }
    if (const Status checked = checkDirectory(m_root, create); !checked) {
        return checked.error();
    }
    const QString directory = QDir(m_root).filePath(projectId);
    if (const Status checked = checkDirectory(directory, create); !checked) {
        return checked.error();
    }
    return directory;
}

Result<QString> Store::checkpointPath(const Id& projectId, const Id& checkpointId) const
{
    if (!isValidId(checkpointId)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Invalid checkpoint identity."));
    }
    const auto directory = projectDirectory(projectId, false);
    if (!directory) {
        return directory.error();
    }
    return QDir(*directory).filePath(checkpointId + QLatin1String(kSuffix));
}

Result<History> Store::list(const Id& projectId) const
{
    const auto directory = projectDirectory(projectId, false);
    if (!directory) {
        return directory.error();
    }
    History history;
    if (!QFileInfo::exists(*directory)) {
        return history;
    }

    QDirIterator iterator(*directory, {QStringLiteral("*.ftcheckpoint")},
                          QDir::Files | QDir::NoSymLinks);
    int scanned = 0;
    while (iterator.hasNext()) {
        if (scanned >= kMaxHistoryScan) {
            history.truncated = true;
            break;
        }
        const QFileInfo info(iterator.next());
        ++scanned;
        const Id id = info.completeBaseName();
        if (!isValidId(id) || !info.isFile() || info.isSymLink()) {
            continue;
        }
        QFile file(info.absoluteFilePath());
        const auto header =
            file.open(QIODevice::ReadOnly)
                ? readHeader(file, projectId, id)
                : Result<Entry>(makeError(ErrorCode::Io, QStringLiteral("Cannot read checkpoint."),
                                          file.errorString()));
        if (header) {
            history.entries.push_back(*header);
        } else {
            // Keep damaged versions visible so the user can identify and
            // explicitly remove them. Never delete them during discovery.
            Entry damaged;
            damaged.id = id;
            damaged.projectId = projectId;
            damaged.label = id;
            damaged.createdAt = info.lastModified().toUTC();
            damaged.bytes = info.size();
            damaged.readable = false;
            damaged.error = header.error().toString();
            history.entries.push_back(std::move(damaged));
        }
    }
    std::sort(history.entries.begin(), history.entries.end(), newerFirst);
    return history;
}

Result<Entry> Store::create(const Project& project, const QString& label,
                            const QString& originalPath) const
{
    const QString cleanLabel = label.trimmed();
    if (!validLabel(cleanLabel)) {
        return makeError(
            ErrorCode::InvalidArgument,
            QStringLiteral("Checkpoint names must contain 1 to 120 characters on one line."));
    }
    if (!validOriginalPath(originalPath)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The original project location is invalid."));
    }
    if (const Status valid = project.validate(); !valid) {
        return valid.error();
    }
    // Check the actual serialized payload against the same format reader.
    // Never publish a checkpoint that the current editor cannot reopen.
    const QByteArray payload = project_format::toBytes(project);
    if (payload.size() > project_format::kMaxDocumentBytes) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The project is too large for a checkpoint."));
    }
    const auto roundTrip = project_format::fromBytes(payload);
    if (!roundTrip) {
        return roundTrip.error();
    }

    const auto directory = projectDirectory(project.id(), true);
    if (!directory) {
        return directory.error();
    }
    QLockFile lock(QDir(*directory).filePath(QStringLiteral(".checkpoint.lock")));
    if (!lock.tryLock(0)) {
        return lockError(*directory);
    }
    const auto history = list(project.id());
    if (!history) {
        return history.error();
    }
    if (history->truncated ||
        history->entries.size() >= static_cast<size_t>(kMaxCheckpointsPerProject)) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("This project has reached the checkpoint limit. Remove an unwanted "
                           "checkpoint explicitly before creating another."));
    }

    Entry entry;
    entry.id = createId();
    entry.projectId = project.id();
    entry.label = cleanLabel;
    entry.projectName = roundTrip->name();
    entry.originalPath = originalPath;
    entry.createdAt = QDateTime::currentDateTimeUtc();
    entry.payloadSha256 = payloadDigest(payload);
    const QString path = QDir(*directory).filePath(entry.id + QLatin1String(kSuffix));
    const QFileInfo destination(path);
    if (destination.exists() || destination.isSymLink() || project.assetByPath(path) ||
        (!originalPath.isEmpty() && canonicalMediaKey(path) == canonicalMediaKey(originalPath))) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Checkpoint destination is occupied."));
    }

    const QByteArray header = encodeHeader(entry);
    if (header.size() > kMaxHeaderBytes) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The checkpoint metadata is too large."));
    }
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not open checkpoint destination."),
                         output.errorString());
    }
    if (output.write(header) != header.size() || output.write(payload) != payload.size()) {
        output.cancelWriting();
        return makeError(ErrorCode::Io, QStringLiteral("Could not write the checkpoint."),
                         output.errorString());
    }
    if (!output.commit()) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not finish the checkpoint."),
                         output.errorString());
    }
    entry.bytes = header.size() + payload.size();
    return entry;
}

Result<Project> Store::restore(const Id& projectId, const Id& checkpointId) const
{
    const auto path = checkpointPath(projectId, checkpointId);
    if (!path) {
        return path.error();
    }
    const QFileInfo info(*path);
    if (!info.exists()) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The checkpoint does not exist."));
    }
    if (!info.isFile() || info.isSymLink()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The checkpoint is not a regular file."));
    }
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not open the checkpoint."),
                         file.errorString());
    }
    const auto header = readHeader(file, projectId, checkpointId);
    if (!header) {
        return header.error();
    }

    // Preserve the serializer's size limit even if a file changes after the
    // metadata check. The checkpoint is not interpreted as a relative project.
    const QByteArray payload = file.read(project_format::kMaxDocumentBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not read the checkpoint."),
                         file.errorString());
    }
    if (payload.size() > project_format::kMaxDocumentBytes) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("The checkpoint project payload is too large."));
    }
    if (payloadDigest(payload) != header->payloadSha256) {
        return makeError(
            ErrorCode::InvalidDocument,
            QStringLiteral("The checkpoint project payload failed its integrity check."));
    }
    auto project = project_format::fromBytes(payload);
    if (!project) {
        return project.error();
    }
    if (project->id() != projectId || project->name() != header->projectName) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("Checkpoint metadata does not match its project payload."));
    }
    return std::move(project).value();
}

Status Store::remove(const Id& projectId, const Id& checkpointId) const
{
    const auto path = checkpointPath(projectId, checkpointId);
    if (!path) {
        return path.error();
    }
    const QFileInfo initial(*path);
    if (!initial.exists() && !initial.isSymLink()) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The checkpoint does not exist."));
    }
    const QString directory = initial.absolutePath();
    QLockFile lock(QDir(directory).filePath(QStringLiteral(".checkpoint.lock")));
    if (!lock.tryLock(0)) {
        return lockError(directory);
    }
    const QFileInfo info(*path);
    if (!info.isFile() || info.isSymLink()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Only regular checkpoint files can be removed."));
    }
    // Remove only the exact validated checkpoint filename, never its media,
    // parent directory or original .ftproj. Damaged headers can be removed too.
    if (!QFile::remove(*path)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not remove the checkpoint."));
    }
    return {};
}

} // namespace ft::checkpoints
