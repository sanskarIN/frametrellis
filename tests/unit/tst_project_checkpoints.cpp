// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_checkpoints.h"
#include "project/project_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;
using namespace ft::checkpoints;

namespace {

QString snapshotPath(const Store& store, const Id& projectId, const Id& checkpointId)
{
    return QDir(QDir(store.root()).filePath(projectId))
        .filePath(checkpointId + QStringLiteral(".ftcheckpoint"));
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool changeHeader(const QString& path, const QString& field, const QJsonValue& value)
{
    const QByteArray bytes = readBytes(path);
    const qsizetype newline = bytes.indexOf('\n');
    if (newline < 0) {
        return false;
    }
    const auto doc = QJsonDocument::fromJson(bytes.left(newline));
    if (!doc.isObject()) {
        return false;
    }
    QJsonObject header = doc.object();
    header[field] = value;
    return writeBytes(path,
                      QJsonDocument(header).toJson(QJsonDocument::Compact) + bytes.mid(newline));
}

Project sampleProject(const QString& path)
{
    ProjectSettings settings;
    settings.width = 1280;
    settings.height = 720;
    settings.frameRate = *FrameRate::create(30000, 1001);
    Project project = Project::createNew(QStringLiteral("Checkpoint project"), settings);
    MediaAsset asset;
    asset.id = QStringLiteral("still");
    asset.filePath = path;
    asset.name = QStringLiteral("Cover");
    asset.info.kind = MediaKind::Image;
    asset.info.hasVideo = true;
    asset.info.width = 1280;
    asset.info.height = 720;
    if (!project.addAsset(asset)) {
        return Project{};
    }
    Clip clip;
    clip.id = QStringLiteral("first-clip");
    clip.assetId = asset.id;
    clip.start = 10;
    clip.duration = 50;
    if (!project.timeline().insertClip(project.timeline().tracks()[0].id, clip,
                                       project.sourceLookup())) {
        return Project{};
    }
    if (!project.setMarkers({{QStringLiteral("intro"), 10, QStringLiteral("Intro")}})) {
        return Project{};
    }
    SubtitleTrack subtitles;
    subtitles.cues = {{QStringLiteral("caption"), 400, 900, QStringLiteral("Opening title")}};
    if (!project.setSubtitles(subtitles)) {
        return Project{};
    }
    return project;
}

} // namespace

class ProjectCheckpointsTest : public QObject
{
    Q_OBJECT

private slots:
    void listingAnEmptyHistoryDoesNotCreateDirectories()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(QStringLiteral("history"));
        Store store(root);
        const Project project = Project::createNew(QStringLiteral("Empty history"), {});
        const auto history = store.list(project.id());
        QVERIFY(history);
        QVERIFY(history->entries.empty());
        QVERIFY(!history->truncated);
        QVERIFY(!QFileInfo::exists(root));
    }

    void immutableSnapshotsPreserveProjectAndOriginalMedia()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString media = dir.filePath(QStringLiteral("source.png"));
        const QString original = dir.filePath(QStringLiteral("original.ftproj"));
        QVERIFY(writeBytes(media, "original image"));
        Project project = sampleProject(media);
        QVERIFY(project.validate());
        QVERIFY(project_format::saveToFile(project, original));
        const QByteArray savedDocument = readBytes(original);
        const QByteArray oldPayload = project_format::toBytes(project);
        const Project before = project;

        Store store(dir.filePath(QStringLiteral("history")));
        const auto first = store.create(project, QStringLiteral("Before edits"), original);
        QVERIFY2(first, qPrintable(first ? QString() : first.error().toString()));
        QCOMPARE(project, before);
        QCOMPARE(first->label, QStringLiteral("Before edits"));
        QCOMPARE(first->projectId, project.id());
        QCOMPARE(first->originalPath, original);
        QVERIFY(first->createdAt.isValid());
        QVERIFY(first->bytes > 0);
        QCOMPARE(first->payloadSha256,
                 QCryptographicHash::hash(oldPayload, QCryptographicHash::Sha256).toHex());
        QVERIFY(QFileInfo(snapshotPath(store, project.id(), first->id)).isFile());

        project.setName(QStringLiteral("After edits"));
        QVERIFY(project.timeline().moveClip(QStringLiteral("first-clip"),
                                            project.timeline().tracks()[0].id, 25,
                                            project.sourceLookup()));
        const auto second = store.create(project, QStringLiteral("After edits"), original);
        QVERIFY(second);
        QVERIFY(second->id != first->id);

