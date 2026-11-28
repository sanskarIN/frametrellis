// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_bundle.h"
#include "project/project_serializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;
using namespace ft::project_bundle;

namespace {

bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

MediaAsset image(const Id& id, const QString& path)
{
    MediaAsset asset;
    asset.id = id;
    asset.name = QFileInfo(path).fileName();
    asset.filePath = path;
    asset.info.kind = MediaKind::Image;
    asset.info.hasVideo = true;
    asset.info.width = 640;
    asset.info.height = 360;
    return asset;
}

bool addClip(Project& project, const Id& assetId, FrameCount start)
{
    Clip clip;
    clip.id = createId();
    clip.assetId = assetId;
    clip.start = start;
    clip.duration = 25;
    return project.timeline()
        .insertClip(project.timeline().tracks().front().id, clip, project.sourceLookup())
        .ok();
}

bool noStaging(const QString& parent)
{
    return QDir(parent)
        .entryList({QStringLiteral(".ft-bundle-*")},
                   QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
        .isEmpty();
}

} // namespace

class ProjectBundleTest : public QObject
{
    Q_OBJECT

private slots:
    void createsIndependentBundleWithExactMediaAndEditingState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("a.png"));
        const QString b = dir.filePath(QStringLiteral("b.png"));
        QVERIFY(write(a, "first image"));
        QVERIFY(write(b, "second image"));
        Project project = Project::createNew(QStringLiteral("Original"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), a)));
        QVERIFY(project.addAsset(image(QStringLiteral("b"), b)));
        QVERIFY(addClip(project, QStringLiteral("a"), 0));
        QVERIFY(addClip(project, QStringLiteral("b"), 40));
        QVERIFY(project.setMarkers({{QStringLiteral("marker"), 40, QStringLiteral("Cut")}}));
        SubtitleTrack captions;
        captions.cues = {{QStringLiteral("cue"), 0, 500, QStringLiteral("Hello")}};
        QVERIFY(project.setSubtitles(captions));
        const Project before = project;
        const auto output = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY2(output, qPrintable(output ? QString() : output.error().toString()));
        QCOMPARE(output->files, 2);
        QCOMPARE(output->bytes, qint64{23});
        QVERIFY(output->projectId != project.id());
        const auto opened = project_format::loadFromFile(output->projectFile);
        QVERIFY(opened);
        QCOMPARE(opened->id(), output->projectId);
        QCOMPARE(opened->name(), project.name());
        QCOMPARE(opened->timeline(), project.timeline());
        QCOMPARE(opened->markers(), project.markers());
        QCOMPARE(opened->subtitles(), project.subtitles());
        QCOMPARE(opened->settings(), project.settings());
        QCOMPARE(opened->media().size(), size_t{2});
        QVERIFY(opened->media()[0].filePath.startsWith(output->directory));
        QVERIFY(opened->media()[1].filePath.startsWith(output->directory));
        QCOMPARE(read(opened->media()[0].filePath), QByteArray("first image"));
        QCOMPARE(read(opened->media()[1].filePath), QByteArray("second image"));
        QCOMPARE(project, before);
        QCOMPARE(read(a), QByteArray("first image"));
        QCOMPARE(read(b), QByteArray("second image"));
        const auto verification = verify(output->directory);
        QVERIFY(verification);
        QVERIFY(verification->projectMatches);
        QVERIFY(verification->problems.isEmpty());
        QCOMPARE(verification->files, 2);
        QCOMPARE(verification->bytes, output->bytes);
        QVERIFY(noStaging(dir.path()));
    }

    void copiedBundleUsesOwnMediaEvenWhileOriginalBundleExists()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "source"));
        Project project = Project::createNew(QStringLiteral("Copy"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        QVERIFY(addClip(project, QStringLiteral("a"), 0));
        const auto first = create(project, dir.filePath(QStringLiteral("one")));
        QVERIFY(first);
        const QString second = dir.filePath(QStringLiteral("two"));
        QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("media"))));
        QVERIFY(QFile::copy(first->projectFile,
                            QDir(second).filePath(QStringLiteral("project.ftproj"))));
        QVERIFY(QFile::copy(QDir(first->directory).filePath(QStringLiteral("manifest.json")),
                            QDir(second).filePath(QStringLiteral("manifest.json"))));
        const auto initial = project_format::loadFromFile(first->projectFile);
        QVERIFY(initial);
        const QString relative =
            QDir(first->directory).relativeFilePath(initial->media().front().filePath);
        QVERIFY(QFile::copy(initial->media().front().filePath, QDir(second).filePath(relative)));
        const auto copied =
            project_format::loadFromFile(QDir(second).filePath(QStringLiteral("project.ftproj")));
        QVERIFY(copied);
        QCOMPARE(copied->media().front().filePath, QDir(second).filePath(relative));
        QVERIFY(copied->media().front().filePath != initial->media().front().filePath);
        QVERIFY(QFileInfo::exists(initial->media().front().filePath));
        const auto check = verify(second);
        QVERIFY(check);
        QVERIFY(check->problems.isEmpty());
    }

    void regularSaveRetainsRelativeMediaForPortableProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "source"));
        Project project = Project::createNew(QStringLiteral("Portable save"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        const auto opened = project_format::loadFromFile(bundle->projectFile);
        QVERIFY(opened);
        QVERIFY(opened->relativeMediaPaths());
        Project edited = *opened;
        edited.setName(QStringLiteral("New name"));
        QVERIFY(project_format::saveToFile(edited, bundle->projectFile));
        const QJsonObject doc = QJsonDocument::fromJson(read(bundle->projectFile)).object();
        const auto body = doc.value(QStringLiteral("project")).toObject();
        QCOMPARE(doc.value(QStringLiteral("version")).toInt(), 8);
        QVERIFY(body.value(QStringLiteral("relativeMediaPaths")).toBool());
        QJsonObject downgraded = doc;
        downgraded[QStringLiteral("version")] = 7;
        const auto unsupported = project_format::fromJson(downgraded, bundle->directory);
        QVERIFY(!unsupported);
        QCOMPARE(unsupported.error().code, ErrorCode::InvalidDocument);
        const auto media = body.value(QStringLiteral("media")).toArray().first().toObject();
        const QString relative = media.value(QStringLiteral("path")).toString();
        QVERIFY(QDir::isRelativePath(relative));
        QVERIFY(relative.startsWith(QLatin1String("media/")));
        const QString relocated = dir.filePath(QStringLiteral("relocated"));
        QVERIFY(QDir().mkpath(QDir(relocated).filePath(QStringLiteral("media"))));
        QVERIFY(QFile::copy(bundle->projectFile,
                            QDir(relocated).filePath(QStringLiteral("project.ftproj"))));
        QVERIFY(QFile::copy(edited.media().front().filePath, QDir(relocated).filePath(relative)));
        const auto loaded = project_format::loadFromFile(
            QDir(relocated).filePath(QStringLiteral("project.ftproj")));
        QVERIFY(loaded);
        QCOMPARE(loaded->name(), edited.name());
        QCOMPARE(loaded->media().front().filePath, QDir(relocated).filePath(relative));

        // Snapshot serialization without a directory retains absolute paths,
        // so recovery and checkpoint files can still refer to the media.
        const auto snapshot = project_format::fromBytes(project_format::toBytes(*loaded));
        QVERIFY(snapshot);
        QVERIFY(snapshot->relativeMediaPaths());
        QCOMPARE(snapshot->media().front().filePath, loaded->media().front().filePath);
    }

    void portablePrimaryPathCannotEscapeProjectDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "source"));
        Project project = Project::createNew(QStringLiteral("Path validation"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        QJsonObject doc = QJsonDocument::fromJson(read(bundle->projectFile)).object();
        const auto original = doc;
        QVERIFY(!project_format::fromJson(original)); // requires a document directory
        QJsonObject body = doc.value(QStringLiteral("project")).toObject();
        QJsonArray media = body.value(QStringLiteral("media")).toArray();
        QJsonObject entry = media.first().toObject();
        entry[QStringLiteral("path")] = QStringLiteral("../source.png");
        media[0] = entry;
        body[QStringLiteral("media")] = media;
        doc[QStringLiteral("project")] = body;
        QVERIFY(!project_format::fromJson(doc, bundle->directory));
        QCOMPARE(read(source), QByteArray("source"));
    }

    void usedOnlyExcludesMissingUnusedMediaButKeepsTimelineAndOriginalBin()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "used"));
        Project project = Project::createNew(QStringLiteral("Used only"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("used"), source)));
        QVERIFY(project.addAsset(
            image(QStringLiteral("unused"), dir.filePath(QStringLiteral("missing.png")))));
        QVERIFY(addClip(project, QStringLiteral("used"), 0));
        const auto all = plan(project);
        QVERIFY(!all);
        QCOMPARE(all.error().code, ErrorCode::NotFound);
        const auto used = plan(project, Options{true});
        QVERIFY(used);
        QCOMPARE(used->files.size(), size_t{1});
        QCOMPARE(used->excludedAssets, 1);
        const auto output = create(project, dir.filePath(QStringLiteral("bundle")), Options{true});
        QVERIFY(output);
        const auto opened = project_format::loadFromFile(output->projectFile);
        QVERIFY(opened);
        QCOMPARE(opened->media().size(), size_t{1});
        QCOMPARE(opened->timeline(), project.timeline());
        QCOMPARE(project.media().size(), size_t{2});
    }

    void missingOrChangedMediaNeverPublishesPartialBundle()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "original"));
        Project project = Project::createNew(QStringLiteral("Changed"), {});
        auto asset = image(QStringLiteral("a"), source);
        asset.info.fileSize = QFileInfo(source).size();
        asset.info.lastModified = QFileInfo(source).lastModified();
        QVERIFY(project.addAsset(asset));
        QVERIFY(write(source, "new bytes and length"));
        const QString output = dir.filePath(QStringLiteral("bundle"));
        const auto changed = create(project, output);
        QVERIFY(!changed);
        QCOMPARE(changed.error().code, ErrorCode::Conflict);
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(noStaging(dir.path()));
        QVERIFY(QFile::remove(source));
        const auto missing = create(project, output);
        QVERIFY(!missing);
        QCOMPARE(missing.error().code, ErrorCode::NotFound);
        QVERIFY(!QFileInfo::exists(output));
    }

    void rejectsSameSecondFingerprintChangesButAcceptsLegacyTimestamp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "original"));
        const QFileInfo info(source);
        const QDateTime actual = info.lastModified();
        QVERIFY(actual.isValid());
        Project project = Project::createNew(QStringLiteral("Fingerprint"), {});
        auto asset = image(QStringLiteral("a"), source);
        asset.info.fileSize = info.size();
        // A stored millisecond difference should be detected even when the
        // filesystem and the source agree on the whole-second timestamp.
        asset.info.lastModified = actual.addMSecs(actual.time().msec() == 999 ? -1 : 1);
        QCOMPARE(asset.info.lastModified.toSecsSinceEpoch(), actual.toSecsSinceEpoch());
        QVERIFY(project.addAsset(asset));
        const auto changed = plan(project);
        QVERIFY(!changed);
        QCOMPARE(changed.error().code, ErrorCode::Conflict);

        // Legacy timestamps have no subsecond precision and must remain usable.
        asset.info.lastModified = actual.addMSecs(-actual.time().msec());
        QVERIFY(project.replaceAsset(asset));
        const auto legacy = plan(project);
        QVERIFY(legacy);
        QCOMPARE(legacy->files.size(), size_t{1});
        const auto copied = create(project, dir.filePath(QStringLiteral("legacy")));
        QVERIFY(copied);
        QCOMPARE(read(source), QByteArray("original"));
    }

    void detectsSourceMutationDuringCopyAndCleansStaging()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, QByteArray(3 * 1024 * 1024, 'a')));
        Project project = Project::createNew(QStringLiteral("Changing source"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        bool changed = false;
        const QString target = dir.filePath(QStringLiteral("bundle"));
        const auto result = create(project, target, {}, {}, [&](const Progress& progress) {
            if (progress.bytesCopied > 0 && !changed) {
                changed = write(source, QByteArray("modified"));
            }
        });
        QVERIFY(changed);
        QVERIFY(!result);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(noStaging(dir.path()));
        QCOMPARE(read(source), QByteArray("modified"));
    }

    void cancellationDuringCopyRemovesTemporaryFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("large.png"));
        const QByteArray data(3 * 1024 * 1024, 'x');
        QVERIFY(write(source, data));
        Project project = Project::createNew(QStringLiteral("Cancel"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        bool cancel = false;
        bool copiedChunk = false;
        const QString destination = dir.filePath(QStringLiteral("bundle"));
        const auto output = create(
            project, destination, {}, [&] { return cancel; },
            [&](const Progress& progress) {
                if (progress.bytesCopied > 0) {
                    copiedChunk = true;
                    cancel = true;
                }
            });
        QVERIFY(copiedChunk);
        QVERIFY(!output);
        QCOMPARE(output.error().code, ErrorCode::Cancelled);
        QVERIFY(!QFileInfo::exists(destination));
        QVERIFY(noStaging(dir.path()));
        QCOMPARE(read(source), data);
    }

    void rejectsExistingFolderAndPreservesItsContents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Project project = Project::createNew(QStringLiteral("Existing"), {});
        const QString target = dir.filePath(QStringLiteral("existing"));
        QVERIFY(QDir().mkpath(target));
        const QString important = QDir(target).filePath(QStringLiteral("important.txt"));
        QVERIFY(write(important, "keep"));
        const auto output = create(project, target);
        QVERIFY(!output);
        QCOMPARE(output.error().code, ErrorCode::Conflict);
        QCOMPARE(read(important), QByteArray("keep"));
        QVERIFY(!create(project, QStringLiteral("relative")));
        QVERIFY(!create(project, dir.filePath(QStringLiteral("missing/child"))));
        QVERIFY(noStaging(dir.path()));
    }

    void destinationCreatedDuringCopyIsNeverReplaced()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "copy me"));
        Project project = Project::createNew(QStringLiteral("Collision"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const QString destination = dir.filePath(QStringLiteral("bundle"));
        bool created = false;
        const auto output = create(project, destination, {}, {}, [&](const Progress& progress) {
            if (progress.phase == Phase::Finalizing && !created) {
                created = QDir().mkdir(destination);
                if (created) {
                    created = write(QDir(destination).filePath(QStringLiteral("keep.txt")), "keep");
                }
            }
        });
        QVERIFY(created);
        QVERIFY(!output);
        QCOMPARE(read(QDir(destination).filePath(QStringLiteral("keep.txt"))), QByteArray("keep"));
        QVERIFY(noStaging(dir.path()));
    }

    void verifiesSameSizeTamperingAndEditedProjectSeparately()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "abcd"));
        Project project = Project::createNew(QStringLiteral("Integrity"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        const auto opened = project_format::loadFromFile(bundle->projectFile);
        QVERIFY(opened);
        QVERIFY(write(opened->media().front().filePath, "wxyz"));
        const auto damaged = verify(bundle->directory);
        QVERIFY(damaged);
        QVERIFY(damaged->projectMatches);
        QCOMPARE(damaged->problems.size(), qsizetype{1});
        QVERIFY(damaged->problems.front().startsWith(QLatin1String("media/")));
        QVERIFY(write(opened->media().front().filePath, "abcd"));
        Project edited = *opened;
        edited.setName(QStringLiteral("Edited"));
        QVERIFY(project_format::saveToFile(edited, bundle->projectFile));
        const auto changed = verify(bundle->directory);
        QVERIFY(changed);
        QVERIFY(!changed->projectMatches);
        QCOMPARE(changed->problems, QStringList{QStringLiteral("project.ftproj")});
    }

    void rejectsManifestTraversalBeforeReadingAnyFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "media"));
        Project project = Project::createNew(QStringLiteral("Manifest"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        const QString path = QDir(bundle->directory).filePath(QStringLiteral("manifest.json"));
        QJsonObject manifest = QJsonDocument::fromJson(read(path)).object();
        QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
        QJsonObject entry = files[0].toObject();
        entry[QStringLiteral("path")] = QStringLiteral("media/../../source.png");
        files[0] = entry;
        manifest[QStringLiteral("files")] = files;
        QVERIFY(write(path, QJsonDocument(manifest).toJson()));
        const auto invalid = verify(bundle->directory);
        QVERIFY(!invalid);
        QCOMPARE(invalid.error().code, ErrorCode::InvalidDocument);
        QCOMPARE(read(source), QByteArray("media"));
    }

    void rejectsManifestWithOmittedMediaRecord()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(write(source, "source"));
        Project project = Project::createNew(QStringLiteral("Completeness"), {});
        QVERIFY(project.addAsset(image(QStringLiteral("a"), source)));
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        const QString path = QDir(bundle->directory).filePath(QStringLiteral("manifest.json"));
        QJsonObject manifest = QJsonDocument::fromJson(read(path)).object();
        manifest[QStringLiteral("files")] = QJsonArray();
        QVERIFY(write(path, QJsonDocument(manifest).toJson()));
        const auto incomplete = verify(bundle->directory);
        QVERIFY(!incomplete);
        QCOMPARE(incomplete.error().code, ErrorCode::InvalidDocument);
    }

    void emptyProjectCreatesValidBundle()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Project project = Project::createNew(QStringLiteral("Empty"), {});
        const auto bundle = create(project, dir.filePath(QStringLiteral("bundle")));
        QVERIFY(bundle);
        QCOMPARE(bundle->files, 0);
        QCOMPARE(bundle->bytes, qint64{0});
        const auto verified = verify(bundle->directory);
        QVERIFY(verified);
        QVERIFY(verified->projectMatches);
        QVERIFY(verified->problems.isEmpty());
        QVERIFY(!verify(bundle->directory, [] { return true; }));
    }
};

QTEST_GUILESS_MAIN(ProjectBundleTest)
#include "tst_project_bundle.moc"
