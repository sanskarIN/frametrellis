// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/media_audit.h"
#include "project/project_serializer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace ft;

namespace {

MediaAsset makeAsset(const QString& id, const QString& path, MediaKind kind,
                     std::int64_t durationUs)
{
    MediaAsset a;
    a.id = id;
    a.filePath = path;
    a.name = QFileInfo(path).fileName();
    a.info.kind = kind;
    a.info.durationUs = durationUs;
    a.info.hasVideo = kind != MediaKind::Audio;
    a.info.hasAudio = kind != MediaKind::Image;
    a.info.width = kind == MediaKind::Audio ? 0 : 1280;
    a.info.height = kind == MediaKind::Audio ? 0 : 720;
    a.info.frameRate = kind == MediaKind::Video ? Rational(30000, 1001) : Rational();
    a.info.audioChannels = kind == MediaKind::Image ? 0 : 2;
    a.info.audioSampleRate = kind == MediaKind::Image ? 0 : 48000;
    a.info.videoCodec = QStringLiteral("h264");
    a.info.fileSize = 12345;
    a.info.lastModified = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
    return a;
}

Project makeProject(const QString& mediaDir)
{
    ProjectSettings settings;
    settings.width = 1280;
    settings.height = 720;
    settings.frameRate = *FrameRate::create(30000, 1001);
    Project p = Project::createNew(QStringLiteral("Round Trip"), settings);
    (void)p.addAsset(makeAsset(QStringLiteral("m1"), mediaDir + QStringLiteral("/clip.mp4"),
                               MediaKind::Video, 10'000'000));
    (void)p.addAsset(makeAsset(QStringLiteral("m2"), mediaDir + QStringLiteral("/music.wav"),
                               MediaKind::Audio, 60'000'000));
    (void)p.addAsset(makeAsset(QStringLiteral("m3"), mediaDir + QStringLiteral("/logo.png"),
                               MediaKind::Image, 0));
    const Id v1 = p.timeline().tracks()[0].id;
    const Id a1 = p.timeline().tracks()[2].id;
    Clip c;
    c.id = QStringLiteral("clip1");
    c.assetId = QStringLiteral("m1");
    c.start = 15;
    c.sourceIn = 30;
    c.duration = 120;
    c.properties.gain = 0.75;
    c.properties.fadeIn = 10;
    c.properties.video.saturation = 1.5;
    (void)p.timeline().insertClip(v1, c, p.sourceLookup());
    Clip next = c;
    next.id = QStringLiteral("clip4");
    next.start = c.end();
    next.sourceIn = 200;
    next.duration = 40;
    next.properties = {};
    (void)p.timeline().insertClip(v1, next, p.sourceLookup());
    (void)p.timeline().addTransition(c.id, 20, p.sourceLookup(), QStringLiteral("fade1"));
    Clip still;
    still.id = QStringLiteral("clip2");
    still.assetId = QStringLiteral("m3");
    still.start = 200;
    still.duration = 150;
    (void)p.timeline().insertClip(v1, still, p.sourceLookup());
    Clip music;
    music.id = QStringLiteral("clip3");
    music.assetId = QStringLiteral("m2");
    music.duration = 300;
    music.properties.muted = true;
    (void)p.timeline().insertClip(a1, music, p.sourceLookup());
    (void)p.timeline().setTrackLocked(a1, true);
    return p;
}

QJsonObject validDocument()
{
    return project_format::toJson(makeProject(QStringLiteral("/media")));
}

QJsonObject& projectBody(QJsonObject& root, QJsonObject& body)
{
    body = root.value(QLatin1String("project")).toObject();
    return body;
}

} // namespace

class ProjectFormatTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesEverything()
    {
        const Project original = makeProject(QStringLiteral("/media"));
        QVERIFY(original.validate());
        const QByteArray bytes = project_format::toBytes(original);
        Result<Project> loaded = project_format::fromBytes(bytes);
        QVERIFY2(loaded, qPrintable(loaded ? QString() : loaded.error().toString()));
        // Media status is runtime state and not persisted; everything else must match.
        QCOMPARE(loaded->timeline(), original.timeline());
        QCOMPARE(loaded->settings(), original.settings());
        QCOMPARE(loaded->name(), original.name());
        QCOMPARE(loaded->id(), original.id());
        QCOMPARE(loaded->media().size(), original.media().size());
        for (size_t i = 0; i < original.media().size(); ++i) {
            QCOMPARE(loaded->media()[i].info, original.media()[i].info);
            QCOMPARE(loaded->media()[i].filePath, original.media()[i].filePath);
        }
        // Serialising again yields identical bytes (stable output for version control).
        QCOMPARE(project_format::toBytes(*loaded), bytes);
    }

    void saveAndLoadFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const Project original = makeProject(dir.path() + QStringLiteral("/media"));
        const QString file = dir.filePath(QStringLiteral("edit.ftproj"));
        QVERIFY(project_format::saveToFile(original, file));
        Result<Project> loaded = project_format::loadFromFile(file);
        QVERIFY(loaded);
        QCOMPARE(loaded->timeline(), original.timeline());
        // The relative media path is stored for relocation.
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonObject media = root[QLatin1String("project")]
                                      .toObject()[QLatin1String("media")]
                                      .toArray()[0]
                                      .toObject();
        QCOMPARE(media[QLatin1String("relativePath")].toString(), QStringLiteral("media/clip.mp4"));
    }

    void movedProjectFolderResolvesRelativePaths()
    {
        QTemporaryDir oldRoot;
        QTemporaryDir newRoot;
        QVERIFY(oldRoot.isValid() && newRoot.isValid());
        // The project and its media folder are moved together to a new location.
        QVERIFY(QDir(newRoot.path()).mkpath(QStringLiteral("media")));
        QFile media(newRoot.filePath(QStringLiteral("media/clip.mp4")));
        QVERIFY(media.open(QIODevice::WriteOnly));
        media.write("not really video");
        media.close();
        const Project original = makeProject(oldRoot.path() + QStringLiteral("/media"));
        const QByteArray bytes = project_format::toBytes(original, oldRoot.path());
        Result<Project> loaded = project_format::fromBytes(bytes, newRoot.path());
        QVERIFY(loaded);
        QCOMPARE(loaded->asset(QStringLiteral("m1"))->filePath,
                 QDir::cleanPath(newRoot.filePath(QStringLiteral("media/clip.mp4"))));
        // Files that cannot be found keep their original absolute path for relinking.
        QCOMPARE(loaded->asset(QStringLiteral("m2"))->filePath,
                 oldRoot.path() + QStringLiteral("/media/music.wav"));
    }

    void rejectsMalformedInput_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<int>("code");
        QTest::newRow("empty") << QByteArray() << int(ErrorCode::Parse);
        QTest::newRow("truncated") << QByteArray("{\"format\": ") << int(ErrorCode::Parse);
        QTest::newRow("array") << QByteArray("[1,2,3]") << int(ErrorCode::InvalidDocument);
        QTest::newRow("other format") << QByteArray(R"({"format":"something.else","version":1})")
                                      << int(ErrorCode::InvalidDocument);
        QTest::newRow("no project")
            << QByteArray(R"({"format":"org.frametrellis.project","version":1})")
            << int(ErrorCode::InvalidDocument);
        QTest::newRow("bad version")
            << QByteArray(R"({"format":"org.frametrellis.project","version":"one"})")
            << int(ErrorCode::InvalidDocument);
        QTest::newRow("newer version")
            << QByteArray(R"({"format":"org.frametrellis.project","version":99,"project":{}})")
            << int(ErrorCode::UnsupportedVersion);
    }

    void rejectsMalformedInput()
    {
        QFETCH(QByteArray, data);
        QFETCH(int, code);
        Result<Project> loaded = project_format::fromBytes(data);
        QVERIFY(!loaded);
        QCOMPARE(int(loaded.error().code), code);
        QVERIFY(!loaded.error().message.isEmpty());
    }

    void rejectsInvalidValues_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("negative start") << QStringLiteral("start") << QJsonValue(-5);
        QTest::newRow("fractional start") << QStringLiteral("start") << QJsonValue(1.5);
        QTest::newRow("string start")
            << QStringLiteral("start") << QJsonValue(QStringLiteral("10"));
        QTest::newRow("zero duration") << QStringLiteral("duration") << QJsonValue(0);
        QTest::newRow("beyond source") << QStringLiteral("duration") << QJsonValue(100000);
        QTest::newRow("huge gain") << QStringLiteral("gain") << QJsonValue(1e9);
        QTest::newRow("unknown media")
            << QStringLiteral("media") << QJsonValue(QStringLiteral("does-not-exist"));
        QTest::newRow("bad id") << QStringLiteral("id") << QJsonValue(QStringLiteral("../../etc"));
        QTest::newRow("overlap") << QStringLiteral("start") << QJsonValue(250);
    }

    void rejectsInvalidValues()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QJsonObject root = validDocument();
        QJsonObject body;
        projectBody(root, body);
        QJsonArray tracks = body[QLatin1String("tracks")].toArray();
        QJsonObject v1 = tracks[0].toObject();
        QJsonArray clips = v1[QLatin1String("clips")].toArray();
        QJsonObject clip = clips[0].toObject();
        clip[field] = value;
        clips[0] = clip;
        v1[QLatin1String("clips")] = clips;
        tracks[0] = v1;
        body[QLatin1String("tracks")] = tracks;
        root[QLatin1String("project")] = body;
        Result<Project> loaded = project_format::fromJson(root);
        QVERIFY(!loaded);
        QCOMPARE(loaded.error().code, ErrorCode::InvalidDocument);
        QVERIFY(!loaded.error().detail.isEmpty());
    }

    void transitionsRoundTrip()
    {
        const Project original = makeProject(QStringLiteral("/media"));
        QCOMPARE(original.timeline().tracks()[0].transitions.size(), std::size_t{1});
        const QJsonObject json = project_format::toJson(original);
        const QJsonArray tracks = json[QLatin1String("project")][QLatin1String("tracks")].toArray();
        const QJsonArray transitions = tracks[0][QLatin1String("transitions")].toArray();
        QCOMPARE(transitions.size(), 1);
        QCOMPARE(transitions[0][QLatin1String("from")].toString(), QStringLiteral("clip1"));
        QCOMPARE(transitions[0][QLatin1String("to")].toString(), QStringLiteral("clip4"));
        QCOMPARE(transitions[0][QLatin1String("duration")].toInt(), 20);
        // Tracks without transitions do not write the field at all.
        QVERIFY(!tracks[2].toObject().contains(QLatin1String("transitions")));
        Result<Project> loaded = project_format::fromJson(json);
        QVERIFY(loaded);
        QCOMPARE(loaded->timeline().tracks()[0].transitions,
                 original.timeline().tracks()[0].transitions);
    }

    void rejectsInvalidTransitions_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("too short") << QStringLiteral("duration") << QJsonValue(1);
        QTest::newRow("longer than the media allows")
            << QStringLiteral("duration") << QJsonValue(5000);
        QTest::newRow("unknown clip")
            << QStringLiteral("from") << QJsonValue(QStringLiteral("nothing"));
        QTest::newRow("clips not touching")
            << QStringLiteral("to") << QJsonValue(QStringLiteral("clip2"));
        QTest::newRow("bad id") << QStringLiteral("id") << QJsonValue(QStringLiteral("a/b"));
    }

    void rejectsInvalidTransitions()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QJsonObject root = validDocument();
        QJsonObject body;
        projectBody(root, body);
        QJsonArray tracks = body[QLatin1String("tracks")].toArray();
        QJsonObject v1 = tracks[0].toObject();
        QJsonArray transitions = v1[QLatin1String("transitions")].toArray();
        QJsonObject transition = transitions[0].toObject();
        transition[field] = value;
        transitions[0] = transition;
        v1[QLatin1String("transitions")] = transitions;
        tracks[0] = v1;
        body[QLatin1String("tracks")] = tracks;
        root[QLatin1String("project")] = body;
        Result<Project> loaded = project_format::fromJson(root);
        QVERIFY(!loaded);
        QCOMPARE(loaded.error().code, ErrorCode::InvalidDocument);
    }

    void rejectsInvalidSettings()
    {
        QJsonObject root = validDocument();
        QJsonObject body;
        projectBody(root, body);
        QJsonObject settings = body[QLatin1String("settings")].toObject();
        settings[QLatin1String("width")] = 1281; // odd width
        body[QLatin1String("settings")] = settings;
        root[QLatin1String("project")] = body;
        QVERIFY(!project_format::fromJson(root));
        settings[QLatin1String("width")] = 1280;
        settings[QLatin1String("frameRate")] = QStringLiteral("0/1");
        body[QLatin1String("settings")] = settings;
        root[QLatin1String("project")] = body;
        QVERIFY(!project_format::fromJson(root));
    }

    void rejectsDuplicateMediaIds()
    {
        QJsonObject root = validDocument();
        QJsonObject body;
        projectBody(root, body);
        QJsonArray media = body[QLatin1String("media")].toArray();
        media.append(media[0]);
        body[QLatin1String("media")] = media;
        root[QLatin1String("project")] = body;
        QVERIFY(!project_format::fromJson(root));
    }

    void ignoresUnknownFields()
    {
        QJsonObject root = validDocument();
        root[QLatin1String("futureExtension")] = QStringLiteral("ignored");
        QVERIFY(project_format::fromJson(root));
    }

    void loadReportsMissingFile()
    {
        Result<Project> loaded =
            project_format::loadFromFile(QStringLiteral("/nonexistent/dir/project.ftproj"));
        QVERIFY(!loaded);
        QCOMPARE(loaded.error().code, ErrorCode::NotFound);
    }

    void failedSaveKeepsPreviousFile()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("keep.ftproj"));
        const Project original = makeProject(dir.path());
        QVERIFY(project_format::saveToFile(original, file));
        // Saving into a directory that does not exist fails cleanly.
        const Status failed = project_format::saveToFile(
            original, dir.filePath(QStringLiteral("missing/sub/x.ftproj")));
        QVERIFY(!failed);
        QCOMPARE(failed.error().code, ErrorCode::Io);
        QVERIFY(project_format::loadFromFile(file));
    }

    void saveCannotOverwriteReferencedMedia()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mediaPath = dir.filePath(QStringLiteral("clip.mp4"));
        const QByteArray original = QByteArrayLiteral("original source media bytes");
        {
            QFile source(mediaPath);
            QVERIFY(source.open(QIODevice::WriteOnly));
            QCOMPARE(source.write(original), static_cast<qint64>(original.size()));
        }

        const Project project = makeProject(dir.path());
        const Status refused = project_format::saveToFile(project, mediaPath);
        QVERIFY(!refused);
        QCOMPARE(refused.error().code, ErrorCode::Conflict);

        // Alternate path spellings must not bypass the same-media check.
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("nested"))));
        const Status aliased =
            project_format::saveToFile(project, dir.filePath(QStringLiteral("nested/../clip.mp4")));
        QVERIFY(!aliased);
        QCOMPARE(aliased.error().code, ErrorCode::Conflict);

        QFile unchanged(mediaPath);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), original);

        // Legitimate project saves still work and remain readable.
        const QString projectPath = dir.filePath(QStringLiteral("edit.ftproj"));
        QVERIFY(project_format::saveToFile(project, projectPath));
        QVERIFY(project_format::loadFromFile(projectPath));
    }

    void rejectsOversizedDocuments()
    {
        QByteArray huge(int(project_format::kMaxDocumentBytes) + 16, ' ');
        Result<Project> loaded = project_format::fromBytes(huge);
        QVERIFY(!loaded);
        QCOMPARE(loaded.error().code, ErrorCode::InvalidDocument);
    }

    void assetLengthInFrames()
    {
        MediaAsset video =
            makeAsset(QStringLiteral("x"), QStringLiteral("/a.mp4"), MediaKind::Video, 2'000'000);
        QCOMPARE(*video.lengthInFrames(*FrameRate::create(25, 1)), FrameCount{50});
        QCOMPARE(*video.lengthInFrames(*FrameRate::create(30000, 1001)), FrameCount{59});
        MediaAsset still =
            makeAsset(QStringLiteral("y"), QStringLiteral("/a.png"), MediaKind::Image, 0);
        QVERIFY(!still.lengthInFrames(*FrameRate::create(25, 1)).has_value());
        MediaAsset tiny =
            makeAsset(QStringLiteral("z"), QStringLiteral("/a.wav"), MediaKind::Audio, 1);
        QCOMPARE(*tiny.lengthInFrames(*FrameRate::create(25, 1)), FrameCount{1});
    }

    void projectMediaRules()
    {
        Project p = makeProject(QStringLiteral("/media"));
        QCOMPARE(p.removeAsset(QStringLiteral("m1")).error().code, ErrorCode::Conflict);
        QVERIFY(p.assetByPath(QStringLiteral("/media/clip.mp4")));
        QVERIFY(p.assetByPath(QStringLiteral("/media/../media/clip.mp4")));
        QVERIFY(!p.addAsset(
            makeAsset(QStringLiteral("m1"), QStringLiteral("/other.mp4"), MediaKind::Video, 1)));
        QCOMPARE(p.nextTrackName(TrackKind::Video), QStringLiteral("V3"));
        QCOMPARE(p.nextTrackName(TrackKind::Audio), QStringLiteral("A3"));
        ProjectSettings changed = p.settings();
        changed.frameRate = *FrameRate::create(25, 1);
        QCOMPARE(p.setSettings(changed).error().code, ErrorCode::Conflict);
        changed = p.settings();
        changed.width = 1920;
        changed.height = 1080;
        QVERIFY(p.setSettings(changed));
    }

    void mediaAuditReportsCurrentStatusAndUsageWithoutChangingProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject(dir.path());

        QFile video(dir.filePath(QStringLiteral("clip.mp4")));
        QVERIFY(video.open(QIODevice::WriteOnly));
        QCOMPARE(video.write("ABCD", 4), qint64{4});
        video.close();

        MediaAsset current = *project.asset(QStringLiteral("m1"));
        current.name = QStringLiteral("फ़ुटेज video");
        current.info.fileSize = 4;
        current.info.lastModified = QFileInfo(video.fileName()).lastModified();
        QVERIFY(project.replaceAsset(current));

        QFile image(dir.filePath(QStringLiteral("logo.png")));
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write("different", 9), qint64{9});
        image.close();
        QVERIFY(project.addAsset(makeAsset(QStringLiteral("unused"),
                                           dir.filePath(QStringLiteral("missing-unused.mp4")),
                                           MediaKind::Video, 1)));

        const Project before = project;
        const QJsonObject report = media_audit::buildReport(project);
        QCOMPARE(report.value(QLatin1String("schemaVersion")).toInt(), 1);
        QCOMPARE(report.value(QLatin1String("projectName")).toString(), project.name());
        const QJsonObject summary = report.value(QLatin1String("summary")).toObject();
        QCOMPARE(summary.value(QLatin1String("total")).toInt(), 4);
        QCOMPARE(summary.value(QLatin1String("online")).toInt(), 1);
        QCOMPARE(summary.value(QLatin1String("missing")).toInt(), 2);
        QCOMPARE(summary.value(QLatin1String("changed")).toInt(), 1);
        QCOMPARE(summary.value(QLatin1String("unused")).toInt(), 1);
        QCOMPARE(summary.value(QLatin1String("clipUses")).toInt(), 4);

        const QJsonArray assets = report.value(QLatin1String("assets")).toArray();
        QCOMPARE(assets.size(), 4);
        QCOMPARE(assets.at(0).toObject().value(QLatin1String("status")).toString(),
                 QStringLiteral("online"));
        QCOMPARE(assets.at(0).toObject().value(QLatin1String("clipUses")).toInt(), 2);
        QCOMPARE(assets.at(1).toObject().value(QLatin1String("status")).toString(),
                 QStringLiteral("missing"));
        QCOMPARE(assets.at(2).toObject().value(QLatin1String("status")).toString(),
                 QStringLiteral("changed"));
        QCOMPARE(assets.at(3).toObject().value(QLatin1String("clipUses")).toInt(), 0);

        const QString dest = dir.filePath(QStringLiteral("फ़ुटेज-audit.json"));
        QVERIFY(media_audit::saveReport(project, dest));
        QFile exported(dest);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const QJsonDocument decoded = QJsonDocument::fromJson(exported.readAll());
        QVERIFY(decoded.isObject());
        QCOMPARE(decoded.object().value(QLatin1String("summary")).toObject(), summary);
        QCOMPARE(decoded.object().value(QLatin1String("assets")).toArray(), assets);
        QCOMPARE(project, before);
    }

    void mediaAuditCannotReplaceSourceOrProjectFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject(dir.path());
        const QString mediaPath = dir.filePath(QStringLiteral("source.json"));
        QFile source(mediaPath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        const QByteArray original = QByteArrayLiteral("original media bytes");
        QCOMPARE(source.write(original), static_cast<qint64>(original.size()));
        source.close();
        QVERIFY(project.addAsset(
            makeAsset(QStringLiteral("json-source"), mediaPath, MediaKind::Image, 0)));

        const Status refusedSource = media_audit::saveReport(project, mediaPath);
        QVERIFY(!refusedSource);
        QCOMPARE(refusedSource.error().code, ErrorCode::Conflict);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
        source.close();

        const QString projectPath = dir.filePath(QStringLiteral("project.json"));
        QFile existing(projectPath);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write(original), static_cast<qint64>(original.size()));
        existing.close();
        const Status refusedProject = media_audit::saveReport(project, projectPath, projectPath);
        QVERIFY(!refusedProject);
        QCOMPARE(refusedProject.error().code, ErrorCode::Conflict);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), original);
        QVERIFY(!media_audit::saveReport(project, dir.filePath(QStringLiteral("no.json.txt"))));
    }

    void presetsAreValid()
    {
        for (const ProjectPreset& preset : projectPresets()) {
            QVERIFY2(preset.settings.validate(), qPrintable(preset.name));
        }
        ProjectSettings s;
        s.sampleRate = 12345;
        QVERIFY(!s.validate());
        s = {};
        s.audioChannels = 6;
        QVERIFY(!s.validate());
        s = {};
        s.width = 9000;
        QVERIFY(!s.validate());
    }
};

QTEST_GUILESS_MAIN(ProjectFormatTest)
#include "tst_project_format.moc"