        const auto restoredFirst = store.restore(project.id(), first->id);
        const auto restoredSecond = store.restore(project.id(), second->id);
        QVERIFY(restoredFirst);
        QVERIFY(restoredSecond);
        QCOMPARE(project_format::toBytes(*restoredFirst), oldPayload);
        QCOMPARE(restoredSecond->name(), QStringLiteral("After edits"));
        QCOMPARE(restoredSecond->timeline().clip(QStringLiteral("first-clip"))->start,
                 FrameCount{25});
        QCOMPARE(restoredFirst->timeline().clip(QStringLiteral("first-clip"))->start,
                 FrameCount{10});
        QCOMPARE(readBytes(original), savedDocument);
        QCOMPARE(readBytes(media), QByteArray("original image"));

        QVERIFY(store.remove(project.id(), first->id));
        const auto history = store.list(project.id());
        QVERIFY(history);
        QCOMPARE(history->entries.size(), size_t{1});
        QCOMPARE(history->entries.front().id, second->id);
        QVERIFY(!store.restore(project.id(), first->id));
        QVERIFY(store.restore(project.id(), second->id));
        QCOMPARE(readBytes(original), savedDocument);
        QCOMPARE(readBytes(media), QByteArray("original image"));
    }

    void historyUsesMetadataTimestampAndDeterministicIdentityOrder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Ordered history"), {});
        const auto older = store.create(project, QStringLiteral("Older"));
        const auto newer = store.create(project, QStringLiteral("Newer"));
        const auto newest = store.create(project, QStringLiteral("Newest"));
        QVERIFY(older);
        QVERIFY(newer);
        QVERIFY(newest);
        QVERIFY(changeHeader(snapshotPath(store, project.id(), older->id),
                             QStringLiteral("createdAtUtc"),
                             QStringLiteral("2026-01-01T00:00:00.000Z")));
        QVERIFY(changeHeader(snapshotPath(store, project.id(), newer->id),
                             QStringLiteral("createdAtUtc"),
                             QStringLiteral("2026-01-02T00:00:00.000Z")));
        QVERIFY(changeHeader(snapshotPath(store, project.id(), newest->id),
                             QStringLiteral("createdAtUtc"),
                             QStringLiteral("2026-01-03T00:00:00.000Z")));
        const auto history = store.list(project.id());
        QVERIFY(history);
        QCOMPARE(history->entries.size(), size_t{3});
        QCOMPARE(history->entries[0].id, newest->id);
        QCOMPARE(history->entries[1].id, newer->id);
        QCOMPARE(history->entries[2].id, older->id);
    }

    void rejectsInvalidLabelsPathsAndUnreadableProjectPayloads()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.filePath(QStringLiteral("history")));
        Project project = Project::createNew(QStringLiteral("Validation"), {});
        for (const QString& label : {QString(), QStringLiteral(" \t "), QStringLiteral("a\nb"),
                                     QString(121, QLatin1Char('x'))}) {
            const auto rejected = store.create(project, label);
            QVERIFY(!rejected);
            QCOMPARE(rejected.error().code, ErrorCode::InvalidArgument);
        }
        QVERIFY(!store.create(project, QStringLiteral("Label"), QStringLiteral("relative.ftproj")));
        QVERIFY(!QFileInfo::exists(store.root()));

        MediaAsset source;
        source.id = QStringLiteral("bad-metadata");
        source.name = QStringLiteral("Bad metadata");
        source.filePath = dir.filePath(QStringLiteral("source.mp4"));
        source.info.kind = MediaKind::Video;
        source.info.hasVideo = true;
        source.info.width = 65537;
        source.info.durationUs = 5'000'000;
        QVERIFY(project.addAsset(source));
        const auto unreadable = store.create(project, QStringLiteral("Invalid payload"));
        QVERIFY(!unreadable);
        QCOMPARE(unreadable.error().code, ErrorCode::InvalidDocument);
        QVERIFY(!QFileInfo::exists(store.root()));

        QVERIFY(project.removeAsset(source.id));
        const auto valid = store.create(project, QStringLiteral("  Before edit  "));
        QVERIFY(valid);
        QCOMPARE(valid->label, QStringLiteral("Before edit"));
    }

    void historyIsIsolatedByProjectIdAndRejectsPathTraversal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project first = Project::createNew(QStringLiteral("First"), {});
        const Project second = Project::createNew(QStringLiteral("Second"), {});
        const auto one = store.create(first, QStringLiteral("One"));
        const auto two = store.create(second, QStringLiteral("Two"));
        QVERIFY(one);
        QVERIFY(two);
        const auto firstHistory = store.list(first.id());
        const auto secondHistory = store.list(second.id());
        QVERIFY(firstHistory);
        QVERIFY(secondHistory);
        QCOMPARE(firstHistory->entries.size(), size_t{1});
        QCOMPARE(secondHistory->entries.size(), size_t{1});
        QCOMPARE(firstHistory->entries.front().id, one->id);
        QCOMPARE(secondHistory->entries.front().id, two->id);

        QVERIFY(!store.restore(second.id(), one->id));
        QVERIFY(!store.remove(first.id(), two->id));
        QVERIFY(!store.list(QStringLiteral("../other")));
        QVERIFY(!store.restore(first.id(), QStringLiteral("../outside")));
        QVERIFY(!store.remove(first.id(), QStringLiteral("..")));
        QVERIFY(store.restore(first.id(), one->id));
        QVERIFY(store.restore(second.id(), two->id));
    }

    void damagedMetadataIsVisibleButCannotBeRestored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Damaged"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        QVERIFY(writeBytes(path, "not a checkpoint\n{}"));
        const auto history = store.list(project.id());
        QVERIFY(history);
        QCOMPARE(history->entries.size(), size_t{1});
        QVERIFY(!history->entries.front().readable);
        QVERIFY(!history->entries.front().error.isEmpty());
        QCOMPARE(history->entries.front().id, saved->id);
        QVERIFY(!store.restore(project.id(), saved->id));
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(store.remove(project.id(), saved->id));
        QVERIFY(!QFileInfo::exists(path));
    }

    void headerDiscoveryDoesNotDecodeTheProjectPayload()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Payload validation"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        const QByteArray bytes = readBytes(path);
        const qsizetype newline = bytes.indexOf('\n');
        QVERIFY(newline > 0);
        QVERIFY(writeBytes(path, bytes.left(newline + 1) + "{invalid JSON"));
        const auto history = store.list(project.id());
        QVERIFY(history);
        QCOMPARE(history->entries.size(), size_t{1});
        QVERIFY(history->entries.front().readable);
        const auto restored = store.restore(project.id(), saved->id);
        QVERIFY(!restored);
        QCOMPARE(restored.error().code, ErrorCode::InvalidDocument);
        QVERIFY(restored.error().message.contains(QStringLiteral("integrity")));
        QVERIFY(QFileInfo::exists(path));
    }

    void rejectsUnsupportedVersionAndMetadataIdentityMismatch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Format validation"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        const QByteArray bytes = readBytes(path);
        QVERIFY(changeHeader(path, QStringLiteral("version"), 2));
        const auto unsupported = store.restore(project.id(), saved->id);
        QVERIFY(!unsupported);
        QCOMPARE(unsupported.error().code, ErrorCode::UnsupportedVersion);
        QVERIFY(writeBytes(path, bytes));
        QVERIFY(changeHeader(path, QStringLiteral("projectId"), QStringLiteral("other")));
        const auto mismatched = store.restore(project.id(), saved->id);
        QVERIFY(!mismatched);
        QCOMPARE(mismatched.error().code, ErrorCode::InvalidDocument);
        QVERIFY(writeBytes(path, bytes));
        QVERIFY(changeHeader(path, QStringLiteral("checkpointId"), QStringLiteral("wrong")));
        QVERIFY(!store.restore(project.id(), saved->id));
    }

    void verifiesPayloadIdentityAgainstTheHeader()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project first = Project::createNew(QStringLiteral("First"), {});
        const Project second = Project::createNew(QStringLiteral("Second"), {});
        const auto saved = store.create(first, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, first.id(), saved->id);
        const QByteArray bytes = readBytes(path);
        const qsizetype newline = bytes.indexOf('\n');
        QVERIFY(newline > 0);
        const QByteArray payload = project_format::toBytes(second);
        QVERIFY(writeBytes(path, bytes.left(newline + 1) + payload));
        // Update the digest to exercise identity validation independently.
        QVERIFY(changeHeader(
            path, QStringLiteral("payloadSha256"),
            QString::fromLatin1(
                QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())));
        const auto restored = store.restore(first.id(), saved->id);
        QVERIFY(!restored);
        QCOMPARE(restored.error().code, ErrorCode::InvalidDocument);
        QVERIFY(QFileInfo::exists(path));
    }

    void rejectsValidJsonPayloadWithUnrecordedEdits()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Original version"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        const QByteArray bytes = readBytes(path);
        const qsizetype newline = bytes.indexOf('\n');
        QVERIFY(newline > 0);

        // Preserve the project ID and name while changing the actual edit.
        // Ordinary schema checks would accept this valid project payload.
        Project modified = project;
        QVERIFY(modified.timeline().renameTrack(modified.timeline().tracks()[0].id,
                                                QStringLiteral("Unexpected change")));
        const QByteArray different = project_format::toBytes(modified);
        QVERIFY(project_format::fromBytes(different));
        QVERIFY(writeBytes(path, bytes.left(newline + 1) + different));

        const auto history = store.list(project.id());
        QVERIFY(history);
        QVERIFY(history->entries.front().readable);
        const auto rejected = store.restore(project.id(), saved->id);
        QVERIFY(!rejected);
        QCOMPARE(rejected.error().code, ErrorCode::InvalidDocument);
        QVERIFY(rejected.error().message.contains(QStringLiteral("integrity")));
        QVERIFY(QFileInfo(path).isFile());

        // Restoring the exact original bytes makes the same checkpoint usable.
        QVERIFY(writeBytes(path, bytes));
        const auto restored = store.restore(project.id(), saved->id);
        QVERIFY(restored);
        QCOMPARE(project_format::toBytes(*restored), project_format::toBytes(project));
    }

    void rejectsMalformedPayloadDigestMetadata()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Digest validation"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        const QByteArray original = readBytes(path);

        const QList<QJsonValue> invalid{
            QJsonValue(QJsonValue::Null),
            QJsonValue(123),
            QJsonValue(QString()),
            QJsonValue(QString(63, QLatin1Char('a'))),
            QJsonValue(QString(65, QLatin1Char('a'))),
            QJsonValue(QString(64, QLatin1Char('g'))),
            QJsonValue(QString(64, QLatin1Char('A'))),
        };
        for (const QJsonValue& digest : invalid) {
            QVERIFY(writeBytes(path, original));
            QVERIFY(changeHeader(path, QStringLiteral("payloadSha256"), digest));
            const auto history = store.list(project.id());
            QVERIFY(history);
            QCOMPARE(history->entries.size(), size_t{1});
            QVERIFY(!history->entries.front().readable);
            const auto restored = store.restore(project.id(), saved->id);
            QVERIFY(!restored);
            QCOMPARE(restored.error().code, ErrorCode::InvalidDocument);
        }
        QVERIFY(writeBytes(path, original));
        QVERIFY(store.restore(project.id(), saved->id));
    }

    void refusesOversizedAndUnterminatedHeaders()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Bounds"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        QVERIFY(writeBytes(path, QByteArray(kMaxHeaderBytes + 1, 'x') + "\n{}"));
        const auto oversized = store.restore(project.id(), saved->id);
        QVERIFY(!oversized);
        QCOMPARE(oversized.error().code, ErrorCode::InvalidDocument);
        QVERIFY(writeBytes(path, "unterminated metadata"));
        QVERIFY(!store.restore(project.id(), saved->id));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(project_format::kMaxDocumentBytes + kMaxHeaderBytes + 1));
        file.close();
        QVERIFY(!store.restore(project.id(), saved->id));
        QVERIFY(store.remove(project.id(), saved->id));
    }

    void serializesConcurrentWritersAndRemovers()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Locking"), {});
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        QLockFile lock(
            QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral(".checkpoint.lock")));
        QVERIFY(lock.tryLock(0));
        const auto concurrent = store.create(project, QStringLiteral("Another"));
        QVERIFY(!concurrent);
        QCOMPARE(concurrent.error().code, ErrorCode::Conflict);
        const Status removal = store.remove(project.id(), saved->id);
        QVERIFY(!removal);
        QCOMPARE(removal.error().code, ErrorCode::Conflict);
        QVERIFY(store.restore(project.id(), saved->id));
        lock.unlock();
        QVERIFY(store.remove(project.id(), saved->id));
    }

    void neverPrunesExistingVersionsWhenTheLimitIsReached()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Store store(dir.path());
        const Project project = Project::createNew(QStringLiteral("Full history"), {});
        const auto first = store.create(project, QStringLiteral("Keep me"));
        QVERIFY(first);
        // Synthesize valid immutable headers with independent identities.
        // This exercises the capacity check without 100 repeated filesystem scans.
        const QString firstPath = snapshotPath(store, project.id(), first->id);
        const QByteArray firstBytes = readBytes(firstPath);
        const qsizetype newline = firstBytes.indexOf('\n');
        QVERIFY(newline > 0);
        const QJsonObject originalHeader =
            QJsonDocument::fromJson(firstBytes.left(newline)).object();
        for (int i = 1; i < kMaxCheckpointsPerProject; ++i) {
            const Id id = QStringLiteral("fixture-%1").arg(i);
            QJsonObject header = originalHeader;
            header[QStringLiteral("checkpointId")] = id;
            QVERIFY(writeBytes(snapshotPath(store, project.id(), id),
                               QJsonDocument(header).toJson(QJsonDocument::Compact) +
                                   firstBytes.mid(newline)));
        }
        const auto full = store.list(project.id());
        QVERIFY(full);
        QCOMPARE(full->entries.size(), size_t(kMaxCheckpointsPerProject));
        const auto rejected = store.create(project, QStringLiteral("Over limit"));
        QVERIFY(!rejected);
        QCOMPARE(rejected.error().code, ErrorCode::OutOfRange);
        QCOMPARE(readBytes(firstPath), firstBytes);
        QVERIFY(store.remove(project.id(), QStringLiteral("fixture-1")));
        QVERIFY(store.create(project, QStringLiteral("New version")));
        QVERIFY(store.restore(project.id(), first->id));
    }

    void rejectsStorageFilesAndCheckpointDirectories()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString occupied = dir.filePath(QStringLiteral("occupied"));
        QVERIFY(writeBytes(occupied, "keep"));
        const Project project = Project::createNew(QStringLiteral("Paths"), {});
        Store badStore(occupied);
        QVERIFY(!badStore.list(project.id()));
        QVERIFY(!badStore.create(project, QStringLiteral("Version")));
        QCOMPARE(readBytes(occupied), QByteArray("keep"));
        QVERIFY(!Store(QString()).list(project.id()));
        QVERIFY(!Store(QStringLiteral("relative")).list(project.id()));

        Store store(dir.filePath(QStringLiteral("history")));
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkpath(path));
        QVERIFY(!store.restore(project.id(), saved->id));
        QVERIFY(!store.remove(project.id(), saved->id));
        QVERIFY(QFileInfo(path).isDir());
    }

    void refusesSymlinkedCheckpointAndProjectDirectory()
    {
#ifndef Q_OS_WIN
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outside = dir.filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(outside));
        const Project project = Project::createNew(QStringLiteral("Links"), {});
        Store store(dir.filePath(QStringLiteral("history")));
        const auto saved = store.create(project, QStringLiteral("Version"));
        QVERIFY(saved);
        const QString path = snapshotPath(store, project.id(), saved->id);
        const QString target = QDir(outside).filePath(QStringLiteral("important.txt"));
        QVERIFY(writeBytes(target, "keep me"));
        QVERIFY(QFile::remove(path));
        QVERIFY(QFile::link(target, path));
        QVERIFY(QFileInfo(path).isSymLink());
        QVERIFY(!store.restore(project.id(), saved->id));
        QVERIFY(!store.remove(project.id(), saved->id));
        QCOMPARE(readBytes(target), QByteArray("keep me"));
        const auto history = store.list(project.id());
        QVERIFY(history);
        QVERIFY(history->entries.empty());

        const Project other = Project::createNew(QStringLiteral("Other"), {});
        const QString projectDir = QDir(store.root()).filePath(other.id());
        QVERIFY(QFile::link(outside, projectDir));
        QVERIFY(QFileInfo(projectDir).isSymLink());
        QVERIFY(!store.list(other.id()));
        QVERIFY(!store.create(other, QStringLiteral("Version")));
        QCOMPARE(readBytes(target), QByteArray("keep me"));
#else
        QSKIP("POSIX symlinks require no Windows link-creation privileges.");
#endif
    }
};

QTEST_GUILESS_MAIN(ProjectCheckpointsTest)
#include "tst_project_checkpoints.moc"
