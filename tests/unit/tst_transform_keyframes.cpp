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
};

QTEST_APPLESS_MAIN(TransformKeyframeTest)
#include "tst_transform_keyframes.moc"
