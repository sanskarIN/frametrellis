// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/marker_edits.h"
#include "editing/project_document.h"
#include "project/project_serializer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace ft;

class MarkerTest : public QObject
{
    Q_OBJECT

private slots:
    void markerEditsAreAtomicUndoableAndSorted()
    {
        ProjectDocument document(Project::createNew(QStringLiteral("Review"), {}));
        QSignalSpy changed(&document, &ProjectDocument::changed);
        QVERIFY(document.execute(edits::addMarker({QStringLiteral("b"), 90,
                                                    QStringLiteral("Second shot")})));
        QVERIFY(document.execute(edits::addMarker({QStringLiteral("a"), 15,
                                                    QStringLiteral("First shot")})));
        QCOMPARE(document.project().markers().size(), size_t{2});
        QCOMPARE(document.project().markers()[0].id, QStringLiteral("a"));
        QCOMPARE(document.project().markers()[1].frame, FrameCount{90});
        QVERIFY(document.isModified());
        QVERIFY(changed.size() >= 2);
        QVERIFY(changed.last().at(0).value<ChangeSet>().markers);

        QVERIFY(document.execute(edits::moveMarker(QStringLiteral("b"), 5)));
        QCOMPARE(document.project().markers()[0].id, QStringLiteral("b"));
        QVERIFY(document.execute(edits::renameMarker(QStringLiteral("b"),
                                                      QStringLiteral("  Intro  "))));
        QCOMPARE(document.project().markers()[0].label, QStringLiteral("Intro"));
        QVERIFY(document.execute(edits::removeMarker(QStringLiteral("a"))));
        QCOMPARE(document.project().markers().size(), size_t{1});

        QVERIFY(document.undo()); // remove
        QCOMPARE(document.project().markers().size(), size_t{2});
        QVERIFY(document.undo()); // rename
        QCOMPARE(document.project().markers()[0].label, QStringLiteral("Second shot"));
        QVERIFY(document.undo()); // move
        QCOMPARE(document.project().markers()[1].frame, FrameCount{90});
        QVERIFY(document.redo());
        QCOMPARE(document.project().markers()[0].frame, FrameCount{5});
        QVERIFY(document.redo());
        QCOMPARE(document.project().markers()[0].label, QStringLiteral("Intro"));
        QVERIFY(document.redo());
        QCOMPARE(document.project().markers().size(), size_t{1});
    }

    void invalidEditsLeaveDocumentIntact()
    {
        ProjectDocument document(Project::createNew(QStringLiteral("Safe"), {}));
        const Project original = document.project();
        QVERIFY(!document.execute(edits::addMarker({QStringLiteral("bad"), -1,
                                                     QStringLiteral("Bad frame")})));
        QVERIFY(!document.execute(edits::addMarker({QStringLiteral("bad"), 3,
                                                     QStringLiteral("   ")})));
        QVERIFY(!document.execute(edits::removeMarker(QStringLiteral("missing"))));
        QVERIFY(!document.execute(edits::renameMarker(QStringLiteral("missing"),
                                                       QStringLiteral("x"))));
        QCOMPARE(document.project(), original);
        QVERIFY(!document.canUndo());
        QVERIFY(!document.isModified());
        QVERIFY(document.execute(edits::addMarker({QStringLiteral("valid"), 0,
                                                    QStringLiteral("Start")})));
        const Project after = document.project();
        QVERIFY(!document.execute(edits::addMarker({QStringLiteral("valid"), 4,
                                                     QStringLiteral("Duplicate")})));
        QVERIFY(!document.execute(edits::moveMarker(QStringLiteral("valid"),
                                                     kMaxTimelineFrames + 1)));
        QVERIFY(!document.execute(edits::renameMarker(
            QStringLiteral("valid"), QString(121, QLatin1Char('x')))));
        QCOMPARE(document.project(), after);
    }

