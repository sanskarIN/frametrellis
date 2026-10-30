// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/timeline_edits.h"
#include "project/project_serializer.h"

#include <QDir>
#include <QJsonArray>
#include <QTest>

#include <limits>

using namespace ft;

namespace {

Project pictureProject(MediaKind kind = MediaKind::Video, FrameCount duration = 100)
{
    Project project = Project::createNew(QStringLiteral("Transform"), {});
    MediaAsset asset;
    asset.id = QStringLiteral("source");
    asset.name = QStringLiteral("Picture");
    asset.filePath = QDir::temp().absoluteFilePath(QStringLiteral("frametrellis-transform.mp4"));
    asset.info.kind = kind;
    asset.info.hasVideo = kind != MediaKind::Audio;
    asset.info.hasAudio = kind != MediaKind::Image;
    asset.info.durationUs = kind == MediaKind::Image ? 0 : 200'000'000;
    asset.info.width = asset.info.hasVideo ? 320 : 0;
    asset.info.height = asset.info.hasVideo ? 180 : 0;
    asset.info.audioSampleRate = asset.info.hasAudio ? 48000 : 0;
    asset.info.audioChannels = asset.info.hasAudio ? 2 : 0;
    (void)project.addAsset(asset);
    Clip clip;
    clip.id = QStringLiteral("clip");
    clip.assetId = asset.id;
    clip.start = 10;
    clip.sourceIn = 30;
    clip.duration = duration;
    for (const Track& track : project.timeline().tracks()) {
        if (track.kind == (kind == MediaKind::Audio ? TrackKind::Audio : TrackKind::Video)) {
            (void)project.timeline().insertClip(track.id, clip, project.sourceLookup());
            break;
        }
    }
    return project;
}

QJsonObject withPoints(QJsonObject document, const QJsonValue& points)
{
    QJsonObject body = document[QStringLiteral("project")].toObject();
    QJsonArray tracks = body[QStringLiteral("tracks")].toArray();
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        QJsonObject track = tracks[i].toObject();
        QJsonArray clips = track[QStringLiteral("clips")].toArray();
        if (clips.isEmpty()) {
            continue;
        }
        QJsonObject clip = clips[0].toObject();
        clip[QStringLiteral("transformKeyframes")] = points;
        clips[0] = clip;
        track[QStringLiteral("clips")] = clips;
        tracks[i] = track;
    }
    body[QStringLiteral("tracks")] = tracks;
    document[QStringLiteral("project")] = body;
    return document;
}

QJsonObject point(double frame)
{
    return {{QStringLiteral("sourceFrame"), frame},
            {QStringLiteral("x"), 0.25},
            {QStringLiteral("y"), -0.125},
            {QStringLiteral("scale"), 0.5},
            {QStringLiteral("rotation"), 90}};
}

} // namespace

class TransformKeyframeTest : public QObject
{
    Q_OBJECT

private slots:
    void interpolationUsesCanvasCoordinatesAndUnwrappedDegrees()
    {
        QCOMPARE(transformKeyframeValue({}, 50), VideoTransform{});
        const VideoTransform first{-0.25, 0.5, 0.5, 0};
        const VideoTransform last{0.25, -0.5, 1.5, 720};
        const std::vector<TransformKeyframe> points{{30, first}, {80, last}};
        QVERIFY(validateTransformKeyframes(points, kMaxTimelineFrames));
        QCOMPARE(transformKeyframeValue(points, 0), first);
        QCOMPARE(transformKeyframeValue(points, 55), (VideoTransform{0, 0, 1, 360}));
        QCOMPARE(transformKeyframeValue(points, 80), last);
        QCOMPARE(transformKeyframeValue(points, kMaxTimelineFrames), last);
        QCOMPARE(transformKeyframeValue({{55, first}}, 0), first);
        QCOMPARE(transformKeyframeValue({{55, first}}, 1000), first);
    }

