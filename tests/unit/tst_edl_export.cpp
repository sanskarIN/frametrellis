// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/edl_export.h"

#include <QTest>

using namespace ft;

namespace {

Project makeProject(FrameRate rate = FrameRate::integer(30))
{
    ProjectSettings settings;
    settings.frameRate = rate;
    return Project::createNew(QStringLiteral("Picture cut list"), settings);
}

MediaAsset makeAsset(const Project& project, QString id = QStringLiteral("camera"))
{
    MediaAsset media;
    media.id = id;
    media.name = id + QStringLiteral(".mov");
    media.filePath = QStringLiteral("/media/") + media.name;
    media.info.kind = MediaKind::Video;
    media.info.hasVideo = true;
    media.info.durationUs = 300'000'000;
    media.info.frameRate = project.settings().frameRate.rational();
    return media;
}

Clip makeClip(QString id, FrameCount start, FrameCount sourceIn, FrameCount duration)
{
    Clip clip;
    clip.id = id;
    clip.assetId = QStringLiteral("camera");
    clip.start = start;
    clip.sourceIn = sourceIn;
    clip.duration = duration;
    return clip;
}

} // namespace

class EdlExportTest : public QObject
{
    Q_OBJECT
private slots:
    void exportsSourceAndRecordCutsWithBlackGaps()
    {
        Project project = makeProject();
        QVERIFY(project.addAsset(makeAsset(project)));
        const Id track = project.timeline().tracks().front().id;
        QVERIFY(project.timeline().insertClip(track, makeClip(QStringLiteral("a"), 0, 10, 60),
                                              project.sourceLookup()));
        QVERIFY(project.timeline().insertClip(track, makeClip(QStringLiteral("b"), 90, 100, 30),
                                              project.sourceLookup()));
        const Project before = project;

        auto result = interchange::toCmx3600(project);
        QVERIFY(result);
        const QString edl = *result;
        QVERIFY(edl.startsWith(QStringLiteral("TITLE: Picture cut list\nFCM: NON-DROP FRAME\n")));
        QVERIFY(edl.contains(QStringLiteral(
            "001  FT000001 V     C        00:00:00:10 00:00:02:10 00:00:00:00 00:00:02:00")));
        QVERIFY(edl.contains(QStringLiteral(
            "002  BL V     C        00:00:00:00 00:00:01:00 00:00:02:00 00:00:03:00")));
        QVERIFY(edl.contains(QStringLiteral(
            "003  FT000001 V     C        00:00:03:10 00:00:04:10 00:00:03:00 00:00:04:00")));
        QCOMPARE(edl.count(QStringLiteral("* SOURCE FILE: /media/camera.mov")), 2);
        QCOMPARE(project, before);
    }

    void usesDropFrameTimecodeForFractionalVideo()
    {
        Project project = makeProject(*FrameRate::create(30000, 1001));
        QVERIFY(project.addAsset(makeAsset(project)));
        const Id track = project.timeline().tracks().front().id;
        QVERIFY(project.timeline().insertClip(
            track, makeClip(QStringLiteral("a"), 1800, 1800, 30), project.sourceLookup()));
        auto result = interchange::toCmx3600(project);
        QVERIFY(result);
        QVERIFY(result->contains(QStringLiteral("FCM: DROP FRAME")));
        QVERIFY(result->contains(QStringLiteral("00:01:00;02 00:01:01;02")));
        QVERIFY(result->contains(QStringLiteral("00:01:00;02 00:01:01;02")));
    }

    void rejectsMultipleActiveVideoLayersWithoutMutating()
    {
        Project project = makeProject();
        QVERIFY(project.addAsset(makeAsset(project)));
        const auto& tracks = project.timeline().tracks();
        QVERIFY(project.timeline().insertClip(
            tracks[0].id, makeClip(QStringLiteral("one"), 0, 0, 30), project.sourceLookup()));
        QVERIFY(project.timeline().insertClip(
            tracks[1].id, makeClip(QStringLiteral("two"), 0, 40, 30), project.sourceLookup()));
        const Project before = project;
        const auto result = interchange::toCmx3600(project);
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::InvalidArgument);
        QCOMPARE(project, before);
        QVERIFY(project.timeline().setTrackHidden(tracks[1].id, true));
        QVERIFY(interchange::toCmx3600(project));
    }

    void rejectsPictureEffectsAndMismatchedSourceRates()
    {
        Project project = makeProject();
        QVERIFY(project.addAsset(makeAsset(project)));
        const Id track = project.timeline().tracks().front().id;
        QVERIFY(project.timeline().insertClip(
            track, makeClip(QStringLiteral("a"), 0, 0, 40), project.sourceLookup()));
        auto properties = project.timeline().clip(QStringLiteral("a"))->properties;
        properties.video.opacity = 0.5;
        QVERIFY(project.timeline().setClipProperties(QStringLiteral("a"), properties));
        QVERIFY(!interchange::toCmx3600(project));

        properties.video.opacity = 1.0;
        QVERIFY(project.timeline().setClipProperties(QStringLiteral("a"), properties));
        MediaAsset mismatched = *project.asset(QStringLiteral("camera"));
        mismatched.info.frameRate = Rational{25, 1};
        QVERIFY(project.replaceAsset(mismatched));
        QVERIFY(!interchange::toCmx3600(project));
    }

    void rejectsEmptySequencesAndUnsupportedRates()
    {
        Project empty = makeProject();
        QVERIFY(!interchange::toCmx3600(empty));
        Project tooFast = makeProject(FrameRate::integer(60));
        QVERIFY(tooFast.addAsset(makeAsset(tooFast)));
        QVERIFY(tooFast.timeline().insertClip(
            tooFast.timeline().tracks().front().id,
            makeClip(QStringLiteral("fast"), 0, 0, 60), tooFast.sourceLookup()));
        QVERIFY(!interchange::toCmx3600(tooFast));
    }

    void rejectsStillImages()
    {
        Project project = makeProject();
        MediaAsset still = makeAsset(project);
        still.info.kind = MediaKind::Image;
        still.info.frameRate = {};
        QVERIFY(project.addAsset(still));
        QVERIFY(project.timeline().insertClip(
            project.timeline().tracks().front().id,
            makeClip(QStringLiteral("still"), 0, 0, 30), project.sourceLookup()));
        QVERIFY(!interchange::toCmx3600(project));
    }
};

QTEST_GUILESS_MAIN(EdlExportTest)
#include "tst_edl_export.moc"
