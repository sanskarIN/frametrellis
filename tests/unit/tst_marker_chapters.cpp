// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/marker_chapters.h"

#include <QTest>

using namespace ft;

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
