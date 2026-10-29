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

Project audioProject(FrameCount duration = 100)
{
    Project project = Project::createNew(QStringLiteral("Automation"), {});
    MediaAsset asset;
    asset.id = QStringLiteral("music");
    asset.name = QStringLiteral("Music");
    asset.filePath = QDir::temp().absoluteFilePath(QStringLiteral("frametrellis-keyframes.wav"));
    asset.info.kind = MediaKind::Audio;
    asset.info.hasAudio = true;
    asset.info.durationUs = 200'000'000;
    asset.info.audioSampleRate = 48000;
    asset.info.audioChannels = 2;
    (void)project.addAsset(asset);
    Clip clip;
    clip.id = QStringLiteral("clip");
    clip.assetId = asset.id;
    clip.start = 10;
    clip.sourceIn = 30;
    clip.duration = duration;
    for (const Track& track : project.timeline().tracks()) {
        if (track.kind == TrackKind::Audio) {
            (void)project.timeline().insertClip(track.id, clip, project.sourceLookup());
            break;
        }
    }
    return project;
}

QJsonObject withKeyframes(const QJsonObject& document, const QJsonValue& points)
{
    QJsonObject result = document;
    QJsonObject body = result[QStringLiteral("project")].toObject();
    QJsonArray tracks = body[QStringLiteral("tracks")].toArray();
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        QJsonObject track = tracks[i].toObject();
        QJsonArray clips = track[QStringLiteral("clips")].toArray();
        if (clips.isEmpty()) {
            continue;
        }
        QJsonObject clip = clips[0].toObject();
        clip[QStringLiteral("audioKeyframes")] = points;
        clips[0] = clip;
        track[QStringLiteral("clips")] = clips;
        tracks[i] = track;
    }
    body[QStringLiteral("tracks")] = tracks;
    result[QStringLiteral("project")] = body;
    return result;
}

QJsonObject point(double frame, const QJsonValue& decibels)
{
    return {{QStringLiteral("sourceFrame"), frame}, {QStringLiteral("decibels"), decibels}};
}

} // namespace

class AudioKeyframeTest : public QObject
{
    Q_OBJECT

private slots:
    void interpolationAndEndpointHold()
    {
        QCOMPARE(audioKeyframeDecibels({}, 20), 0.0);
        const std::vector<AudioKeyframe> one{{25, -12}};
        QCOMPARE(audioKeyframeDecibels(one, 0), -12.0);
        QCOMPARE(audioKeyframeDecibels(one, 100), -12.0);
        const std::vector<AudioKeyframe> points{{30, 0}, {80, -20}, {130, 12}};
        QVERIFY(validateAudioKeyframes(points, kMaxTimelineFrames));
        QCOMPARE(audioKeyframeDecibels(points, 0), 0.0);
        QCOMPARE(audioKeyframeDecibels(points, 55), -10.0);
        QCOMPARE(audioKeyframeDecibels(points, 80), -20.0);
        QCOMPARE(audioKeyframeDecibels(points, 105), -4.0);
        QCOMPARE(audioKeyframeDecibels(points, kMaxTimelineFrames), 12.0);
    }

