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
    Project project = Project::createNew(QStringLiteral("Opacity"), {});
    MediaAsset asset;
    asset.id = QStringLiteral("source");
    asset.name = QStringLiteral("Picture");
    asset.filePath = QDir::temp().absoluteFilePath(QStringLiteral("frametrellis-opacity.mp4"));
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

QJsonObject withPoints(const QJsonObject& input, const QJsonValue& points)
{
    QJsonObject result = input;
    QJsonObject body = result[QStringLiteral("project")].toObject();
    QJsonArray tracks = body[QStringLiteral("tracks")].toArray();
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        QJsonObject track = tracks[i].toObject();
        QJsonArray clips = track[QStringLiteral("clips")].toArray();
        if (clips.isEmpty()) {
            continue;
        }
        QJsonObject clip = clips[0].toObject();
        clip[QStringLiteral("opacityKeyframes")] = points;
        clips[0] = clip;
        track[QStringLiteral("clips")] = clips;
        tracks[i] = track;
    }
    body[QStringLiteral("tracks")] = tracks;
    result[QStringLiteral("project")] = body;
    return result;
}

QJsonObject point(double frame, const QJsonValue& opacity)
{
    return {{QStringLiteral("sourceFrame"), frame}, {QStringLiteral("opacity"), opacity}};
}

} // namespace

class OpacityKeyframeTest : public QObject
{
    Q_OBJECT

private slots:
    void interpolationAndHeldEndpoints()
    {
        QCOMPARE(opacityKeyframeValue({}, 50), 1.0);
        const std::vector<OpacityKeyframe> points{{30, 0}, {80, 1}, {130, 0.5}};
        QVERIFY(validateOpacityKeyframes(points, kMaxTimelineFrames));
        QCOMPARE(opacityKeyframeValue(points, 0), 0.0);
        QCOMPARE(opacityKeyframeValue(points, 55), 0.5);
        QCOMPARE(opacityKeyframeValue(points, 80), 1.0);
        QCOMPARE(opacityKeyframeValue(points, 105), 0.75);
        QCOMPARE(opacityKeyframeValue(points, kMaxTimelineFrames), 0.5);
    }