    void markersPersistInV2AndV1ProjectsStillLoad()
    {
        Project project = Project::createNew(QStringLiteral("Cues"), {});
        QVERIFY(project.setMarkers({{QStringLiteral("second"), 125,
                                     QStringLiteral("Dialogue")},
                                    {QStringLiteral("first"), 0,
                                     QStringLiteral("Opening")}}));
        const QJsonObject saved = project_format::toJson(project);
        QCOMPARE(saved.value(QLatin1String("version")).toInt(), 2);
        const QJsonArray markers = saved[QLatin1String("project")]
                                       .toObject()[QLatin1String("markers")].toArray();
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers[0].toObject()[QLatin1String("label")].toString(),
                 QStringLiteral("Opening"));
        auto reloaded = project_format::fromBytes(project_format::toBytes(project));
        QVERIFY(reloaded);
        QCOMPARE(reloaded->markers(), project.markers());
        QCOMPARE(reloaded->settings(), project.settings());
        QCOMPARE(reloaded->name(), project.name());

        QJsonObject legacy = saved;
        QJsonObject body = legacy[QLatin1String("project")].toObject();
        body.remove(QLatin1String("markers"));
        legacy[QLatin1String("project")] = body;
        legacy[QLatin1String("version")] = 1;
        auto parsedLegacy = project_format::fromJson(legacy);
        QVERIFY(parsedLegacy);
        QVERIFY(parsedLegacy->markers().empty());
    }

    void malformedMarkerInputIsRejected()
    {
        Project project = Project::createNew(QStringLiteral("Safe"), {});
        QVERIFY(project.setMarkers({{QStringLiteral("ok"), 25, QStringLiteral("Review")}}));
        QJsonObject original = project_format::toJson(project);
        QJsonObject body = original[QLatin1String("project")].toObject();
        auto reject = [&](QJsonArray items) {
            QJsonObject document = original;
            QJsonObject b = body;
            b[QLatin1String("markers")] = items;
            document[QLatin1String("project")] = b;
            auto result = project_format::fromJson(document);
            return !result && result.error().code == ErrorCode::InvalidDocument;
        };

        QVERIFY(reject(QJsonArray{QJsonValue(QStringLiteral("invalid"))}));
        QVERIFY(reject(QJsonArray{QJsonObject{{QStringLiteral("id"),
                                               QStringLiteral("ok")},
                                              {QStringLiteral("frame"), -1},
                                              {QStringLiteral("label"),
                                               QStringLiteral("Bad")}}}));
        QVERIFY(reject(QJsonArray{QJsonObject{{QStringLiteral("id"),
                                               QStringLiteral("ok")},
                                              {QStringLiteral("frame"), 3.5},
                                              {QStringLiteral("label"),
                                               QStringLiteral("Bad")}}}));
        QVERIFY(reject(QJsonArray{QJsonObject{{QStringLiteral("id"),
                                               QStringLiteral("ok")},
                                              {QStringLiteral("frame"), 10},
                                              {QStringLiteral("label"),
                                               QStringLiteral(" ")}}}));
        QJsonArray duplicates;
        duplicates.append(body[QLatin1String("markers")].toArray()[0]);
        duplicates.append(body[QLatin1String("markers")].toArray()[0]);
        QVERIFY(reject(duplicates));
    }

    void markerCollectionLimitAndFrameRateGuard()
    {
        Project p = Project::createNew(QStringLiteral("Limit"), {});
        std::vector<TimelineMarker> markers;
        for (int i = 0; i <= kMaxTimelineMarkers; ++i) {
            markers.push_back({QStringLiteral("m%1").arg(i), i,
                               QStringLiteral("Marker")});
        }
        QVERIFY(!p.setMarkers(markers));
        QVERIFY(p.markers().empty());
        QVERIFY(p.setMarkers({{QStringLiteral("one"), 0,
                               QStringLiteral("Start")}}));
        ProjectSettings settings = p.settings();
        settings.frameRate = *FrameRate::create(25, 1);
        QVERIFY(!p.setSettings(settings));
        QCOMPARE(p.settings().frameRate, *FrameRate::create(30, 1));
    }
};

QTEST_GUILESS_MAIN(MarkerTest)
#include "tst_markers.moc"