    void sortedUpsertBoundsAndRejectionAreAtomic()
    {
        Project project = audioProject();
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 80, -12, sources));
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30, 0, sources));
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 129, 12, sources));
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 80, -20, sources));
        const auto& keys = timeline.clip(QStringLiteral("clip"))->audioKeyframes;
        QCOMPARE(keys.size(), size_t{3});
        QCOMPARE(keys[0].sourceFrame, FrameCount{30});
        QCOMPARE(keys[1].decibels, -20.0);
        QVERIFY(project.validate());
        const Project before = project;
        for (const double invalid : {-61.0, 12.1, std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!timeline.setAudioKeyframe(QStringLiteral("clip"), 80, invalid, sources));
            QCOMPARE(project, before);
        }
        QVERIFY(!timeline.setAudioKeyframe(QStringLiteral("clip"), 29, 0, sources));
        QVERIFY(!timeline.setAudioKeyframe(QStringLiteral("clip"), 130, 0, sources));
        QVERIFY(!timeline.setAudioKeyframe(QStringLiteral("missing"), 30, 0, sources));
        QVERIFY(!timeline.removeAudioKeyframe(QStringLiteral("clip"), 40));
        QCOMPARE(project, before);
        QVERIFY(timeline.removeAudioKeyframe(QStringLiteral("clip"), 80));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->audioKeyframes.size(), size_t{2});
        QVERIFY(timeline.clearAudioKeyframes(QStringLiteral("clip")));
        QVERIFY(timeline.clip(QStringLiteral("clip"))->audioKeyframes.empty());
    }

    void limitsAndInvalidModelEnvelopes()
    {
        Project project = audioProject(2000);
        auto& timeline = project.timeline();
        for (int i = 0; i < kMaxAudioKeyframes; ++i) {
            QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30 + i, -6,
                                              project.sourceLookup()));
        }
        const Project full = project;
        QVERIFY(
            !timeline.setAudioKeyframe(QStringLiteral("clip"), 1500, 0, project.sourceLookup()));
        QCOMPARE(project, full);
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30, -12, project.sourceLookup()));
        QVERIFY(!validateAudioKeyframes({{0, 0}, {0, 1}}, kMaxTimelineFrames));
        QVERIFY(!validateAudioKeyframes({{10, 0}, {5, 1}}, kMaxTimelineFrames));
        QVERIFY(!validateAudioKeyframes({{-1, 0}}, kMaxTimelineFrames));
        QVERIFY(!validateAudioKeyframes({{kMaxTimelineFrames + 1, 0}}, kMaxTimelineFrames));
        auto tracks = timeline.tracks();
        for (Track& track : tracks) {
            if (!track.clips.empty()) {
                track.clips[0].audioKeyframes = {{0, 0}, {0, 1}};
            }
        }
        timeline.restoreTracks(tracks);
        QVERIFY(!project.validate());
    }

    void undoRedoAndSliderMerging()
    {
        ProjectDocument document(audioProject());
        const Project original = document.project();
        QVERIFY(document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 30, -6)));
        QVERIFY(document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 30, -12)));
        QVERIFY(document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 30, -20)));
        const Project edited = document.project();
        QVERIFY(document.undo());
        QCOMPARE(document.project(), original);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        QVERIFY(document.redo());
        QCOMPARE(document.project(), edited);
        document.breakMerge();
        QVERIFY(document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 80, 0)));
        const Project twoPoints = document.project();
        QVERIFY(document.execute(edits::removeAudioKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), twoPoints);
        QVERIFY(document.execute(edits::clearAudioKeyframes(QStringLiteral("clip"))));
        QVERIFY(document.undo());
        QCOMPARE(document.project(), twoPoints);
        QVERIFY(!document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 80, 13)));
        QCOMPARE(document.project(), twoPoints);
    }

    void locksAndSilentMediaRejectEdits()
    {
        Project project = audioProject();
        QVERIFY(project.timeline().setAudioKeyframe(QStringLiteral("clip"), 30, -12,
                                                    project.sourceLookup()));
        const Id trackId = project.timeline().trackOfClip(QStringLiteral("clip"))->id;
        QVERIFY(project.timeline().setTrackLocked(trackId, true));
        ProjectDocument document(project);
        QVERIFY(!document.execute(edits::setAudioKeyframe(QStringLiteral("clip"), 80, 0)));
        QVERIFY(!document.execute(edits::removeAudioKeyframe(QStringLiteral("clip"), 30)));
        QVERIFY(!document.execute(edits::clearAudioKeyframes(QStringLiteral("clip"))));
        QCOMPARE(document.project(), project);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        auto silent = [](const Id&) -> std::optional<SourceLimits> {
            return SourceLimits{6000, true, false};
        };
        Project other = audioProject();
        QVERIFY(!other.timeline().setAudioKeyframe(QStringLiteral("clip"), 30, 0, silent));
    }

    void editingPreservesSourceAnchorsAndScalarAttributesStayIndependent()
    {
        Project project = audioProject();
        auto& timeline = project.timeline();
        const auto sources = project.sourceLookup();
        const Id trackId = timeline.trackOfClip(QStringLiteral("clip"))->id;
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 30, 0, sources));
        QVERIFY(timeline.setAudioKeyframe(QStringLiteral("clip"), 80, -20, sources));
        const auto keys = timeline.clip(QStringLiteral("clip"))->audioKeyframes;
        QVERIFY(timeline.trimClipStart(QStringLiteral("clip"), 20, sources));
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->sourceIn, FrameCount{40});
        QCOMPARE(timeline.clip(QStringLiteral("clip"))->audioKeyframes, keys);
        QVERIFY(timeline.splitClip(QStringLiteral("clip"), 60, QStringLiteral("right")));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->audioKeyframes, keys);
        QCOMPARE(timeline.clip(QStringLiteral("right"))->sourceIn, FrameCount{80});
        QVERIFY(timeline.slipClip(QStringLiteral("right"), 90, sources));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->audioKeyframes, keys);
        QVERIFY(timeline.moveClip(QStringLiteral("right"), trackId, 200, sources));
        QCOMPARE(timeline.clip(QStringLiteral("right"))->audioKeyframes, keys);
        ProjectDocument document(project);
        QVERIFY(document.execute(
            edits::duplicateClip(QStringLiteral("right"), QStringLiteral("copy"))));
        QCOMPARE(document.project().timeline().clip(QStringLiteral("copy"))->audioKeyframes, keys);
        ClipProperties properties;
        properties.gain = 0.5;
        QVERIFY(
            document.execute(edits::pasteClipProperties({QStringLiteral("right")}, properties)));
        QCOMPARE(document.project().timeline().clip(QStringLiteral("right"))->audioKeyframes, keys);
        QVERIFY(document.project().validate());
    }

    void persistenceAndLegacyCompatibility()
    {
        Project project = audioProject();
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 2);
        for (const int version : {1, 2}) {
            QJsonObject legacy = project_format::toJson(project);
            legacy[QStringLiteral("version")] = version;
            const auto loaded = project_format::fromJson(legacy);
            QVERIFY(loaded);
            QVERIFY(loaded->timeline().clip(QStringLiteral("clip"))->audioKeyframes.empty());
        }
        QVERIFY(project.timeline().setAudioKeyframe(QStringLiteral("clip"), 30, -12.5,
                                                    project.sourceLookup()));
        QVERIFY(project.timeline().setAudioKeyframe(QStringLiteral("clip"), 80, 3.25,
                                                    project.sourceLookup()));
        const QJsonObject saved = project_format::toJson(project);
        QCOMPARE(saved[QStringLiteral("version")].toInt(), 3);
        const auto loaded = project_format::fromBytes(project_format::toBytes(project));
        QVERIFY(loaded);
        QCOMPARE(loaded->timeline(), project.timeline());
        QJsonObject tooNew = saved;
        tooNew[QStringLiteral("version")] = project_format::kCurrentVersion + 1;
        QCOMPARE(project_format::fromJson(tooNew).error().code, ErrorCode::UnsupportedVersion);
        QJsonObject wrongVersion = saved;
        wrongVersion[QStringLiteral("version")] = 2;
        QVERIFY(!project_format::fromJson(wrongVersion));
        QVERIFY(project.timeline().clearAudioKeyframes(QStringLiteral("clip")));
        QCOMPARE(project_format::toJson(project)[QStringLiteral("version")].toInt(), 2);
    }

    void malformedDocumentsAreRejected()
    {
        QJsonObject document = project_format::toJson(audioProject());
        document[QStringLiteral("version")] = 3;
        const auto rejects = [&](const QJsonValue& points) {
            const auto result = project_format::fromJson(withKeyframes(document, points));
            return !result && result.error().code == ErrorCode::InvalidDocument;
        };
        QVERIFY(rejects(QStringLiteral("not an array")));
        QVERIFY(rejects(QJsonArray{QStringLiteral("not an object")}));
        QVERIFY(rejects(QJsonArray{point(30.5, 0)}));
        QVERIFY(rejects(QJsonArray{point(-1, 0)}));
        QVERIFY(rejects(QJsonArray{point(kMaxTimelineFrames + 1, 0)}));
        QVERIFY(rejects(QJsonArray{point(30, -61)}));
        QVERIFY(rejects(QJsonArray{point(30, 13)}));
        QVERIFY(rejects(QJsonArray{point(30, QStringLiteral("quiet"))}));
        QVERIFY(rejects(QJsonArray{point(30, QJsonValue::Null)}));
        QVERIFY(rejects(QJsonArray{QJsonObject{{QStringLiteral("sourceFrame"), 30}}}));
        QVERIFY(rejects(QJsonArray{QJsonObject{{QStringLiteral("decibels"), 0}}}));
        QVERIFY(rejects(QJsonArray{point(30, 0), point(30, -6)}));
        QVERIFY(rejects(QJsonArray{point(80, 0), point(30, -6)}));
        QJsonArray oversized;
        for (int i = 0; i <= kMaxAudioKeyframes; ++i) {
            oversized.append(point(i, 0));
        }
        QVERIFY(rejects(oversized));
    }
};

QTEST_GUILESS_MAIN(AudioKeyframeTest)
#include "tst_audio_keyframes.moc"