    void sortedUpsertAndAtomicInvalidInput()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 80, 0.5, sources));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0, sources));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 129, 1, sources));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 80, 0.25, sources));
        const auto keys = timeline.clip(QStringLiteral("clip"))->opacityKeyframes;
        QCOMPARE(keys.size(), size_t{3});
        QCOMPARE(keys[0].sourceFrame, FrameCount{30});
        QCOMPARE(keys[1].opacity, 0.25);
        QVERIFY(project.validate());
        const Project before = project;
        for (double invalid : {-0.01, 1.01, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!timeline.setOpacityKeyframe(QStringLiteral("clip"), 80, invalid, sources));
            QCOMPARE(project, before);
        }
        QVERIFY(!timeline.setOpacityKeyframe(QStringLiteral("clip"), 29, 0, sources));
        QVERIFY(!timeline.setOpacityKeyframe(QStringLiteral("clip"), 130, 0, sources));
        QVERIFY(!timeline.setOpacityKeyframe(QStringLiteral("missing"), 30, 0, sources));
        QVERIFY(!timeline.removeOpacityKeyframe(QStringLiteral("clip"), 50));
        QCOMPARE(project, before);
        QVERIFY(timeline.removeOpacityKeyframe(QStringLiteral("clip"), 80));
        QVERIFY(timeline.clearOpacityKeyframes(QStringLiteral("clip")));
        QVERIFY(timeline.clip(QStringLiteral("clip"))->opacityKeyframes.empty());
    }

    void limitsAndMalformedModelCurves()
    {
        Project project = pictureProject(MediaKind::Image, 2000);
        auto& timeline = project.timeline();
        for (int i = 0; i < kMaxOpacityKeyframes; ++i) {
            QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30 + i, 0.5,
                                                project.sourceLookup()));
        }
        const Project full = project;
        QVERIFY(
            !timeline.setOpacityKeyframe(QStringLiteral("clip"), 1500, 1, project.sourceLookup()));
        QCOMPARE(project, full);
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0, project.sourceLookup()));
        QVERIFY(!validateOpacityKeyframes({{0, 0}, {0, 1}}, kMaxTimelineFrames));
        QVERIFY(!validateOpacityKeyframes({{5, 1}, {0, 0}}, kMaxTimelineFrames));
        QVERIFY(!validateOpacityKeyframes({{-1, 0}}, kMaxTimelineFrames));
        QVERIFY(!validateOpacityKeyframes({{kMaxTimelineFrames + 1, 1}}, kMaxTimelineFrames));
        auto tracks = timeline.tracks();
        tracks[0].clips[0].opacityKeyframes = {{0, 0}, {0, 1}};
        timeline.restoreTracks(tracks);
        QVERIFY(!project.validate());
    }

    void undoRedoAndMergeBoundaries()
    {
        ProjectDocument document(pictureProject());
        const Project original = document.project();
        QVERIFY(document.execute(edits::setOpacityKeyframe(QStringLiteral("clip"), 30, 1)));
        QVERIFY(document.execute(edits::setOpacityKeyframe(QStringLiteral("clip"), 30, 0.5)));
        QVERIFY(document.execute(edits::setOpacityKeyframe(QStringLiteral("clip"), 30, 0)));
        const Project edited = document.project();
        QVERIFY(document.undo());
        QCOMPARE(document.project(), original);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        QVERIFY(document.redo());
        QCOMPARE(document.project(), edited);
        document.breakMerge();
        QVERIFY(document.execute(edits::setOpacityKeyframe(QStringLiteral("clip"), 80, 1)));
        const Project two = document.project();
        QVERIFY(document.execute(edits::removeOpacityKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), two);
        QVERIFY(document.execute(edits::clearOpacityKeyframes(QStringLiteral("clip"))));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), two);
    }

    void locksAndMediaCompatibility()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        QVERIFY(
            timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0.5, project.sourceLookup()));
        const Id videoTrack = timeline.trackOfClip(QStringLiteral("clip"))->id;
        QVERIFY(timeline.setTrackLocked(videoTrack, true));
        ProjectDocument document(project);
        QVERIFY(!document.execute(edits::setOpacityKeyframe(QStringLiteral("clip"), 80, 1)));
        QVERIFY(!document.execute(edits::removeOpacityKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(!document.execute(edits::clearOpacityKeyframes(QStringLiteral("clip"))));
        QVERIFY(!document.execute(edits::resetClipPicture(QStringLiteral("clip"))));
        QCOMPARE(document.project(), project);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        Project audio = pictureProject(MediaKind::Audio);
        QVERIFY(!audio.timeline().setOpacityKeyframe(QStringLiteral("clip"), 30, 0.5,
                                                     audio.sourceLookup()));
        QVERIFY(timeline.setTrackLocked(videoTrack, false));
        const auto keys = timeline.clip(QStringLiteral("clip"))->opacityKeyframes;
        const Id audioTrack = timeline.tracks()[2].id;
        QVERIFY(timeline.moveClip(QStringLiteral("clip"), audioTrack, 10, project.sourceLookup()));
        QVERIFY(project.validate());
        QVERIFY(
            !timeline.setOpacityKeyframe(QStringLiteral("clip"), 80, 1, project.sourceLookup()));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->opacityKeyframes, keys);
        QVERIFY(timeline.moveClip(QStringLiteral("clip"), videoTrack, 10, project.sourceLookup()));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->opacityKeyframes, keys);
    }

    void sourceEditsAndAttributePastePreserveCurves()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0, sources));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 80, 1, sources));
        const auto keys = timeline.clip(QStringLiteral("clip"))->opacityKeyframes;
        QVERIFY(timeline.trimClipStart(QStringLiteral("clip"), 20, sources));
        QVERIFY(timeline.splitClip(QStringLiteral("clip"), 60, QStringLiteral("right")));
        QVERIFY(timeline.rollCut(QStringLiteral("clip"), 5, sources));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->sourceIn, FrameCount{85});
        QVERIFY(timeline.slipClip(QStringLiteral("right"), 90, sources));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->opacityKeyframes, keys);
        ProjectDocument document(project);
        QVERIFY(document.execute(
            edits::duplicateClip(QStringLiteral("right"), QStringLiteral("copy"))));
        QCOMPARE(document.project().timeline().clip(QStringLiteral("copy"))->opacityKeyframes,
                 keys);
        ClipProperties properties;
        properties.video.opacity = 0.5;
        QVERIFY(
            document.execute(edits::pasteClipProperties({QStringLiteral("right")}, properties)));
        QCOMPARE(document.project().timeline().clip(QStringLiteral("right"))->opacityKeyframes,
                 keys);
        QVERIFY(document.project().validate());
    }

    void pictureResetClearsOpacityAndRetainsAudioAndFades()
    {
        Project project = pictureProject();
        auto& timeline = project.timeline();
        ClipProperties properties;
        properties.video.opacity = 0.5;
        properties.video.brightness = 0.25;
        properties.gain = 0.5;
        properties.fadeIn = 10;
        properties.fadeOut = 20;
        QVERIFY(timeline.setClipProperties(QStringLiteral("clip"), properties));
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30, -12, project.sourceLookup()));
        QVERIFY(timeline.setOpacityKeyframe(QStringLiteral("clip"), 30, 0, project.sourceLookup()));
        ProjectDocument document(project);
        QVERIFY(document.execute(edits::resetClipPicture(QStringLiteral("clip"))));
        const Clip* clip = document.project().timeline().clip(QStringLiteral("clip"));
        QVERIFY(clip->properties.video.isNeutral());
        QVERIFY(clip->opacityKeyframes.empty());
        QCOMPARE(clip->audioKeyframes, timeline.clip(QStringLiteral("clip"))->audioKeyframes);
        QCOMPARE(clip->properties.gain, 0.5);
        QCOMPARE(clip->properties.fadeIn, FrameCount{10});
        QCOMPARE(clip->properties.fadeOut, FrameCount{20});
        QVERIFY(document.undo());
        QCOMPARE(document.project(), project);
        QVERIFY(document.redo());
        QVERIFY(document.undo());
        Clip second = *timeline.clip(QStringLiteral("clip"));
        second.id = QStringLiteral("second");
        const Id otherTrack = timeline.tracks()[1].id;
        QVERIFY(timeline.insertClip(otherTrack, second, project.sourceLookup()));
        QVERIFY(timeline.setTrackLocked(otherTrack, true));
        ProjectDocument locked(project);
        QVERIFY(!locked.execute(edits::resetClipPictures({QStringLiteral("clip"), second.id})));
        QCOMPARE(locked.project(), project);
        QVERIFY(!locked.canUndo());
    }

    void versionedPersistenceAndMinimumFormat()
    {
        Project project = pictureProject();
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 2);
        for (int version : {1, 2, 3}) {
            QJsonObject legacy = project_format::toJson(project);
            legacy[QStringLiteral("version")] = version;
            const auto loaded = project_format::fromJson(legacy);
            QVERIFY(loaded);
            QVERIFY(loaded->timeline().clip(QStringLiteral("clip"))->opacityKeyframes.empty());
        }
        QVERIFY(project.timeline().setAudioKeyframe(QStringLiteral("clip"), 30, -6,
                                                    project.sourceLookup()));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 3);
        QVERIFY(project.timeline().setOpacityKeyframe(QStringLiteral("clip"), 30, 0,
                                                      project.sourceLookup()));
        QVERIFY(project.timeline().setOpacityKeyframe(QStringLiteral("clip"), 80, 0.375,
                                                      project.sourceLookup()));
        const QJsonObject saved = project_format::toJson(project);
        QCOMPARE(saved[QStringLiteral("version")].toInt(), 4);
        const auto loaded = project_format::fromBytes(project_format::toBytes(project));
        QVERIFY(loaded);
        QCOMPARE(loaded->timeline(), project.timeline());
        for (int version : {1, 2, 3}) {
            QJsonObject wrong = saved;
            wrong[QStringLiteral("version")] = version;
            QVERIFY(!project_format::fromJson(wrong));
        }
        QVERIFY(project.timeline().clearOpacityKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 3);
        QVERIFY(project.timeline().clearAudioKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 2);
    }

    void rejectsMalformedDocuments()
    {
        QJsonObject document = project_format::toJson(pictureProject());
        document[QStringLiteral("version")] = 4;
        const auto rejects = [&](const QJsonValue& points) {
            const auto result = project_format::fromJson(withPoints(document, points));
            return !result && result.error().code == ErrorCode::InvalidDocument;
        };
        QVERIFY(rejects(QStringLiteral("not an array")));
        QVERIFY(rejects(QJsonArray{QStringLiteral("not an object")}));
        QVERIFY(rejects(QJsonArray{point(30.5, 0)}));
        QVERIFY(rejects(QJsonArray{point(-1, 0)}));
        QVERIFY(rejects(QJsonArray{point(kMaxTimelineFrames + 1, 0)}));
        QVERIFY(rejects(QJsonArray{point(30, -0.1)}));
        QVERIFY(rejects(QJsonArray{point(30, 1.1)}));
        QVERIFY(rejects(QJsonArray{point(30, QStringLiteral("opaque"))}));
        QVERIFY(rejects(QJsonArray{point(30, QJsonValue::Null)}));
        QVERIFY(rejects(QJsonArray{QJsonObject{{QStringLiteral("sourceFrame"), 30}}}));
        QVERIFY(rejects(QJsonArray{QJsonObject{{QStringLiteral("opacity"), 1}}}));
        QVERIFY(rejects(QJsonArray{point(30, 0), point(30, 1)}));
        QVERIFY(rejects(QJsonArray{point(80, 0), point(30, 1)}));
        QJsonArray oversized;
        for (int i = 0; i <= kMaxOpacityKeyframes; ++i) {
            oversized.append(point(i, 1));
        }
        QVERIFY(rejects(oversized));
        QJsonObject audio = project_format::toJson(pictureProject(MediaKind::Audio));
        audio[QStringLiteral("version")] = 4;
        QVERIFY(!project_format::fromJson(withPoints(audio, QJsonArray{point(30, 0.5)})));
    }
};

QTEST_GUILESS_MAIN(OpacityKeyframeTest)
#include "tst_opacity_keyframes.moc"
