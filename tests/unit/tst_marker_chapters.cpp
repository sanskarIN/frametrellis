// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/marker_chapters.h"

#include <QTest>

using namespace ft;

namespace {

Project makeTimedProject(FrameRate rate = FrameRate::integer(30))
{
    ProjectSettings settings;
    settings.frameRate = rate;
    Project project = Project::createNew(QStringLiteral("Timed chapters"), settings);
    MediaAsset media;
    media.id = QStringLiteral("source");
    media.name = QStringLiteral("clip.mp4");
    media.filePath = QStringLiteral("/tmp/marker-chapters-source.mp4");
    media.info.kind = MediaKind::Video;
    media.info.hasVideo = true;
    media.info.durationUs = 20'000'000;
    Q_ASSERT(project.addAsset(media));
    Clip clip;
    clip.id = QStringLiteral("clip");
    clip.assetId = media.id;
    clip.duration = 300;
    Q_ASSERT(project.timeline().insertClip(project.timeline().tracks().front().id, clip,
                                           project.sourceLookup()));
    return project;
}

} // namespace

class MarkerChaptersTest : public QObject
{
    Q_OBJECT

private slots:
    void requiresOpeningChapter()
    {
        Project project = Project::createNew(QStringLiteral("Chapters"), {});
        QVERIFY(!chapters::toText(project));
        QVERIFY(project.setMarkers({{QStringLiteral("a"), 30, QStringLiteral("Too late")}}));
        auto text = chapters::toText(project);
        QVERIFY(!text);
        QCOMPARE(text.error().code, ErrorCode::InvalidArgument);
        QCOMPARE(project.markers().front().frame, FrameCount{30});
    }

    void exportsTimelineCuesWithoutEditing()
    {
        Project project = Project::createNew(QStringLiteral("Chapters"), {});
        QVERIFY(project.setMarkers(
            {{QStringLiteral("m3"), 108000, QStringLiteral("Finale")},
             {QStringLiteral("m1"), 0, QStringLiteral("Intro")},
             {QStringLiteral("m2"), 1950, QStringLiteral("  Color  grade\n  notes  ")}}));
        const Project before = project;
        auto text = chapters::toText(project);
        QVERIFY(text);
        QCOMPARE(*text, QStringLiteral("00:00:00 Intro\n"
                                       "00:01:05 Color grade notes\n"
                                       "01:00:00 Finale"));
        QCOMPARE(project, before);
    }

    void usesExactRationalRateWithoutRoundingUp()
    {
        ProjectSettings settings;
        settings.frameRate = *FrameRate::create(30000, 1001);
        Project project = Project::createNew(QStringLiteral("NTSC"), settings);
        QVERIFY(project.setMarkers({{QStringLiteral("a"), 0, QStringLiteral("Start")},
                                    {QStringLiteral("b"), 30000, QStringLiteral("Second")}}));
        auto text = chapters::toText(project);
        QVERIFY(text);
        QCOMPARE(*text, QStringLiteral("00:00:00 Start\n00:16:41 Second"));
    }

    void rejectsAmbiguousSecondButKeepsMarkers()
    {
        Project project = Project::createNew(QStringLiteral("Review"), {});
        QVERIFY(project.setMarkers({{QStringLiteral("a"), 0, QStringLiteral("Start")},
                                    {QStringLiteral("b"), 7, QStringLiteral("Same second")}}));
        const Project original = project;
        auto result = chapters::toText(project);
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::InvalidArgument);
        QCOMPARE(project, original);
    }

    void webVttExportsFrameAccurateCuesAndEscapesMarkup()
    {
        Project project = makeTimedProject();
        QCOMPARE(project.timeline().duration(), FrameCount{300});
        QVERIFY(project.setMarkers({
            {QStringLiteral("intro"), 0, QStringLiteral("Opening & <notes> -->")},
            {QStringLiteral("review"), 60, QString::fromUtf8("रंग सुधार")}}));
        const Project unchanged = project;
        const auto result = chapters::toWebVtt(project);
        QVERIFY(result);
        QCOMPARE(*result,
                 QString::fromUtf8("WEBVTT\\n\\n"
                                   "00:00:00.000 --> 00:00:02.000\\n"
                                   "Opening &amp; &lt;notes&gt; →\\n\\n"
                                   "00:00:02.000 --> 00:00:10.000\\n"
                                   "रंग सुधार\\n"));
        QCOMPARE(project, unchanged);
    }

    void webVttPreservesFractionalFrameRates()
    {
        Project project = makeTimedProject(*FrameRate::create(30000, 1001));
        QVERIFY(project.setMarkers({
            {QStringLiteral("begin"), 0, QStringLiteral("Opening")},
            {QStringLiteral("next"), 30, QStringLiteral("Cut")}}));
        auto result = chapters::toWebVtt(project);
        QVERIFY(result);
        QVERIFY(result->contains(QStringLiteral("00:00:00.000 --> 00:00:01.001")));
        QVERIFY(result->contains(QStringLiteral("00:00:01.001 --> 00:00:10.010")));
    }

    void webVttRejectsEmptyOverlappingAndOutOfBoundsCues()
    {
        Project project = makeTimedProject();
        QVERIFY(!chapters::toWebVtt(project));
        QVERIFY(project.setMarkers({{QStringLiteral("late"), 3, QStringLiteral("Late")}}));
        QVERIFY(!chapters::toWebVtt(project));
        QVERIFY(project.setMarkers({
            {QStringLiteral("intro"), 0, QStringLiteral("Intro")},
            {QStringLiteral("same"), 0, QStringLiteral("Same frame")}}));
        const Project sameFrame = project;
        QVERIFY(!chapters::toWebVtt(project));
        QCOMPARE(project, sameFrame);
        QVERIFY(project.setMarkers({
            {QStringLiteral("intro"), 0, QStringLiteral("Intro")},
            {QStringLiteral("atEnd"), 300, QStringLiteral("At sequence end")}}));
        QVERIFY(!chapters::toWebVtt(project));
        QVERIFY(project.setMarkers({
            {QStringLiteral("intro"), 0, QStringLiteral("Intro")},
            {QStringLiteral("past"), 301, QStringLiteral("Beyond sequence")}}));
        QVERIFY(!chapters::toWebVtt(project));

        Project empty = Project::createNew(QStringLiteral("No clips"), {});
        QVERIFY(empty.setMarkers({{QStringLiteral("intro"), 0, QStringLiteral("Intro")}}));
        QVERIFY(!chapters::toWebVtt(empty));
    }

    void preservesUnicodeAndSequenceEndCue()
    {
        Project project = Project::createNew(QStringLiteral("Review"), {});
        QVERIFY(project.setMarkers({{QStringLiteral("a"), 0, QString::fromUtf8("शुरुआत")},
                                    {QStringLiteral("b"), 1800, QString::fromUtf8("अंत")}}));
        const auto result = chapters::toText(project);
        QVERIFY(result);
        QCOMPARE(*result, QString::fromUtf8("00:00:00 शुरुआत\n00:01:00 अंत"));
    }
};

QTEST_GUILESS_MAIN(MarkerChaptersTest)
#include "tst_marker_chapters.moc"