    void invalidEditsAreAtomicAndCapacityAllowsReplacement()
    {
        Project project = pictureProject(MediaKind::Image, 2000);
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(
            timeline.setTransformKeyframe(QStringLiteral("clip"), 80, {0.25, 0, 0.5, 90}, sources));
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30, {}, sources));
        const Project before = project;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (const VideoTransform& invalid : std::vector<VideoTransform>{{4.01, 0, 1, 0},
                                                                         {0, -4.01, 1, 0},
                                                                         {0, 0, 0, 0},
                                                                         {0, 0, 10.01, 0},
                                                                         {0, 0, 1, 3601},
                                                                         {nan, 0, 1, 0},
                                                                         {0, inf, 1, 0},
                                                                         {0, 0, nan, 0},
                                                                         {0, 0, 1, inf}}) {
            QVERIFY(!timeline.setTransformKeyframe(QStringLiteral("clip"), 80, invalid, sources));
            QCOMPARE(project, before);
        }
        for (FrameCount frame :
             {FrameCount{-1}, FrameCount{29}, FrameCount{2030}, kMaxTimelineFrames + 1}) {
            QVERIFY(!timeline.setTransformKeyframe(QStringLiteral("clip"), frame, {}, sources));
            QCOMPARE(project, before);
        }
        QVERIFY(!timeline.removeTransformKeyframe(QStringLiteral("clip"), 31));
        QCOMPARE(project, before);
        QVERIFY(validateVideoTransform({-4, 4, 0.01, -3600}));
        QVERIFY(validateVideoTransform({4, -4, 10, 3600}));
        QVERIFY(!validateTransformKeyframes({{5, {}}, {5, {}}}, kMaxTimelineFrames));
        QVERIFY(!validateTransformKeyframes({{6, {}}, {5, {}}}, kMaxTimelineFrames));
        QVERIFY(!validateTransformKeyframes({{-1, {}}}, kMaxTimelineFrames));
        QVERIFY(!validateTransformKeyframes({{kMaxTimelineFrames + 1, {}}}, kMaxTimelineFrames));
        QVERIFY(timeline.clearTransformKeyframes(QStringLiteral("clip")));
        for (int i = 0; i < kMaxTransformKeyframes; ++i) {
            QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30 + i, {}, sources));
        }
        const Project full = project;
        QVERIFY(!timeline.setTransformKeyframe(QStringLiteral("clip"), 1500, {}, sources));
        QCOMPARE(project, full);
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30, {0, 0, 2, 45}, sources));
        QVERIFY(project.validate());
    }

    void commandsMergeDragsAndRestoreExactState()
    {
        ProjectDocument document(pictureProject());
        const Project original = document.project();
        QVERIFY(document.execute(edits::setTransformKeyframe(QStringLiteral("clip"), 30, {})));
        QVERIFY(document.execute(
            edits::setTransformKeyframe(QStringLiteral("clip"), 30, {0.25, 0, 0.5, 45})));
        QVERIFY(document.execute(
            edits::setTransformKeyframe(QStringLiteral("clip"), 30, {-0.5, 0.5, 2, 90})));
        const Project edited = document.project();
        QVERIFY(document.undo());
        QCOMPARE(document.project(), original);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        QVERIFY(document.redo());
        QCOMPARE(document.project(), edited);
        document.breakMerge();
        QVERIFY(document.execute(edits::setTransformKeyframe(QStringLiteral("clip"), 80, {})));
        const Project two = document.project();
        QVERIFY(document.execute(edits::removeTransformKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), two);
        QVERIFY(document.execute(edits::clearTransformKeyframes(QStringLiteral("clip"))));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), two);
    }

    void locksAndDormantPictureOnAudioTracks()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30, {0, 0, 0.5, 90},
                                              project.sourceLookup()));
        const Id videoTrack = timeline.trackOfClip(QStringLiteral("clip"))->id;
        QVERIFY(timeline.setTrackLocked(videoTrack, true));
        ProjectDocument document(project);
        QVERIFY(!document.execute(edits::setTransformKeyframe(QStringLiteral("clip"), 80, {})));
        QVERIFY(!document.execute(edits::removeTransformKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(!document.execute(edits::clearTransformKeyframes(QStringLiteral("clip"))));
        QVERIFY(!document.execute(edits::resetClipPicture(QStringLiteral("clip"))));
        QCOMPARE(document.project(), project);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        Project audio = pictureProject(MediaKind::Audio);
        QVERIFY(!audio.timeline().setTransformKeyframe(QStringLiteral("clip"), 30, {},
                                                       audio.sourceLookup()));
        QVERIFY(timeline.setTrackLocked(videoTrack, false));
        const auto keys = timeline.clip(QStringLiteral("clip"))->transformKeyframes;
        QVERIFY(timeline.moveClip(QStringLiteral("clip"), timeline.tracks()[2].id, 10,
                                  project.sourceLookup()));
        QVERIFY(project.validate());
        QVERIFY(
            !timeline.setTransformKeyframe(QStringLiteral("clip"), 80, {}, project.sourceLookup()));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->transformKeyframes, keys);
        QVERIFY(timeline.moveClip(QStringLiteral("clip"), videoTrack, 10, project.sourceLookup()));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->transformKeyframes, keys);
        auto tracks = audio.timeline().tracks();
        tracks[2].clips[0].transformKeyframes = keys;
        audio.timeline().restoreTracks(tracks);
        QVERIFY(!audio.validate());
    }

    void editsPreserveSourceAnchoringAndPictureResetIsAtomic()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30, {-0.25, 0, 0.5, -90},
                                              sources));
        QVERIFY(
            timeline.setTransformKeyframe(QStringLiteral("clip"), 80, {0.25, 0, 1, 90}, sources));
        const auto keys = timeline.clip(QStringLiteral("clip"))->transformKeyframes;
        QVERIFY(timeline.trimClipStart(QStringLiteral("clip"), 20, sources));
        QVERIFY(timeline.splitClip(QStringLiteral("clip"), 60, QStringLiteral("right")));
        QVERIFY(timeline.rollCut(QStringLiteral("clip"), 5, sources));
        QVERIFY(timeline.slipClip(QStringLiteral("right"), 90, sources));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->transformKeyframes, keys);
        ProjectDocument document(project);
        QVERIFY(document.execute(
            edits::duplicateClip(QStringLiteral("right"), QStringLiteral("copy"))));
        QCOMPARE(document.project().timeline().clip(QStringLiteral("copy"))->transformKeyframes,
                 keys);
        ClipProperties props;
        props.gain = 0.25;
        props.fadeIn = 2;
        props.video.opacity = 0.5;
        QVERIFY(document.execute(edits::pasteClipProperties({QStringLiteral("right")}, props)));
        QVERIFY(document.execute(edits::setAudioKeyframe(QStringLiteral("right"), 90, -12)));
        QVERIFY(document.execute(edits::setOpacityKeyframe(QStringLiteral("right"), 90, 0.5)));
        const Project beforeReset = document.project();
        QVERIFY(!document.execute(
            edits::resetClipPictures({QStringLiteral("right"), QStringLiteral("missing")})));
        QCOMPARE(document.project(), beforeReset);
        QVERIFY(document.execute(
            edits::resetClipPictures({QStringLiteral("right"), QStringLiteral("copy")})));
        const Clip* clip = document.project().timeline().clip(QStringLiteral("right"));
        QVERIFY(clip->transformKeyframes.empty());
        QVERIFY(clip->opacityKeyframes.empty());
        QVERIFY(clip->properties.video.isNeutral());
        QCOMPARE(clip->audioKeyframes, beforeReset.timeline().clip(clip->id)->audioKeyframes);
        QCOMPARE(clip->properties.gain, 0.25);
        QCOMPARE(clip->properties.fadeIn, FrameCount{2});
        QVERIFY(document.undo());
        QCOMPARE(document.project(), beforeReset);
        QVERIFY(document.project().validate());
    }
    void formatFiveRoundtripAndMinimumRequiredVersions()
    {
        Project project = pictureProject();
        QJsonObject legacy = project_format::toJson(project);
        for (int version : {1, 2, 3, 4, 5}) {
            legacy[QStringLiteral("version")] = version;
            QVERIFY(project_format::fromJson(legacy));
        }
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30, -6, sources));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0.5, sources));
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 30, {-0.25, 0.125, 0.5, -90},
                                              sources));
        QVERIFY(timeline.setTransformKeyframe(QStringLiteral("clip"), 80, {0.25, -0.125, 2, 720},
                                              sources));
        QJsonObject saved = project_format::toJson(project);
        QCOMPARE(saved[QStringLiteral("version")].toInt(), 5);
        const auto loaded = project_format::fromBytes(project_format::toBytes(project));
        QVERIFY(loaded);
        QCOMPARE(loaded->timeline(), timeline);
        for (int version : {1, 2, 3, 4}) {
            saved[QStringLiteral("version")] = version;
            QVERIFY(!project_format::fromJson(saved));
        }
        QVERIFY(timeline.clearTransformKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 4);
        QVERIFY(timeline.clearOpacityKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 3);
        QVERIFY(timeline.clearAudioKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 2);
    }

    void malformedTransformDocumentsAreRefused()
    {
        QJsonObject document = project_format::toJson(pictureProject());
        document[QStringLiteral("version")] = 5;
        const auto rejects = [&](const QJsonValue& points) {
            const auto result = project_format::fromJson(withPoints(document, points));
            return !result && result.error().code == ErrorCode::InvalidDocument;
        };
        QVERIFY(rejects(QStringLiteral("not an array")));
        QVERIFY(rejects(QJsonArray{QJsonValue::Null}));
        for (double frame : {-1.0, 30.5, static_cast<double>(kMaxTimelineFrames + 1)}) {
            QVERIFY(rejects(QJsonArray{point(frame)}));
        }
        for (const QString& field :
             {QStringLiteral("sourceFrame"), QStringLiteral("x"), QStringLiteral("y"),
              QStringLiteral("scale"), QStringLiteral("rotation")}) {
            QJsonObject missing = point(30);
            missing.remove(field);
            QVERIFY(rejects(QJsonArray{missing}));
            for (const QJsonValue& value : {QJsonValue(QJsonValue::Null),
                                            QJsonValue(QStringLiteral("zero")), QJsonValue(true)}) {
                QJsonObject wrong = point(30);
                wrong[field] = value;
                QVERIFY(rejects(QJsonArray{wrong}));
            }
        }
        for (const auto& entry :
             std::vector<std::pair<QString, double>>{{QStringLiteral("x"), 4.01},
                                                     {QStringLiteral("y"), -4.01},
                                                     {QStringLiteral("scale"), 0},
                                                     {QStringLiteral("scale"), 10.01},
                                                     {QStringLiteral("rotation"), -3601}}) {
            QJsonObject wrong = point(30);
            wrong[entry.first] = entry.second;
            QVERIFY(rejects(QJsonArray{wrong}));
        }
        QVERIFY(rejects(QJsonArray{point(30), point(30)}));
        QVERIFY(rejects(QJsonArray{point(80), point(30)}));
        QJsonArray oversized;
        for (int i = 0; i <= kMaxTransformKeyframes; ++i) {
            oversized.append(point(i));
        }
        QVERIFY(rejects(oversized));
        QJsonObject audio = project_format::toJson(pictureProject(MediaKind::Audio));
        audio[QStringLiteral("version")] = 5;
        QVERIFY(!project_format::fromJson(withPoints(audio, QJsonArray{point(30)})));
        document[QStringLiteral("version")] = project_format::kCurrentVersion + 1;
        QVERIFY(!project_format::fromJson(document));
    }
};

QTEST_APPLESS_MAIN(TransformKeyframeTest)
#include "tst_transform_keyframes.moc"
