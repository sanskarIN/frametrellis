// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Drives the real QML interface with simulated mouse and keyboard input. When the environment
// variable FT_SCREENSHOT_DIR is set, screenshots of the running application are saved there
// (they are used for the documentation).

#include "engine/mlt_runtime.h"
#include "media_fixtures.h"
#include "ui/app_controller.h"
#include "ui/media_bin_model.h"
#include "ui/title_image.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtQml/qqmlextensionplugin.h>

#include <cmath>
#include <limits>

Q_IMPORT_QML_PLUGIN(FrameTrellisPlugin)

using namespace ft;
using namespace ft::testing;

class GuiTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QStringList m_media;
    std::unique_ptr<ui::AppController> m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    // Remains alive as long as the warning signal connection does.
    QList<QQmlError> m_qmlWarnings;

    void screenshot(const QString& name)
    {
        const QString dir = qEnvironmentVariable("FT_SCREENSHOT_DIR");
        if (dir.isEmpty() || !m_window) {
            return;
        }
        QDir().mkpath(dir);
        QTest::qWait(300);
        QVERIFY(m_window->grabWindow().save(QDir(dir).filePath(name + QStringLiteral(".png"))));
    }

    // Delegates created by views are not QObject children, so walk the visual item tree.
    static QQuickItem* findItem(QQuickItem* root, const QString& name)
    {
        if (!root) {
            return nullptr;
        }
        if (root->objectName() == name) {
            return root;
        }
        for (QQuickItem* child : root->childItems()) {
            if (QQuickItem* found = findItem(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    QQuickItem* clipItem(const QString& clipId) const
    {
        return findItem(m_window->contentItem(), QStringLiteral("clip:") + clipId);
    }

    const Track& track(int projectIndex) const
    {
        return m_app->project()->project().timeline().tracks()[static_cast<size_t>(projectIndex)];
    }

    QPoint centreOf(QQuickItem* item) const
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }

    void drag(QPoint from, QPoint to)
    {
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, from);
        const int steps = 8;
        for (int i = 1; i <= steps; ++i) {
            QTest::mouseMove(m_window, from + (to - from) * i / steps, 10);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, to);
        QTest::qWait(50);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("FrameTrellisTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_gui"));
        QVERIFY(m_dir.isValid());
        // Keep settings (recent projects, first-run flag, theme) out of the user's real profile.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_dir.filePath(QStringLiteral("settings")));
        const Status engine = engine::MltRuntime::initialize();
        QVERIFY(engine);

        const QString demo = qEnvironmentVariable("FT_DEMO_MEDIA");
        if (!demo.isEmpty()) {
            m_media = demo.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        } else {
            VideoSpec spec;
            // Match the default sequence rate for exact-timecode interchange smoke tests.
            spec.fps = 30;
            spec.size = QSize(320, 180);
            spec.segmentColors = {
                QColor(QStringLiteral("#1f7a8c")), QColor(QStringLiteral("#e09f3e")),
                QColor(QStringLiteral("#540b0e")), QColor(QStringLiteral("#335c67"))};
            m_media << makeVideo(m_dir.path(), QStringLiteral("harbour.mp4"), spec);
            m_media << makeTone(m_dir.path(), QStringLiteral("ambience.wav"), 6.0, 220.0);
            m_media << makeStill(m_dir.path(), QStringLiteral("title-card.png"),
                                 QColor(QStringLiteral("#8592fb")), QSize(320, 180));
        }
        for (const QString& path : std::as_const(m_media)) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        }

        m_app = std::make_unique<ui::AppController>(engine, QStringLiteral(FT_RENDER_HELPER));
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->addImageProvider(QStringLiteral("ft-thumbnail"),
                                   m_app->createThumbnailProvider());
        m_engine->addImageProvider(QStringLiteral("ft-waveform"), m_app->createWaveformProvider());
        m_engine->setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(m_app.get())}});
        m_qmlWarnings.clear();
        connect(m_engine.get(), &QQmlEngine::warnings, this,
                [this](const QList<QQmlError>& w) { m_qmlWarnings << w; });
        m_engine->loadFromModule("FrameTrellis", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        m_window->resize(1600, 960);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QTest::qWait(200);
        for (const QQmlError& e : std::as_const(m_qmlWarnings)) {
            qWarning() << e.toString();
        }
        QVERIFY2(m_qmlWarnings.isEmpty(), "QML produced warnings while loading");
    }

    void firstRunGuideIsShownAndDismissable()
    {
        QObject* welcome = m_window->findChild<QObject*>(QStringLiteral("welcome"));
        QVERIFY(welcome);
        QVERIFY(welcome->property("visible").toBool());
        screenshot(QStringLiteral("01-welcome"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QVERIFY(QTest::qWaitFor([&] { return !welcome->property("visible").toBool(); }, 2000));
        QVERIFY(!m_app->firstRun());
        screenshot(QStringLiteral("02-empty-workspace"));
    }

    void importShowsMediaInTheBin()
    {
        QSignalSpy imported(m_app->media(), &ui::MediaController::imported);
        QVariantList urls;
        for (const QString& path : std::as_const(m_media)) {
            urls << QUrl::fromLocalFile(path);
        }
        m_app->media()->importFiles(urls);
        QVERIFY(imported.wait(20'000));
        QCOMPARE(m_app->media()->model()->rowCount(), static_cast<int>(m_media.size()));
        // Importing the same files again does not create duplicates.
        m_app->media()->importFiles(urls);
        QTest::qWait(300);
        QCOMPARE(m_app->media()->model()->rowCount(), static_cast<int>(m_media.size()));
    }

    void mediaBinCanFilterWithoutChangingProject()
    {
        ui::MediaBinFilterModel filter;
        filter.setSourceModel(m_app->media()->model());
        const int total = filter.count();
        QVERIFY(total > 0);
        QCOMPARE(total, m_app->media()->model()->rowCount());

        const QString firstName =
            m_app->media()->model()->get(0).value(QStringLiteral("name")).toString();
        QVERIFY(!firstName.isEmpty());
        filter.setSearchText(firstName.toUpper());
        QVERIFY(filter.count() >= 1);
        QCOMPARE(filter.data(filter.index(0, 0), ui::MediaBinModel::NameRole).toString(),
                 firstName);
        filter.setSearchText(QStringLiteral("NO_MATCH_MEDIA_BIN_12345"));
        QCOMPARE(filter.count(), 0);
        filter.setSearchText(QString());
        QCOMPARE(filter.count(), total);

        int expectedAudio = 0;
        for (int i = 0; i < total; ++i) {
            if (m_app->media()->model()->get(i).value(QStringLiteral("kind")).toString() ==
                QLatin1String("audio")) {
                ++expectedAudio;
            }
        }
        filter.setCategory(QStringLiteral("audio"));
        QCOMPARE(filter.count(), expectedAudio);
        filter.setCategory(QStringLiteral("offline"));
        QCOMPARE(filter.count(), 0);
        filter.setCategory(QStringLiteral("invalid-category"));
        QCOMPARE(filter.count(), total);

        QQuickItem* grid = findItem(m_window->contentItem(), QStringLiteral("mediaGrid"));
        QQuickItem* search = findItem(m_window->contentItem(), QStringLiteral("mediaSearchField"));
        QVERIFY(grid);
        QVERIFY(search);
        search->setProperty("text", QStringLiteral("NO_MATCH_MEDIA_BIN_12345"));
        QTRY_COMPARE(grid->property("count").toInt(), 0);
        search->setProperty("text", QString());
        QTRY_COMPARE(grid->property("count").toInt(), total);
        QCOMPARE(m_app->media()->model()->rowCount(), total);
    }

    void doubleClickingATileAppendsIt()
    {
        QQuickItem* grid = findItem(m_window->contentItem(), QStringLiteral("mediaGrid"));
        QVERIFY(grid);
        const int before = m_app->project()->project().timeline().clipCount();
        // Clicking a delegate must not produce a QML ReferenceError from view lookup.
        m_qmlWarnings.clear();
        // The first tile sits at the top-left of the grid.
        const QPoint tile = grid->mapToScene(QPointF(60, 60)).toPoint();
        QTest::mouseDClick(m_window, Qt::LeftButton, Qt::NoModifier, tile);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + 1; },
            3000));
        QVERIFY2(m_qmlWarnings.isEmpty(), "Media-bin tile interaction emitted QML warnings");
        // Append the remaining media through the same action.
        for (int row = 1; row < m_app->media()->model()->rowCount(); ++row) {
            QVERIFY(m_app->timeline()->appendAsset(
                m_app->media()->model()->get(row).value(QStringLiteral("assetId")).toString()));
        }
        QCOMPARE(m_app->project()->project().timeline().clipCount(),
                 before + m_app->media()->model()->rowCount());
        m_app->timeline()->zoomToFit(1200);
        m_app->playback()->seek(40);
        QTest::qWait(500);
        screenshot(QStringLiteral("03-timeline"));
    }

    void slipSelectedClipKeepsPlacementAndIsUndoable()
    {
        // The imported video has no spare source at first; trim its end to make
        // room for a real slip while preserving its position in the sequence.
        const Clip original = track(0).clips.front();
        const Timeline before = m_app->project()->project().timeline();
        QVERIFY(original.duration > 20);
        QVERIFY(m_app->timeline()->trimEnd(original.id, static_cast<double>(original.end() - 10)));
        m_app->timeline()->select(original.id, false);
        QQuickItem* controls =
            findItem(m_window->contentItem(), QStringLiteral("clipSlipControls"));
        QVERIFY(controls);
        QVERIFY(controls->isVisible());
        QVERIFY(m_app->timeline()->slipSelection(5));
        const Clip* slipped = m_app->project()->project().timeline().clip(original.id);
        QVERIFY(slipped);
        QCOMPARE(slipped->start, original.start);
        QCOMPARE(slipped->duration, original.duration - FrameCount{10});
        QCOMPARE(slipped->sourceIn, original.sourceIn + FrameCount{5});
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Slip 1 Clip(s)"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->sourceIn,
                 original.sourceIn);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->sourceIn,
                 original.sourceIn + FrameCount{5});
        m_app->project()->undo();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), before);
        m_app->timeline()->clearSelection();
    }

    void rollSelectedCutShowsInspectorAndRestoresUndoRedo()
    {
        const Clip original = track(0).clips.front();
        QVERIFY(original.duration > 10);
        const Timeline initial = m_app->project()->project().timeline();
        m_app->timeline()->select(original.id, false);
        m_app->playback()->seek(original.start + original.duration / 2);
        QVERIFY(m_app->timeline()->splitAtPlayhead());
        const Clip* left = m_app->project()->project().timeline().clip(original.id);
        QVERIFY(left);
        const FrameCount cut = left->end();
        const Track& splitTrack = track(0);
        Id rightId;
        for (const Clip& c : splitTrack.clips) {
            if (c.id != original.id && c.start == cut) {
                rightId = c.id;
                break;
            }
        }
        QVERIFY(!rightId.isEmpty());
        const FrameCount timelineEnd = m_app->project()->project().timeline().duration();
        m_app->timeline()->select(original.id, false);
        QVERIFY(
            m_app->timeline()->selectedClip().value(QStringLiteral("rollCutAvailable")).toBool());
        QQuickItem* controls =
            findItem(m_window->contentItem(), QStringLiteral("clipRollControls"));
        QVERIFY(controls);
        QTRY_VERIFY(controls->isVisible());
        QVERIFY(m_app->timeline()->rollSelection(1));
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Roll Cut Right"));
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->end(),
                 cut + FrameCount{1});
        QCOMPARE(m_app->project()->project().timeline().clip(rightId)->start, cut + FrameCount{1});
        QCOMPARE(m_app->project()->project().timeline().duration(), timelineEnd);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->end(), cut);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->end(),
                 cut + FrameCount{1});
        m_app->project()->undo(); // roll
        m_app->project()->undo(); // split
        QCOMPARE(m_app->project()->project().timeline(), initial);
        m_app->timeline()->clearSelection();
    }

    void volumeKeyframeInspectorNavigationAndUndo()
    {
        m_app->playback()->pause();
        const Clip original = track(0).clips.front();
        const Timeline initial = m_app->project()->project().timeline();
        QVERIFY(original.duration > 40);
        m_app->timeline()->select(original.id, false);
        m_app->playback()->seek(static_cast<double>(original.start + 10));
        QTRY_VERIFY(m_app->timeline()->audioAutomation().value(QStringLiteral("canEdit")).toBool());
        QQuickItem* controls =
            findItem(m_window->contentItem(), QStringLiteral("audioKeyframeControls"));
        QVERIFY(controls);
        QVERIFY(controls->isVisible());
        QQuickItem* add = findItem(m_window->contentItem(), QStringLiteral("addAudioKeyframe"));
        QVERIFY(add && add->isEnabled());
        QQuickItem* inspectorScroll =
            findItem(m_window->contentItem(), QStringLiteral("inspectorScroll"));
        QVERIFY(inspectorScroll);
        QObject* inspectorFlick = inspectorScroll->property("contentItem").value<QObject*>();
        QVERIFY(inspectorFlick);
        inspectorFlick->setProperty("contentY", std::max(0.0, controls->y() - 15));
        QTest::qWait(50);
        m_qmlWarnings.clear();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(add));
        QTRY_COMPARE(
            m_app->project()->project().timeline().clip(original.id)->audioKeyframes.size(),
            size_t{1});
        QQuickItem* level = findItem(m_window->contentItem(), QStringLiteral("audioKeyframeLevel"));
        QVERIFY(level && level->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(level, "moved", Q_ARG(double, -6.0)));
        QVERIFY(QMetaObject::invokeMethod(level, "moved", Q_ARG(double, -12.0)));
        m_app->timeline()->endEdit();
        QCOMPARE(
            m_app->project()->project().timeline().clip(original.id)->audioKeyframes[0].decibels,
            -12.0);
        m_app->playback()->seek(static_cast<double>(original.start + 30));
        QVERIFY(m_app->timeline()->setAudioKeyframeAtPlayhead(-24));
        m_app->timeline()->endEdit();
        const Timeline automated = m_app->project()->project().timeline();
        m_app->timeline()->previousAudioKeyframe();
        QCOMPARE(m_app->playback()->positionFrames(), original.start + 10);
        m_app->timeline()->nextAudioKeyframe();
        QCOMPARE(m_app->playback()->positionFrames(), original.start + 30);
        QCOMPARE(m_app->project()->project().timeline(), automated);
        QQuickItem* item = clipItem(original.id);
        QVERIFY(item);
        QVERIFY(item->property("audioEnvelope").toList().size() >= 4);
        const double previousZoom = m_app->timeline()->pixelsPerFrame();
        m_app->timeline()->setPixelsPerFrame(24);
        QQuickItem* envelope = findItem(item, QStringLiteral("audioEnvelope:") + original.id);
        QVERIFY(envelope);
        QQuickItem* lanes = findItem(m_window->contentItem(), QStringLiteral("timelineLanes"));
        QVERIFY(lanes);
        QTRY_VERIFY(item->width() > lanes->width());
        QTRY_VERIFY(envelope->width() > 0 && envelope->width() <= lanes->width());
        lanes->setProperty("contentX", static_cast<double>(original.start * 24 + 200));
        QTRY_VERIFY(envelope->x() >= 200);
        QVERIFY(envelope->width() <= lanes->width());
        m_app->timeline()->setPixelsPerFrame(previousZoom);
        lanes->setProperty("contentX", 0);
        QQuickItem* remove =
            findItem(m_window->contentItem(), QStringLiteral("removeAudioKeyframe"));
        QVERIFY(remove && remove->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(remove));
        QTRY_COMPARE(
            m_app->project()->project().timeline().clip(original.id)->audioKeyframes.size(),
            size_t{1});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), automated);
        m_app->project()->redo();
        m_app->project()->undo();
        QVERIFY(!m_app->timeline()->setAudioKeyframeAtPlayhead(
            std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(m_app->project()->project().timeline(), automated);
        QTest::qWait(100);
        QVERIFY2(m_qmlWarnings.isEmpty(), "Volume keyframe controls emitted QML warnings");
        m_app->showStatus(QString());
        screenshot(QStringLiteral("09-volume-keyframes"));
        m_app->project()->undo(); // second point
        m_app->project()->undo(); // first point slider drag
        m_app->project()->undo(); // add first point
        QCOMPARE(m_app->project()->project().timeline(), initial);
        m_app->timeline()->clearSelection();
        inspectorFlick->setProperty("contentY", 0);
    }

    void transformControlsAndMonitorDragsPreserveUndoAndOtherCurves()
    {
        m_app->playback()->pause();
        const Timeline initial = m_app->project()->project().timeline();
        const Clip original = track(0).clips.front();
        const QSize previousSize = m_window->size();
        const QString previousTheme = m_app->theme();
        const auto restore = qScopeGuard([&] {
            m_app->playback()->pause();
            m_app->timeline()->setTransformHandlesVisible(false);
            m_app->setTheme(previousTheme);
            for (int i = 0; i < 20 && m_app->project()->project().timeline() != initial; ++i) {
                m_app->project()->undo();
            }
            m_window->resize(previousSize);
        });
        m_app->timeline()->select(original.id, false);
        m_app->playback()->seek(static_cast<double>(original.start + 10));
        QTRY_VERIFY(
            m_app->timeline()->transformAutomation().value(QStringLiteral("canEdit")).toBool());
        QVERIFY(m_app->timeline()->setAudioKeyframeAtPlayhead(-12));
        m_app->timeline()->endEdit();
        QVERIFY(m_app->timeline()->setOpacityKeyframeAtPlayhead(0.75));
        m_app->timeline()->endEdit();
        QQuickItem* controls =
            findItem(m_window->contentItem(), QStringLiteral("transformKeyframeControls"));
        QQuickItem* scroll = findItem(m_window->contentItem(), QStringLiteral("inspectorScroll"));
        QVERIFY(controls && scroll);
        QObject* flick = scroll->property("contentItem").value<QObject*>();
        QVERIFY(flick);
        const auto reveal = [&](QQuickItem* item) {
            const qreal y = item->mapToItem(controls, QPointF()).y() + controls->y();
            flick->setProperty("contentY", std::max(0.0, y - 30));
            QTest::qWait(60);
        };
        const auto click = [&](const QString& name) {
            QQuickItem* item = findItem(m_window->contentItem(), name);
            if (!item || !item->isEnabled())
                return false;
            reveal(item);
            QSignalSpy clicks(item, SIGNAL(clicked()));
            QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(item));
            return QTest::qWaitFor([&] { return !clicks.isEmpty(); }, 1000);
        };
        QVERIFY(click(QStringLiteral("addTransformKeyframe")));
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("count")).toInt(),
                 1);
        QQuickItem* xSlider =
            findItem(m_window->contentItem(), QStringLiteral("transformPositionX"));
        QVERIFY(xSlider);
        QVERIFY(QMetaObject::invokeMethod(xSlider, "moved", Q_ARG(double, 0.1)));
        QVERIFY(QMetaObject::invokeMethod(xSlider, "moved", Q_ARG(double, 0.2)));
        m_app->timeline()->endEdit();
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("x")).toDouble(),
                 0.2);
        m_app->project()->undo();
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("x")).toDouble(),
                 0.0);
        QVERIFY(m_app->timeline()->setTransformComponent(QStringLiteral("scale"), 0.5));
        m_app->timeline()->endEdit();
        QVERIFY(click(QStringLiteral("showTransformHandles")));
        QQuickItem* overlay =
            findItem(m_window->contentItem(), QStringLiteral("monitorTransformOverlay"));
        QQuickItem* move = findItem(m_window->contentItem(), QStringLiteral("transformMoveArea"));
        QQuickItem* scale =
            findItem(m_window->contentItem(), QStringLiteral("transformScaleHandle"));
        QQuickItem* rotate =
            findItem(m_window->contentItem(), QStringLiteral("transformRotateHandle"));
        QVERIFY(overlay && move && scale && rotate);
        QTRY_VERIFY(overlay->isVisible());
        const Timeline beforeMove = m_app->project()->project().timeline();
        const QPoint centre = centreOf(move);
        drag(centre, centre + QPoint(25, 12));
        QVERIFY(m_app->timeline()->transformAutomation().value(QStringLiteral("x")).toDouble() >
                0.02);
        QVERIFY(m_app->timeline()->transformAutomation().value(QStringLiteral("y")).toDouble() >
                0.02);
        const Timeline moved = m_app->project()->project().timeline();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), beforeMove);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().timeline(), moved);
        const Timeline beforeScale = m_app->project()->project().timeline();
        const QPoint fromScale = centreOf(scale);
        drag(fromScale, fromScale + QPoint(30, 15));
        QVERIFY(m_app->timeline()->transformAutomation().value(QStringLiteral("scale")).toDouble() >
                0.55);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), beforeScale);
        const Timeline beforeRotate = m_app->project()->project().timeline();
        const QPoint rotationCentre = centreOf(move);
        const QPoint fromRotation = centreOf(rotate);
        drag(fromRotation, rotationCentre + QPoint(35, -35));
        const double angle =
            m_app->timeline()->transformAutomation().value(QStringLiteral("rotation")).toDouble();
        QVERIFY2(angle > 15 && angle < 75, qPrintable(QString::number(angle)));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), beforeRotate);
        m_app->project()->redo();
        const VideoTransform first = m_app->project()
                                         ->project()
                                         .timeline()
                                         .clip(original.id)
                                         ->transformKeyframes.front()
                                         .value;
        m_app->playback()->seek(static_cast<double>(original.start + 40));
        QVERIFY(m_app->timeline()->setTransformAtPlayhead(-0.1, 0, 0.75, 720));
        m_app->timeline()->endEdit();
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("count")).toInt(),
                 2);
        QVERIFY(click(QStringLiteral("previousTransformKeyframe")));
        QCOMPARE(m_app->playback()->positionFrames(), original.start + 10);
        QVERIFY(click(QStringLiteral("nextTransformKeyframe")));
        QTRY_COMPARE(m_app->playback()->positionFrames(), original.start + 40);
        m_app->playback()->seek(static_cast<double>(original.start + 25));
        QVERIFY(std::abs(m_app->timeline()
                             ->transformAutomation()
                             .value(QStringLiteral("rotation"))
                             .toDouble() -
                         (first.rotation + 720) / 2) < 0.001);
        const Timeline beforeReset = m_app->project()->project().timeline();
        m_app->playback()->seek(static_cast<double>(original.end() + 1));
        QVERIFY(!overlay->isVisible());
        QVERIFY(!m_app->timeline()->setTransformComponent(QStringLiteral("x"), 0.5));
        QCOMPARE(m_app->project()->project().timeline(), beforeReset);
        m_app->playback()->seek(static_cast<double>(original.start + 10));
        QVERIFY(m_app->timeline()->setTrackFlag(track(0).id, QStringLiteral("locked"), true));
        const Timeline locked = m_app->project()->project().timeline();
        QVERIFY(!m_app->timeline()->setTransformAtPlayhead(0, 0, 1, 0));
        QVERIFY(!m_app->timeline()->clearSelectedTransformKeyframes());
        QCOMPARE(m_app->project()->project().timeline(), locked);
        m_app->project()->undo();
        QVERIFY(m_app->timeline()->resetPictureAdjustments(original.id));
        const Clip* reset = m_app->project()->project().timeline().clip(original.id);
        QVERIFY(reset->transformKeyframes.empty());
        QVERIFY(reset->opacityKeyframes.empty());
        QVERIFY(!reset->audioKeyframes.empty());
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), beforeReset);
        QVERIFY(click(QStringLiteral("removeTransformKeyframe")));
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("count")).toInt(),
                 1);
        m_app->project()->undo();
        QVERIFY(click(QStringLiteral("clearTransformKeyframes")));
        QCOMPARE(m_app->timeline()->transformAutomation().value(QStringLiteral("count")).toInt(),
                 0);
        m_app->project()->undo();
        QQuickItem* badge =
            findItem(clipItem(original.id), QStringLiteral("transformBadge:") + original.id);
        QVERIFY(badge && badge->isVisible());
        m_window->resize(1200, 800);
        QTest::qWait(100);
        reveal(controls);
        screenshot(QStringLiteral("11-transform-keyframes"));
        m_app->setTheme(QStringLiteral("light"));
        QTest::qWait(100);
        screenshot(QStringLiteral("12-transform-keyframes-light"));
        m_app->setTheme(QStringLiteral("dark"));
        m_app->timeline()->setTransformHandlesVisible(false);
        for (int i = 0; i < 20 && m_app->project()->project().timeline() != initial; ++i) {
            m_app->project()->undo();
        }
        QCOMPARE(m_app->project()->project().timeline(), initial);
        m_window->resize(previousSize);
        flick->setProperty("contentY", 0);
    }

    void subtitleEditorGuardsDraftsAndUsesSequenceTimes()
    {
        m_app->playback()->pause();
        const Timeline initial = m_app->project()->project().timeline();
        const SubtitleTrack initialSubtitles = m_app->project()->project().subtitles();
        const QSize previousSize = m_window->size();
        const QString previousTheme = m_app->theme();
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("subtitleDialog"));
        QVERIFY(dialog);
        const auto restore = qScopeGuard([&] {
            QMetaObject::invokeMethod(dialog, "close");
            m_app->setTheme(previousTheme);
            for (int i = 0; i < 12 && m_app->project()->project().subtitles() != initialSubtitles;
                 ++i)
                m_app->project()->undo();
            m_window->resize(previousSize);
        });
        const auto click = [&](const QString& name) {
            QQuickItem* item = findItem(m_window->contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled())
                return false;
            QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(item));
            return true;
        };
        const auto type = [&](const char* value) {
            for (char c : QByteArray(value))
                QTest::keyClick(m_window, c);
        };
        m_app->playback()->seek(0);
        m_window->resize(1200, 800);
        m_app->setTheme(QStringLiteral("dark"));
        m_qmlWarnings.clear();
        QVERIFY(click(QStringLiteral("subtitlesButton")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(click(QStringLiteral("subtitleAdd")));
        QTRY_COMPARE(m_app->subtitles()->rowCount(), 1);
        QQuickItem* text = findItem(m_window->contentItem(), QStringLiteral("subtitleText"));
        QQuickItem* start = findItem(m_window->contentItem(), QStringLiteral("subtitleStart"));
        QQuickItem* end = findItem(m_window->contentItem(), QStringLiteral("subtitleEnd"));
        QVERIFY(text && start && end);
        text->forceActiveFocus();
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        type("Welcome to FrameTrellis");
        QTest::keyClick(m_window, Qt::Key_Return);
        type("A precise subtitle workflow.");
        QTest::keyClick(m_window, Qt::Key_Space);
        QTest::keyClick(m_window, Qt::Key_Left);
        QTest::keyClick(m_window, Qt::Key_Delete);
        QVERIFY(dialog->property("dirty").toBool());
        QCOMPARE(m_app->project()->project().timeline(), initial);
        QCOMPARE(m_app->subtitles()->rowCount(), 1);
        QVERIFY(!m_app->playback()->playing());
        start->forceActiveFocus();
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        type("00:00:00,123");
        QVERIFY(click(QStringLiteral("subtitleApply")));
        QTRY_VERIFY(!dialog->property("dirty").toBool());
        QCOMPARE(m_app->project()->project().subtitles().cues.front().startMs, 123);
        const SubtitleTrack applied = m_app->project()->project().subtitles();
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles().cues.front().startMs, 0);
        m_app->project()->redo();
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
        end->forceActiveFocus();
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        type("00:00:00,100");
        QVERIFY(click(QStringLiteral("subtitleApply")));
        QVERIFY(dialog->property("dirty").toBool());
        QVERIFY(!m_app->subtitles()->error().isEmpty());
        QCOMPARE(m_app->project()->project().subtitles(), applied);
        QVERIFY(click(QStringLiteral("subtitleClose")));
        QObject* guard = m_window->findChild<QObject*>(QStringLiteral("subtitleDraftGuard"));
        QVERIFY(guard);
        QTRY_VERIFY(guard->property("visible").toBool());
        QVERIFY(click(QStringLiteral("subtitleDraftGuardCancel")));
        QTRY_VERIFY(!guard->property("visible").toBool());
        QVERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("dirty").toBool());
        QVERIFY(click(QStringLiteral("subtitleClose")));
        QTRY_VERIFY(guard->property("visible").toBool());
        QVERIFY(click(QStringLiteral("subtitleDraftGuardDiscard")));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(m_app->project()->project().subtitles(), applied);
        QQuickItem* span = findItem(m_window->contentItem(),
                                    QStringLiteral("subtitleSpan:") + applied.cues.front().id);
        QVERIFY(span && span->isVisible());
        QVERIFY(span->width() > 0 && span->width() <= 1200);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(span));
        QCOMPARE(m_app->playback()->positionFrames(), 4);
        QVERIFY(click(QStringLiteral("subtitlesButton")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        screenshot(QStringLiteral("13-subtitles"));
        m_app->setTheme(QStringLiteral("light"));
        screenshot(QStringLiteral("14-subtitles-light"));
        QVERIFY2(m_qmlWarnings.isEmpty(), "Subtitle editor produced QML warnings");
        QCOMPARE(m_app->project()->project().timeline(), initial);
    }

    void opacityKeyframesUseRealControlsAndPreserveAudio()
    {
        m_app->playback()->pause();
        const Clip original = track(0).clips.front();
        const Timeline initial = m_app->project()->project().timeline();
        const double previousZoom = m_app->timeline()->pixelsPerFrame();
        m_app->timeline()->select(original.id, false);
        m_app->playback()->seek(static_cast<double>(original.start + 10));
        QVERIFY(m_app->timeline()->setAudioKeyframeAtPlayhead(-12));
        m_app->timeline()->endEdit();
        QTRY_VERIFY(
            m_app->timeline()->opacityAutomation().value(QStringLiteral("canEdit")).toBool());
        QQuickItem* controls =
            findItem(m_window->contentItem(), QStringLiteral("opacityKeyframeControls"));
        QQuickItem* scroll = findItem(m_window->contentItem(), QStringLiteral("inspectorScroll"));
        QVERIFY(controls && controls->isVisible());
        QVERIFY(scroll);
        QObject* flick = scroll->property("contentItem").value<QObject*>();
        QVERIFY(flick);
        flick->setProperty("contentY", std::max(0.0, controls->y() - 15));
        QTest::qWait(50);
        QQuickItem* add = findItem(m_window->contentItem(), QStringLiteral("addOpacityKeyframe"));
        QQuickItem* level =
            findItem(m_window->contentItem(), QStringLiteral("opacityKeyframeLevel"));
        QVERIFY(add && add->isEnabled());
        QVERIFY(level);
        m_qmlWarnings.clear();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(add));
        QTRY_VERIFY(level->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(level, "moved", Q_ARG(double, 0.5)));
        QVERIFY(QMetaObject::invokeMethod(level, "moved", Q_ARG(double, 0.0)));
        m_app->timeline()->endEdit();
        QTRY_COMPARE(level->property("value").toDouble(), 0.0);
        QCOMPARE(
            m_app->project()->project().timeline().clip(original.id)->opacityKeyframes[0].opacity,
            0.0);
        m_app->playback()->seek(static_cast<double>(original.start + 30));
        QVERIFY(m_app->timeline()->setOpacityKeyframeAtPlayhead(1));
        m_app->timeline()->endEdit();
        const Timeline automated = m_app->project()->project().timeline();
        m_app->playback()->seek(static_cast<double>(original.start + 20));
        QCOMPARE(m_app->timeline()->opacityAutomation().value(QStringLiteral("opacity")).toDouble(),
                 0.5);
        m_app->timeline()->previousOpacityKeyframe();
        QCOMPARE(m_app->playback()->positionFrames(), original.start + 10);
        m_app->timeline()->nextOpacityKeyframe();
        QCOMPARE(m_app->playback()->positionFrames(), original.start + 30);
        QCOMPARE(m_app->project()->project().timeline(), automated);
        m_app->playback()->seek(static_cast<double>(original.end()));
        QVERIFY(!m_app->timeline()->setOpacityKeyframeAtPlayhead(0.5));
        m_app->playback()->seek(static_cast<double>(original.start + 30));
        QVERIFY(!m_app->timeline()->setOpacityKeyframeAtPlayhead(
            std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(m_app->project()->project().timeline(), automated);
        QVERIFY(m_app->timeline()->resetPictureAdjustments(original.id));
        QVERIFY(m_app->project()->project().timeline().clip(original.id)->opacityKeyframes.empty());
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->audioKeyframes.size(),
                 size_t{1});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), automated);
        const Id trackId = m_app->project()->project().timeline().trackOfClip(original.id)->id;
        QVERIFY(m_app->timeline()->setTrackFlag(trackId, QStringLiteral("locked"), true));
        QVERIFY(!m_app->timeline()->setOpacityKeyframeAtPlayhead(0.5));
        QVERIFY(!m_app->timeline()->clearSelectedOpacityKeyframes());
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), automated);
        QQuickItem* remove =
            findItem(m_window->contentItem(), QStringLiteral("removeOpacityKeyframe"));
        QQuickItem* clear =
            findItem(m_window->contentItem(), QStringLiteral("clearOpacityKeyframes"));
        QVERIFY(remove && remove->isEnabled());
        QVERIFY(clear && clear->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(remove));
        QTRY_COMPARE(
            m_app->project()->project().timeline().clip(original.id)->opacityKeyframes.size(),
            size_t{1});
        m_app->project()->undo();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(clear));
        QTRY_VERIFY(
            m_app->project()->project().timeline().clip(original.id)->opacityKeyframes.empty());
        QCOMPARE(m_app->project()->project().timeline().clip(original.id)->audioKeyframes.size(),
                 size_t{1});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), automated);
        m_app->timeline()->setPixelsPerFrame(24);
        QQuickItem* item = clipItem(original.id);
        QQuickItem* lanes = findItem(m_window->contentItem(), QStringLiteral("timelineLanes"));
        QVERIFY(item && lanes);
        QQuickItem* opacity = findItem(item, QStringLiteral("opacityEnvelope:") + original.id);
        QQuickItem* audio = findItem(item, QStringLiteral("audioEnvelope:") + original.id);
        QVERIFY(opacity && audio);
        QVERIFY(!opacity->property("diamonds").toBool());
        QVERIFY(audio->property("diamonds").toBool());
        QTRY_VERIFY(opacity->width() > 0 && opacity->width() <= lanes->width());
        lanes->setProperty("contentX", static_cast<double>(original.start * 24 + 200));
        QTRY_VERIFY(opacity->x() >= 200);
        QVERIFY(opacity->width() <= lanes->width());
        QVERIFY(audio->width() <= lanes->width());
        m_app->timeline()->setPixelsPerFrame(previousZoom);
        lanes->setProperty("contentX", 0);
        m_window->resize(1200, 800);
        flick->setProperty("contentY", std::max(0.0, controls->y() - 15));
        QTest::qWait(100);
        QVERIFY2(m_qmlWarnings.isEmpty(), "Opacity controls or envelope emitted QML warnings");
        m_app->showStatus(QString());
        screenshot(QStringLiteral("10-opacity-keyframes"));
        m_window->resize(1600, 960);
        m_app->project()->undo(); // second opacity point
        m_app->project()->undo(); // opacity slider drag
        m_app->project()->undo(); // add first opacity point
        m_app->project()->undo(); // audio point
        QCOMPARE(m_app->project()->project().timeline(), initial);
        m_app->timeline()->clearSelection();
        flick->setProperty("contentY", 0);
    }

    void opacityKeyframesAlsoWorkForStillImages()
    {
        const Timeline initial = m_app->project()->project().timeline();
        Id imageClip;
        for (const Track& track : initial.tracks()) {
            for (const Clip& clip : track.clips) {
                const MediaAsset* asset = m_app->project()->project().asset(clip.assetId);
                if (track.kind == TrackKind::Video && asset &&
                    asset->info.kind == MediaKind::Image) {
                    imageClip = clip.id;
                    break;
                }
            }
        }
        QVERIFY(!imageClip.isEmpty());
        const Clip* image = initial.clip(imageClip);
        m_app->timeline()->select(imageClip, false);
        m_app->playback()->seek(static_cast<double>(image->start + 10));
        QVERIFY(m_app->timeline()->opacityAutomation().value(QStringLiteral("canEdit")).toBool());
        QVERIFY(m_app->timeline()->setOpacityKeyframeAtPlayhead(0.5));
        m_app->timeline()->endEdit();
        QVERIFY(!m_app->timeline()->audioAutomation().value(QStringLiteral("available")).toBool());
        QVERIFY(!m_app->timeline()->setAudioKeyframeAtPlayhead(-12));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), initial);
        QVERIFY(m_app->timeline()->transformAutomation().value(QStringLiteral("canEdit")).toBool());
        QVERIFY(m_app->timeline()->setTransformAtPlayhead(0.25, -0.25, 0.5, 90));
        m_app->timeline()->endEdit();
        const Timeline transformed = m_app->project()->project().timeline();
        QVERIFY(!m_app->timeline()->setTransformAtPlayhead(0, 0, 0, 0));
        QVERIFY(!m_app->timeline()->setTransformAtPlayhead(std::numeric_limits<double>::quiet_NaN(),
                                                           0, 1, 0));
        QVERIFY(!m_app->timeline()->setTransformComponent(QStringLiteral("unknown"), 1));
        QCOMPARE(m_app->project()->project().timeline(), transformed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), initial);
        m_app->timeline()->clearSelection();
        m_app->timeline()->setTransformHandlesVisible(true);
        QQuickItem* overlay =
            findItem(m_window->contentItem(), QStringLiteral("monitorTransformOverlay"));
        QVERIFY(overlay && !overlay->isVisible());
        m_app->timeline()->setTransformHandlesVisible(false);
    }

    void goToTimecodeSeeksOnlyForValidPositions()
    {
        const double before = m_app->playback()->position();
        QVERIFY(!m_app->playback()->seekToTimecode(QStringLiteral("invalid")));
        QCOMPARE(m_app->playback()->position(), before);
        QVERIFY(!m_app->playback()->seekToTimecode(QStringLiteral("99:00:00:00")));
        QCOMPARE(m_app->playback()->position(), before);

        QTest::keyClick(m_window, Qt::Key_G, Qt::ControlModifier);
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("goToTimecodeDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());

        QQuickItem* field = findItem(m_window->contentItem(), QStringLiteral("goToTimecodeField"));
        QVERIFY(field);
        field->setProperty("text", QStringLiteral("00:00:01:00"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "trySeek"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(static_cast<FrameCount>(m_app->playback()->position()), FrameCount{30});
    }

    void markerDialogUndoRedoAndFrameNavigation()
    {
        const Project before = m_app->project()->project();
        const int previousMarkerCount = static_cast<int>(before.markers().size());

        // Exercise the keyboard action and the actual QML form.
        QTest::keyClick(m_window, Qt::Key_M, Qt::ShiftModifier);
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("markerDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QQuickItem* label = findItem(m_window->contentItem(), QStringLiteral("markerLabelField"));
        QQuickItem* frame = findItem(m_window->contentItem(), QStringLiteral("markerFrameField"));
        QVERIFY(label);
        QVERIFY(frame);
        QVERIFY(label->setProperty("text", QStringLiteral("Review the music beat")));
        QVERIFY(frame->setProperty("text", QStringLiteral("25")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "applyChanges"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        QCOMPARE(static_cast<int>(m_app->project()->project().markers().size()),
                 previousMarkerCount + 1);
        const TimelineMarker created = m_app->project()->project().markers().back();
        QCOMPARE(created.frame, FrameCount{25});
        QCOMPARE(created.label, QStringLiteral("Review the music beat"));
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Marker"));

        // Navigation changes transport only: the timeline and undo history stay intact.
        const Timeline originalTimeline = m_app->project()->project().timeline();
        m_app->playback()->seek(5);
        m_app->timeline()->nextMarker();
        QCOMPARE(m_app->playback()->positionFrames(), FrameCount{25});
        m_app->playback()->seek(80);
        m_app->timeline()->previousMarker();
        QCOMPARE(m_app->playback()->positionFrames(), FrameCount{25});
        QCOMPARE(m_app->project()->project().timeline(), originalTimeline);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Marker"));

        // Invalid marker commands must not mutate project state or push a new edit.
        QVERIFY(!m_app->timeline()->updateMarker(created.id, -1, QStringLiteral("Invalid")));
        QVERIFY(!m_app->timeline()->addMarker(-1, QStringLiteral("Invalid")));
        QCOMPARE(m_app->project()->project().markers().back(), created);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Marker"));

        m_app->project()->undo();
        QCOMPARE(static_cast<int>(m_app->project()->project().markers().size()),
                 previousMarkerCount);
        m_app->project()->redo();
        QCOMPARE(static_cast<int>(m_app->project()->project().markers().size()),
                 previousMarkerCount + 1);
        QVERIFY(m_app->timeline()->removeMarker(created.id));
        QCOMPARE(m_app->project()->project().markers(), before.markers());
    }

    void copyChaptersToClipboardWithoutChangingTheEdit()
    {
        const Timeline originalTimeline = m_app->project()->project().timeline();
        const auto originalMarkers = m_app->project()->project().markers();
        QVERIFY(m_app->timeline()->addMarker(0, QStringLiteral("Opening & Review")));
        const auto markers = m_app->project()->project().markers();
        QCOMPARE(markers.size(), originalMarkers.size() + size_t{1});
        const QString undoLabel = m_app->project()->undoText();

        QVERIFY(m_app->timeline()->copyMarkersAsChapters());
        QClipboard* clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        QCOMPARE(clipboard->text(), QStringLiteral("00:00:00 Opening & Review"));
        QVERIFY(m_app->timeline()->copyMarkersAsWebVtt());
        const QString webVtt = clipboard->text();
        QVERIFY(webVtt.startsWith(QStringLiteral("WEBVTT\n\n"
                                                 "00:00:00.000 --> ")));
        QVERIFY(webVtt.contains(QStringLiteral("\nOpening &amp; Review\n")));
        QCOMPARE(m_app->project()->undoText(), undoLabel);
        QCOMPARE(m_app->project()->project().timeline(), originalTimeline);
        QCOMPARE(m_app->project()->project().markers(), markers);

        QVERIFY(m_app->timeline()->removeMarker(markers.front().id));
        QCOMPARE(m_app->project()->project().markers(), originalMarkers);
    }

    void canExportReadOnlyMediaAuditFromController()
    {
        const Project before = m_app->project()->project();
        QVERIFY(!before.media().empty());
        const bool modified = m_app->project()->isModified();
        const QString undoLabel = m_app->project()->undoText();
        const QString reportPath = m_dir.filePath(QStringLiteral("ui-media-audit.json"));
        QVERIFY(m_app->media()->exportMediaAudit(reportPath));
        QFile file(reportPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument decoded = QJsonDocument::fromJson(file.readAll());
        QVERIFY(decoded.isObject());
        const QJsonObject report = decoded.object();
        QCOMPARE(report.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(report.value(QStringLiteral("assets")).toArray().size(),
                 static_cast<qsizetype>(before.media().size()));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->isModified(), modified);
        QCOMPARE(m_app->project()->undoText(), undoLabel);
    }

    void canImportFolderRecursively()
    {
        const QString root = m_dir.filePath(QStringLiteral("import-folder"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/nested")));
        const QString still =
            makeStill(root + QStringLiteral("/nested"), QStringLiteral("extra-still.png"),
                      QColor(QStringLiteral("#446688")), QSize(32, 18));
        QVERIFY(!still.isEmpty());

        const int before = m_app->media()->model()->rowCount();
        QVERIFY(m_app->media()->importFolder(root));
        QVERIFY(QTest::qWaitFor([&] { return m_app->media()->model()->rowCount() == before + 1; },
                                15000));
        QVERIFY(m_app->project()->project().assetByPath(still));
        QVERIFY(m_app->media()->importFolder(root));
        QTest::qWait(250);
        QCOMPARE(m_app->media()->model()->rowCount(), before + 1);
        const MediaAsset* added = m_app->project()->project().assetByPath(still);
        QVERIFY(added);
        const Id addedId = added->id;
        m_app->media()->remove(addedId);
        QCOMPARE(m_app->media()->model()->rowCount(), before);
    }

    void previewFrameSnapshotIsSafe()
    {
        QTRY_VERIFY(m_app->playback()->hasFrame());
        const QString output = m_dir.filePath(QStringLiteral("preview-snapshot.png"));
        QVERIFY(m_app->playback()->savePreviewFrame(output));
        const QImage snapshot(output);
        QVERIFY(!snapshot.isNull());
        QVERIFY(snapshot.width() > 0);
        QVERIFY(snapshot.height() > 0);

        // A snapshot cannot replace imported footage, even if it is itself a PNG.
        const QString still = m_media.constLast();
        QFile source(still);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray original = source.readAll();
        source.close();
        QVERIFY(!m_app->playback()->savePreviewFrame(still));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), original);
        QVERIFY(!m_app->playback()->savePreviewFrame(
            m_dir.filePath(QStringLiteral("not-a-png.jpg"))));
    }

    void audioClipsShowTheirWaveform()
    {
        const auto& tracks = m_app->project()->project().timeline().tracks();
        const auto audio = std::find_if(tracks.begin(), tracks.end(), [](const Track& t) {
            return t.kind == TrackKind::Audio && !t.clips.empty();
        });
        QVERIFY(audio != tracks.end());
        QQuickItem* clip = clipItem(audio->clips.front().id);
        QVERIFY(clip);
        QQuickItem* waveform = findItem(clip, QStringLiteral("waveform"));
        QVERIFY(waveform);
        QVERIFY(waveform->isVisible());
        // Status 1 is Image.Ready: the sound was decoded and the outline drawn.
        QVERIFY(QTest::qWaitFor([&] { return waveform->property("status").toInt() == 1; }, 20'000));
        QVERIFY(waveform->property("source").toString().startsWith(
            QStringLiteral("image://ft-waveform/") + audio->clips.front().assetId));
    }

    void draggingAClipMovesIt()
    {
        // Arrange: video alone on V1, the still on V2 above it.
        const Id stillId = track(0).clips.back().id;
        QVERIFY(m_app->timeline()->moveClip(stillId, track(1).id, 0));
        QCOMPARE(track(0).clips.size(), size_t{1});
        QTest::qWait(100);

        const Clip original = track(0).clips.front();
        QQuickItem* item = clipItem(original.id);
        QVERIFY(item);
        const double ppf = m_app->timeline()->pixelsPerFrame();
        const QPoint from = centreOf(item);
        const int frames = 30;
        m_app->timeline()->setSnapping(false);
        drag(from, from + QPoint(qRound(frames * ppf), 0));
        const Clip* moved = m_app->project()->project().timeline().clip(original.id);
        QVERIFY(moved);
        QVERIFY2(std::abs(moved->start - (original.start + frames)) <= 1,
                 qPrintable(QStringLiteral("start %1, expected about %2")
                                .arg(moved->start)
                                .arg(original.start + frames)));
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Move Clip"));

        // Drag it back to where it started.
        drag(centreOf(item), centreOf(item) - QPoint(qRound(frames * ppf), 0));
        QVERIFY(std::abs(m_app->project()->project().timeline().clip(original.id)->start -
                         original.start) <= 1);
        m_app->timeline()->setSnapping(true);
    }

    void copyPictureEdlRejectsLayersAndKeepsEditUnchanged()
    {
        // A still on V2 hides part of V1; a flattened-looking EDL would be misleading.
        QVERIFY(!m_app->timeline()->copyPictureAsEdl());
        const Id stillTrack = track(1).id;
        QVERIFY(m_app->timeline()->setTrackFlag(stillTrack, QStringLiteral("hidden"), true));
        const Project before = m_app->project()->project();
        const QString undoLabel = m_app->project()->undoText();
        QVERIFY(m_app->timeline()->copyPictureAsEdl());
        QClipboard* clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        QVERIFY(clipboard->text().startsWith(QStringLiteral("TITLE: ")));
        QVERIFY(clipboard->text().contains(QStringLiteral("001  FT000001 V     C")));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoLabel);
        QVERIFY(m_app->timeline()->setTrackFlag(stillTrack, QStringLiteral("hidden"), false));
    }

    void draggingOntoAnotherClipIsRefused()
    {
        // The still sits on V2 at frame 0 above the video. Put it on V1 after the video, then try
        // to drag it back over the video: the move must be refused and nothing may change.
        const Id stillId = track(1).clips.front().id;
        const Clip video = track(0).clips.front();
        QVERIFY(m_app->timeline()->moveClip(stillId, track(0).id,
                                            static_cast<double>(video.end() + 10)));
        QTest::qWait(100);
        const Clip still = *m_app->project()->project().timeline().clip(stillId);
        QQuickItem* item = clipItem(stillId);
        QVERIFY(item);
        const double ppf = m_app->timeline()->pixelsPerFrame();
        const QString undoBefore = m_app->project()->undoText();
        drag(centreOf(item),
             centreOf(item) - QPoint(qRound((still.start - video.start - 5) * ppf), 0));
        QCOMPARE(m_app->project()->project().timeline().clip(stillId)->start, still.start);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        // Put it back on V2 for the following tests.
        QVERIFY(m_app->timeline()->moveClip(stillId, track(1).id, 0));
    }

    void trimmingTheRightEdge()
    {
        const Clip clip = track(0).clips.front();
        QQuickItem* item = clipItem(clip.id);
        QVERIFY(item);
        const double ppf = m_app->timeline()->pixelsPerFrame();
        m_app->timeline()->setSnapping(false);
        const QPoint edge =
            item->mapToScene(QPointF(item->width() - 2, item->height() / 2)).toPoint();
        drag(edge, edge - QPoint(qRound(20 * ppf), 0));
        const Clip* trimmed = m_app->project()->project().timeline().clip(clip.id);
        QVERIFY(std::abs(trimmed->duration - (clip.duration - 20)) <= 1);
        QCOMPARE(trimmed->start, clip.start);
        m_app->timeline()->setSnapping(true);
    }

    void keyboardSplitUndoAndDelete()
    {
        const Clip clip = track(0).clips.front();
        const FrameCount frame = clip.start + clip.duration / 2;
        m_app->timeline()->clearSelection();
        m_app->playback()->seek(static_cast<double>(frame));
        QTest::qWait(100);
        // Without a selection, S splits every unlocked clip under the playhead.
        int spanning = 0;
        for (const Track& t : m_app->project()->project().timeline().tracks()) {
            for (const Clip& c : t.clips) {
                spanning += (frame > c.start && frame < c.end()) ? 1 : 0;
            }
        }
        QVERIFY(spanning >= 1);
        const int before = m_app->project()->project().timeline().clipCount();
        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_S);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + spanning; },
            2000));
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before; }, 2000));
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + spanning; },
            2000));
        const int afterSplit = before + spanning;

        // Click a clip to select it, then delete it with the keyboard.
        QQuickItem* item = clipItem(track(0).clips.front().id);
        QVERIFY(item);
        QTest::mouseClick(
            m_window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() - 6)).toPoint());
        QCOMPARE(m_app->timeline()->selection().size(), 1);
        m_app->showStatus(QString());
        screenshot(QStringLiteral("04-clip-selected"));
        QTest::keyClick(m_window, Qt::Key_Delete);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == afterSplit - 1; },
            2000));
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == afterSplit; },
            2000));
    }

    void spaceTogglesPlayback()
    {
        m_app->playback()->seek(0);
        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(QTest::qWaitFor([&] { return m_app->playback()->playing(); }, 3000));
        QTest::qWait(300);
        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(QTest::qWaitFor([&] { return !m_app->playback()->playing(); }, 3000));
        QVERIFY(m_app->playback()->position() > 0);
    }

    void transitionsWithTheKeyboardAndInspector()
    {
        // Two touching stills on a new video track: stills can always be blended.
        QString stillId;
        for (int row = 0; row < m_app->media()->model()->rowCount(); ++row) {
            const QVariantMap item = m_app->media()->model()->get(row);
            if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("image")) {
                stillId = item.value(QStringLiteral("assetId")).toString();
            }
        }
        QVERIFY(!stillId.isEmpty());
        QVERIFY(m_app->timeline()->addTrack(QStringLiteral("video")));
        const auto& tracks = m_app->project()->project().timeline().tracks();
        Id trackId;
        for (const Track& t : tracks) {
            if (t.kind == TrackKind::Video && t.clips.empty()) {
                trackId = t.id;
            }
        }
        QVERIFY(!trackId.isEmpty());
        const double length = m_app->timeline()->assetDuration(stillId);
        QVERIFY(m_app->timeline()->dropAsset(stillId, trackId, 0));
        QVERIFY(m_app->timeline()->dropAsset(stillId, trackId, length));
        const Track* lane = m_app->project()->project().timeline().track(trackId);
        QVERIFY(lane && lane->clips.size() == 2);
        const Id first = lane->clips.front().id;
        QTest::qWait(100);

        QQuickItem* item = clipItem(first);
        QVERIFY(item);
        QTest::mouseClick(
            m_window, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 3, item->height() - 6)).toPoint());
        QCOMPARE(m_app->timeline()->selection(), QStringList{first});
        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_T);
        QVERIFY(QTest::qWaitFor(
            [&] {
                return m_app->project()->project().timeline().transitionAfter(first) != nullptr;
            },
            2000));
        const Transition added = *m_app->project()->project().timeline().transitionAfter(first);
        QCOMPARE(added.duration, FrameCount{m_app->timeline()->nominalRate()}); // one second
        QCOMPARE(m_app->timeline()
                     ->selectedTransition()
                     .value(QStringLiteral("transitionId"))
                     .toString(),
                 added.id);
        QVERIFY(m_app->timeline()->selection().isEmpty());

        // The transition is drawn on the timeline and edited in the inspector.
        QTest::qWait(100);
        QQuickItem* overlay =
            findItem(m_window->contentItem(), QStringLiteral("transition:") + added.id);
        QVERIFY(overlay);
        QVERIFY(overlay->isVisible());
        QQuickItem* inspector =
            findItem(m_window->contentItem(), QStringLiteral("transitionInspector"));
        QVERIFY(inspector && inspector->isVisible());
        QVERIFY(m_app->timeline()->setTransitionDuration(added.id, 20));
        m_app->timeline()->endEdit();
        QCOMPARE(m_app->project()->project().timeline().transition(added.id)->duration,
                 FrameCount{20});
        m_app->showStatus(QString());
        screenshot(QStringLiteral("transition"));

        // Delete removes the selected transition; undo brings it back. The overlay is recreated
        // when the transition changes, so look it up again.
        m_app->timeline()->clearSelection();
        QTest::qWait(100);
        overlay = findItem(m_window->contentItem(), QStringLiteral("transition:") + added.id);
        QVERIFY(overlay);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(overlay));
        QCOMPARE(m_app->timeline()->selectedTransition().value(QStringLiteral("count")).toInt(), 1);
        QTest::keyClick(m_window, Qt::Key_Delete);
        QVERIFY(QTest::qWaitFor(
            [&] { return !m_app->project()->project().timeline().transition(added.id); }, 2000));
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().transition(added.id) != nullptr; },
            2000));
        m_app->timeline()->clearSelection();
    }

    void exportDialogValidatesAndExports()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000));
        dialog->setProperty("outputPath", m_dir.filePath(QStringLiteral("gui-export.mp4")));
        QTest::qWait(200);
        screenshot(QStringLiteral("05-export-dialog"));
        QVariantMap settings{
            {QStringLiteral("presetId"), QStringLiteral("draft")},
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("gui-export.mp4"))},
            {QStringLiteral("speedPreset"), QStringLiteral("ultrafast")}};
        QCOMPARE(m_app->exporter()->validate(settings), QString());
        QSignalSpy finished(m_app->exporter(), &ui::ExportController::finished);
        QVERIFY(m_app->exporter()->start(settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QTest::qWait(200);
        screenshot(QStringLiteral("06-export-finished"));
        QMetaObject::invokeMethod(dialog, "close");
    }

    void saveAndReopenRoundTrip()
    {
        const QString file = m_dir.filePath(QStringLiteral("gui-project.ftproj"));
        QVERIFY(m_app->project()->saveAs(file));
        QVERIFY(!m_app->project()->isModified());
        const Timeline saved = m_app->project()->project().timeline();
        QVERIFY(m_app->project()->newProject(0, QStringLiteral("Other")));
        QCOMPARE(m_app->project()->project().timeline().clipCount(), 0);
        QVERIFY(m_app->project()->openProject(file));
        QCOMPARE(m_app->project()->project().timeline(), saved);
        QTest::qWait(300);
        QCOMPARE(m_app->media()->model()->rowCount(), static_cast<int>(m_media.size()));
    }

    void duplicateClipUsingKeyboard()
    {
        const Track& video = track(0);
        QVERIFY(!video.clips.empty());
        const Id original = video.clips.front().id;
        const Id originalAsset = video.clips.front().assetId;
        const FrameCount originalDuration = video.clips.front().duration;
        const int before = m_app->project()->project().timeline().clipCount();
        m_app->timeline()->select(original, false);
        QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + 1; },
            2000));
        const QStringList selection = m_app->timeline()->selection();
        QCOMPARE(selection.size(), 1);
        QVERIFY(selection.front() != original);
        const Clip* copy = m_app->project()->project().timeline().clip(selection.front());
        QVERIFY(copy);
        QCOMPARE(copy->duration, originalDuration);
        QCOMPARE(copy->assetId, originalAsset);
        QVERIFY(m_app->project()->canUndo());
        screenshot(QStringLiteral("07-clip-duplication"));
        m_app->project()->undo();
        QVERIFY(!m_app->project()->project().timeline().clip(selection.front()));
    }

    void duplicateSelectionAndPasteAttributesThroughKeyboard()
    {
        const Timeline& timeline = m_app->project()->project().timeline();
        const auto& videos = timeline.tracks()[0].clips;
        QVERIFY(videos.size() >= 2);
        const Id first = videos[0].id;
        const Id second = videos[1].id;
        QVERIFY(m_app->timeline()->setClipProperty(first, QStringLiteral("opacity"), 0.4));
        m_app->timeline()->select(first, false);
        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(m_app->timeline()->hasCopiedClipAttributes());

        m_app->timeline()->select(second, true);
        const int count = timeline.clipCount();
        QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == count + 2; }, 2000));
        const QStringList copies = m_app->timeline()->selection();
        QCOMPARE(copies.size(), 2);
        for (const Id& id : copies) {
            QVERIFY(id != first && id != second);
            QVERIFY(m_app->project()->project().timeline().clip(id));
        }
        const Timeline beforePaste = m_app->project()->project().timeline();
        QTest::keyClick(m_window, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
        for (const Id& id : copies) {
            const Clip* clip = m_app->project()->project().timeline().clip(id);
            QVERIFY(clip);
            QCOMPARE(clip->properties.video.opacity, 0.4);
        }
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Paste Clip Attributes"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), beforePaste);
        m_app->project()->redo();
        for (const Id& id : copies) {
            QCOMPARE(m_app->project()->project().timeline().clip(id)->properties.video.opacity,
                     0.4);
        }
    }

    void titleImageGenerationAndProjectRoundTrip()
    {
        QVERIFY(!ui::makeTitleImage(QString(), QSize(320, 180), 36, Qt::white));
        QVERIFY(!ui::makeTitleImage(QStringLiteral("Title"), QSize(8192, 8192), 36, Qt::white));
        QVERIFY(!ui::makeTitleImage(QStringLiteral("Title"), QSize(320, 180), 0, Qt::white));
        const auto image = ui::makeTitleImage(QStringLiteral("FrameTrellis"), QSize(320, 180), 42,
                                              QColor(QStringLiteral("#ffffff")));
        QVERIFY(image);
        QCOMPARE(image->size(), QSize(320, 180));
        QCOMPARE(QColor::fromRgba(image->pixel(0, 0)).alpha(), 0);
        bool hasText = false;
        for (int y = 0; y < image->height() && !hasText; ++y) {
            for (int x = 0; x < image->width(); ++x) {
                if (qAlpha(image->pixel(x, y)) != 0) {
                    hasText = true;
                    break;
                }
            }
        }
        QVERIFY(hasText);

        const QString created = m_app->media()->createTitle(QStringLiteral("A title overlay"), 72,
                                                            QStringLiteral("#ffffff"));
        QVERIFY(!created.isEmpty());
        const MediaAsset* asset = m_app->project()->project().asset(created);
        QVERIFY(asset);
        QCOMPARE(asset->info.kind, MediaKind::Image);
        QVERIFY(QFileInfo::exists(asset->filePath));
        QVERIFY(asset->filePath.contains(QStringLiteral("FrameTrellis Titles")));
        QVERIFY(m_app->timeline()->appendTitleAsset(created));
        const Track* titleTrack = m_app->project()->project().timeline().trackOfClip(
            m_app->timeline()->selection().front());
        QVERIFY(titleTrack);
        QCOMPARE(titleTrack->kind, TrackKind::Video);
        if (!qEnvironmentVariableIsEmpty("FT_SCREENSHOT_DIR")) {
            const Clip* titleClip =
                m_app->project()->project().timeline().clip(m_app->timeline()->selection().front());
            QVERIFY(titleClip);
            m_app->playback()->seek(static_cast<double>(titleClip->start + 5));
            m_app->showStatus(QStringLiteral("Title overlay on the timeline"));
            QTest::qWait(800);
            screenshot(QStringLiteral("08-title-overlay"));
        }
        QVERIFY(m_app->project()->save());
        const QString savedPath = m_app->project()->filePath();
        QVERIFY(m_app->project()->openProject(savedPath));
        QVERIFY(m_app->project()->project().asset(created));
        QCOMPARE(m_app->project()->project().asset(created)->status, MediaStatus::Online);
    }

    void creatingTitlesRequiresAnUnlockedVideoTrack()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("titleDialog"));
        QVERIFY(dialog);
        QList<Id> videoTracks;
        for (const Track& track : m_app->project()->project().timeline().tracks()) {
            if (track.kind == TrackKind::Video) {
                videoTracks.append(track.id);
            }
        }
        QVERIFY(!videoTracks.isEmpty());
        QSignalSpy changed(m_app->timeline(), &ui::TimelineController::canAppendTitleChanged);
        for (const Id& id : videoTracks) {
            QVERIFY(m_app->timeline()->setTrackFlag(id, QStringLiteral("locked"), true));
        }
        QVERIFY(!m_app->timeline()->canAppendTitle());
        QVERIFY(changed.count() > 0);

        const size_t before = m_app->project()->project().media().size();
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QVERIFY(QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000));
        QObject* textField = dialog->findChild<QObject*>(QStringLiteral("titleTextField"));
        QObject* createButton = dialog->findChild<QObject*>(QStringLiteral("createTitleButton"));
        QVERIFY(textField);
        QVERIFY(createButton);
        QVERIFY(textField->setProperty("text", QStringLiteral("Do not generate this title")));
        QTRY_VERIFY(!createButton->property("enabled").toBool());
        QCOMPARE(m_app->project()->project().media().size(), before);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));

        // Unlocking a video track re-enables the title action without restarting the dialog.
        QVERIFY(
            m_app->timeline()->setTrackFlag(videoTracks.front(), QStringLiteral("locked"), false));
        QVERIFY(m_app->timeline()->canAppendTitle());
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QVERIFY(QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000));
        QVERIFY(textField->setProperty("text", QStringLiteral("Can place this title")));
        QTRY_VERIFY(createButton->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        // Restore the test project's track state for follow-up editing.
        for (const Id& id : videoTracks) {
            QVERIFY(m_app->timeline()->setTrackFlag(id, QStringLiteral("locked"), false));
        }
    }

    void nudgeSelectionUsingKeyboard()
    {
        QString assetId;
        for (const MediaAsset& asset : m_app->project()->project().media()) {
            if (asset.info.hasVideo) {
                assetId = asset.id;
                break;
            }
        }
        QVERIFY(!assetId.isEmpty());
        QVERIFY(m_app->timeline()->addTrack(QStringLiteral("video")));

        Id trackId;
        for (const Track& track : m_app->project()->project().timeline().tracks()) {
            if (track.kind == TrackKind::Video && track.clips.empty()) {
                trackId = track.id;
            }
        }
        QVERIFY(!trackId.isEmpty());
        QVERIFY(m_app->timeline()->dropAsset(assetId, trackId, 100));
        QCOMPARE(m_app->timeline()->selection().size(), 1);
        const Id clipId = m_app->timeline()->selection().front();
        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_Right, Qt::AltModifier);
        QVERIFY(QTest::qWaitFor(
            [&] {
                const Clip* clip = m_app->project()->project().timeline().clip(clipId);
                return clip && clip->start == 101;
            },
            2000));
        QTest::keyClick(m_window, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor(
            [&] {
                const Clip* clip = m_app->project()->project().timeline().clip(clipId);
                return clip && clip->start == 91;
            },
            2000));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline().clip(clipId)->start, FrameCount{101});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline().clip(clipId)->start, FrameCount{100});
        QVERIFY(m_app->timeline()->deleteSelection(false));
        QVERIFY(m_app->timeline()->removeTrack(trackId));
    }

    void cleanupTestCase()
    {
        m_engine.reset();
        m_app.reset();
        QThreadPool::globalInstance()->waitForDone();
        engine::MltRuntime::shutdown();
    }
};

int main(int argc, char** argv)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    GuiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_gui.moc"
