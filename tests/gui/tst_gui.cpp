// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Drives the real QML interface with simulated mouse and keyboard input. When the environment
// variable FT_SCREENSHOT_DIR is set, screenshots of the running application are saved there
// (they are used for the documentation).

#include "core/timecode.h"
#include "editing/marker_edits.h"
#include "editing/subtitle_edits.h"
#include "editing/timeline_edits.h"
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
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
#include <QUrlQuery>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
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

        // Sorting affects only the proxy, never the project's stored import order.
        QStringList importedOrder;
        for (int i = 0; i < total; ++i) {
            importedOrder.append(
                m_app->media()->model()->get(i).value(QStringLiteral("name")).toString());
        }
        QCOMPARE(filter.sortMode(), QStringLiteral("import"));
        filter.setSortMode(QStringLiteral("name-asc"));
        QCOMPARE(filter.sortMode(), QStringLiteral("name-asc"));
        for (int i = 1; i < total; ++i) {
            const QString previous =
                filter.data(filter.index(i - 1, 0), ui::MediaBinModel::NameRole).toString();
            const QString current =
                filter.data(filter.index(i, 0), ui::MediaBinModel::NameRole).toString();
            QVERIFY(QString::compare(previous, current, Qt::CaseInsensitive) <= 0);
        }
        filter.setSortMode(QStringLiteral("name-desc"));
        for (int i = 1; i < total; ++i) {
            const QString previous =
                filter.data(filter.index(i - 1, 0), ui::MediaBinModel::NameRole).toString();
            const QString current =
                filter.data(filter.index(i, 0), ui::MediaBinModel::NameRole).toString();
            QVERIFY(QString::compare(previous, current, Qt::CaseInsensitive) >= 0);
        }
        // Numeric duration sorts must not use displayed minute/second strings.
        // Still images and unknown durations always follow timed media.
        for (const QString& mode :
             {QStringLiteral("duration-asc"), QStringLiteral("duration-desc")}) {
            filter.setSortMode(mode);
            bool sawUnknown = false;
            qint64 previous =
                mode == QLatin1String("duration-asc") ? 0 : std::numeric_limits<qint64>::max();
            for (int i = 0; i < total; ++i) {
                const qint64 current =
                    filter.data(filter.index(i, 0), ui::MediaBinModel::DurationUsRole).toLongLong();
                if (current <= 0) {
                    sawUnknown = true;
                    continue;
                }
                QVERIFY2(!sawUnknown, "Timed media appeared after a still or unknown duration");
                if (mode == QLatin1String("duration-asc")) {
                    QVERIFY(previous <= current);
                } else {
                    QVERIFY(previous >= current);
                }
                previous = current;
            }
        }
        filter.setSortMode(QStringLiteral("usage-desc"));
        for (int i = 0; i < total; ++i) {
            QCOMPARE(filter.data(filter.index(i, 0), ui::MediaBinModel::NameRole).toString(),
                     importedOrder.at(i)); // No clip uses yet; imported order breaks ties.
        }
        filter.setSortMode(QStringLiteral("unknown"));
        QCOMPARE(filter.sortMode(), QStringLiteral("import"));
        for (int i = 0; i < total; ++i) {
            QCOMPARE(filter.data(filter.index(i, 0), ui::MediaBinModel::NameRole).toString(),
                     importedOrder.at(i));
            QCOMPARE(m_app->media()->model()->get(i).value(QStringLiteral("name")).toString(),
                     importedOrder.at(i));
        }

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
        filter.setCategory(QStringLiteral("changed"));
        QCOMPARE(filter.count(), 0);
        filter.setCategory(QStringLiteral("unused"));
        QCOMPARE(filter.count(), total);
        filter.setCategory(QStringLiteral("used"));
        QCOMPARE(filter.count(), 0);
        // The category remains combinable with a name search, without editing source rows.
        filter.setSearchText(firstName);
        filter.setCategory(QStringLiteral("unused"));
        QCOMPARE(filter.count(), 1);
        filter.setSearchText(QString());
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
        QQuickItem* kind = findItem(m_window->contentItem(), QStringLiteral("mediaKindFilter"));
        QVERIFY(kind);
        kind->setProperty("currentIndex", 7); // Unused
        QTRY_COMPARE(grid->property("count").toInt(), total);
        kind->setProperty("currentIndex", 6); // Used
        QTRY_COMPARE(grid->property("count").toInt(), 0);
        kind->setProperty("currentIndex", 5); // Changed
        QTRY_COMPARE(grid->property("count").toInt(), 0);
        kind->setProperty("currentIndex", 0); // Restore All for following GUI tests.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        QQuickItem* sorter = findItem(m_window->contentItem(), QStringLiteral("mediaSortOrder"));
        QVERIFY(sorter);
        sorter->setProperty("currentIndex", 1); // A–Z through the actual UI.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        sorter->setProperty("currentIndex", 2); // Z–A.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        sorter->setProperty("currentIndex", 3); // Longest first.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        sorter->setProperty("currentIndex", 4); // Shortest first.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        sorter->setProperty("currentIndex", 5); // Most used.
        QTRY_COMPARE(grid->property("count").toInt(), total);
        sorter->setProperty("currentIndex", 0); // Preserve default import order.
        QCOMPARE(m_app->media()->model()->rowCount(), total);
    }

    void doubleClickingATileAppendsIt()
    {
        QQuickItem* grid = findItem(m_window->contentItem(), QStringLiteral("mediaGrid"));
        QVERIFY(grid);
        const int before = m_app->project()->project().timeline().clipCount();
        const int totalMedia = m_app->media()->model()->rowCount();
        // Filtering must respond to new clip usage without touching actual media entries.
        ui::MediaBinFilterModel usedFilter;
        usedFilter.setSourceModel(m_app->media()->model());
        usedFilter.setCategory(QStringLiteral("used"));
        ui::MediaBinFilterModel unusedFilter;
        unusedFilter.setSourceModel(m_app->media()->model());
        unusedFilter.setCategory(QStringLiteral("unused"));
        ui::MediaBinFilterModel usageSort;
        usageSort.setSourceModel(m_app->media()->model());
        usageSort.setSortMode(QStringLiteral("usage-desc"));
        QCOMPARE(usedFilter.count(), 0);
        QCOMPARE(unusedFilter.count(), totalMedia);
        // Clicking a delegate must not produce a QML ReferenceError from view lookup.
        m_qmlWarnings.clear();
        // The first tile sits at the top-left of the grid.
        const QPoint tile = grid->mapToScene(QPointF(60, 60)).toPoint();
        QTest::mouseDClick(m_window, Qt::LeftButton, Qt::NoModifier, tile);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + 1; },
            3000));
        QVERIFY2(m_qmlWarnings.isEmpty(), "Media-bin tile interaction emitted QML warnings");
        QTRY_COMPARE(usedFilter.count(), 1);
        QTRY_COMPARE(unusedFilter.count(), totalMedia - 1);
        QTRY_COMPARE(usageSort.data(usageSort.index(0, 0), ui::MediaBinModel::UsageRole).toInt(),
                     1);
        // Append the remaining media through the same action.
        for (int row = 1; row < m_app->media()->model()->rowCount(); ++row) {
            QVERIFY(m_app->timeline()->appendAsset(
                m_app->media()->model()->get(row).value(QStringLiteral("assetId")).toString()));
        }
        QCOMPARE(m_app->project()->project().timeline().clipCount(),
                 before + m_app->media()->model()->rowCount());
        QTRY_COMPARE(usedFilter.count(), totalMedia);
        QTRY_COMPARE(unusedFilter.count(), 0);
        m_app->timeline()->zoomToFit(1200);
        m_app->playback()->seek(40);
        QTest::qWait(500);
        screenshot(QStringLiteral("03-timeline"));
    }

    void controlZeroFitsEntireTimelineWithoutEditingProject()
    {
        QQuickItem* lanes = findItem(m_window->contentItem(), QStringLiteral("timelineLanes"));
        QVERIFY(lanes);
        QVERIFY(lanes->width() > 100);
        QVERIFY(m_app->timeline()->duration() > 0);
        const Project before = m_app->project()->project();
        const QString history = m_app->project()->undoText();

        m_app->timeline()->zoomToFit(lanes->width());
        const double fitted = m_app->timeline()->pixelsPerFrame();
        m_app->timeline()->zoomIn();
        QVERIFY(!qFuzzyCompare(m_app->timeline()->pixelsPerFrame(), fitted));
        QTest::keyClick(m_window, Qt::Key_0, Qt::ControlModifier);
        QTRY_VERIFY(qFuzzyCompare(m_app->timeline()->pixelsPerFrame(), fitted));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), history);
    }

    void mediaBinCanFindTimelineUsesWithoutEditing()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString assetId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!assetId.isEmpty());

        // Create a second occurrence, then restore the project at the end.
        const Project projectBefore = m_app->project()->project();
        const QStringList previousSelection = timeline->selection();
        const FrameCount previousPlayhead = playback->positionFrames();
        QVERIFY(timeline->appendAssetRange(assetId, 0, 20));
        const Project withTwoUses = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();
        const std::vector<Id> uses = withTwoUses.timeline().clipsUsingAsset(assetId);
        QVERIFY(uses.size() >= 2);

        timeline->clearSelection();
        QVERIFY(!timeline->selectMediaOccurrences(QStringLiteral("nonexistent-asset-id")));
        QVERIFY(timeline->selection().isEmpty());

        QVERIFY(timeline->selectMediaOccurrences(assetId));
        QCOMPARE(timeline->selection().size(), static_cast<int>(uses.size()));
        FrameCount earliest = kMaxTimelineFrames;
        for (const Id& use : uses) {
            QVERIFY(timeline->selection().contains(use));
            const Clip* clip = withTwoUses.timeline().clip(use);
            QVERIFY(clip);
            earliest = std::min(earliest, clip->start);
        }
        QVERIFY(timeline->seekToSelectionBoundary(false));
        QCOMPARE(playback->positionFrames(), earliest);

        const QStringList matches = timeline->selection();
        QVERIFY(!timeline->selectMediaOccurrences(QStringLiteral("nonexistent-asset-id")));
        QCOMPARE(timeline->selection(), matches);
        QCOMPARE(m_app->project()->project(), withTwoUses);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);

        // Undo only the temporary appended excerpt, leaving following GUI tests
        // with their original timeline and playhead.
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), projectBefore);
        timeline->clearSelection();
        bool first = true;
        for (const QString& clipId : previousSelection) {
            timeline->select(clipId, !first);
            first = false;
        }
        playback->seek(static_cast<double>(previousPlayhead));
    }

    void mediaPropertiesInspectorIsReadOnly()
    {
        auto* media = m_app->media();
        const QString assetId = media->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!assetId.isEmpty());
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();

        QVERIFY(media->mediaProperties(QStringLiteral("invalid-asset-id")).isEmpty());
        const QVariantMap facts = media->mediaProperties(assetId);
        QCOMPARE(facts.value(QStringLiteral("assetId")).toString(), assetId);
        QCOMPARE(facts.value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
        QCOMPARE(facts.value(QStringLiteral("filePath")).toString(), m_media.front());
        QCOMPARE(facts.value(QStringLiteral("timelineUses")).toString(),
                 QString::number(before.timeline().clipsUsingAsset(assetId).size()));
        QVERIFY(!facts.value(QStringLiteral("resolution")).toString().isEmpty());
        QVERIFY(!facts.value(QStringLiteral("frameRate")).toString().isEmpty());

        QClipboard* clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->setText(QStringLiteral("preserve on invalid media"));
        QVERIFY(!media->copyMediaProperties(QStringLiteral("invalid-asset-id")));
        QCOMPARE(clipboard->text(), QStringLiteral("preserve on invalid media"));
        QVERIFY(media->copyMediaProperties(assetId));
        QVERIFY(clipboard->text().contains(QStringLiteral("File: ") + m_media.front()));
        QVERIFY(clipboard->text().contains(QStringLiteral("Timeline uses: ")));

        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("mediaPropertiesDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(assetId))));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));

        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);
    }

    void loopPreviewToggleLeavesProjectAndUndoHistoryUnchanged()
    {
        QQuickItem* button = findItem(m_window->contentItem(), QStringLiteral("previewLoopToggle"));
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        QVERIFY(!m_app->playback()->loopPreview());
        const Project before = m_app->project()->project();
        const QString originalUndo = m_app->project()->undoText();
        QSignalSpy changed(m_app->playback(), &ui::PlaybackController::loopPreviewChanged);

        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QCOMPARE(m_app->playback()->loopPreview(), true);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), originalUndo);

        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QCOMPARE(m_app->playback()->loopPreview(), false);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), originalUndo);
    }

    void loopPreviewKeyboardShortcutLeavesProjectAndHistoryUnchanged()
    {
        auto* playback = m_app->playback();
        playback->setLoopPreview(false);
        const Project before = m_app->project()->project();
        const QString undo = m_app->project()->undoText();
        const bool modified = m_app->project()->isModified();

        QTest::keyClick(m_window, Qt::Key_L, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(playback->loopPreview(), true);
        QTest::keyClick(m_window, Qt::Key_L, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(playback->loopPreview(), false);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undo);
        QCOMPARE(m_app->project()->isModified(), modified);
    }

    void sourceExcerptFramePreviewIsFrameAccurateAndReadOnly()
    {
        auto* media = m_app->media();
        const QString videoId = media->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId = media->model()->get(1).value(QStringLiteral("assetId")).toString();
        const FrameRate rate = m_app->project()->project().settings().frameRate;
        const int length = static_cast<int>(m_app->timeline()->assetDuration(videoId));
        QVERIFY(length > 60);

        QVERIFY(media->sourcePreviewUrl(QStringLiteral("missing-id"), 0).isEmpty());
        QVERIFY(media->sourcePreviewUrl(audioId, 0).isEmpty());
        QVERIFY(media->sourcePreviewUrl(videoId, -1).isEmpty());
        QVERIFY(media->sourcePreviewUrl(videoId, length).isEmpty());
        const QUrl url(media->sourcePreviewUrl(videoId, 13));
        QVERIFY(url.isValid());
        QCOMPARE(url.scheme(), QStringLiteral("image"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("frame")), QStringLiteral("13"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("fpsNum")),
                 QString::number(rate.numerator()));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("fpsDen")),
                 QString::number(rate.denominator()));

        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("sourceExcerptDialog"));
        QVERIFY(dialog);
        QVERIFY(
            QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(videoId)),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("Preview test")))));
        QTRY_VERIFY(dialog->property("visible").toBool());

        QQuickItem* previewFrame =
            findItem(m_window->contentItem(), QStringLiteral("sourcePreviewFrame"));
        QQuickItem* previewImage =
            findItem(m_window->contentItem(), QStringLiteral("sourceFramePreviewImage"));
        QQuickItem* inFrame = findItem(m_window->contentItem(), QStringLiteral("sourceExcerptIn"));
        QQuickItem* outFrame =
            findItem(m_window->contentItem(), QStringLiteral("sourceExcerptOut"));
        QVERIFY(previewFrame);
        QVERIFY(previewImage);
        QVERIFY(inFrame);
        QVERIFY(outFrame);
        QVERIFY(previewFrame->setProperty("value", 13));
        QTRY_VERIFY(previewImage->property("source").toString().contains(
            QStringLiteral("frame=13&fpsNum=")));
        QTRY_COMPARE_WITH_TIMEOUT(previewImage->property("status").toInt(), 1, 15000);
        QVERIFY(inFrame->setProperty("value", 13));
        QVERIFY(outFrame->setProperty("value", 29));
        QCOMPARE(outFrame->property("value").toInt() - inFrame->property("value").toInt(), 16);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    }

    void independentSourceMonitorMarksWithoutChangingProgram()
    {
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();
        const FrameCount programBefore = m_app->playback()->positionFrames();

        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* picture =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorFrameImage"));
        QVERIFY(source);
        QVERIFY(tabs);
        QVERIFY(picture);
        QVERIFY(source->property("assetId").toString().isEmpty());

        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QCOMPARE(source->property("assetId").toString(), videoId);
        QVERIFY(source->property("hasPicture").toBool());
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(source->setProperty("previewFrame", 13));
        QTRY_VERIFY(
            picture->property("source").toString().contains(QStringLiteral("frame=13&fpsNum=")));
        QTRY_COMPARE_WITH_TIMEOUT(picture->property("status").toInt(), 1, 15000);
        QTest::keyClick(m_window, Qt::Key_Right);
        QTRY_COMPARE(source->property("previewFrame").toInt(), 14);
        QCOMPARE(m_app->playback()->positionFrames(), programBefore);
        QTest::keyClick(m_window, Qt::Key_Left);
        QTRY_COMPARE(source->property("previewFrame").toInt(), 13);
        QTest::keyClick(m_window, Qt::Key_I);
        QCOMPARE(source->property("markInFrame").toInt(), 13);

        QVERIFY(source->setProperty("previewFrame", 27));
        QTest::keyClick(m_window, Qt::Key_O);
        QCOMPARE(source->property("markOutFrame").toInt(), 28);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);
        QCOMPARE(m_app->playback()->positionFrames(), programBefore);

        // Invalid IDs cannot replace the loaded source or create an edit.
        QVERIFY(QMetaObject::invokeMethod(source, "openFor",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("invalid")))));
        QCOMPARE(source->property("assetId").toString(), videoId);
        QCOMPARE(m_app->project()->project(), before);

        QVERIFY(QMetaObject::invokeMethod(source, "appendRange"));
        const Project changed = m_app->project()->project();
        QCOMPARE(changed.timeline().clipCount(), before.timeline().clipCount() + 1);
        const Clip* excerpt = changed.timeline().clip(m_app->timeline()->selection().first());
        QVERIFY(excerpt);
        QCOMPARE(excerpt->assetId, videoId);
        QCOMPARE(excerpt->sourceIn, FrameCount{13});
        QCOMPARE(excerpt->duration, FrameCount{15});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);

        // Reset only the source's In/Out marks. Neither Program transport nor
        // project/Undo history may change, and the action disables at full range.
        QQuickItem* resetMarks =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorResetMarks"));
        QVERIFY(resetMarks);
        QVERIFY(resetMarks->isEnabled());
        const int sourceDuration = source->property("maxFrames").toInt();
        QVERIFY(QMetaObject::invokeMethod(source, "resetMarks"));
        QCOMPARE(source->property("markInFrame").toInt(), 0);
        QCOMPARE(source->property("markOutFrame").toInt(), sourceDuration);
        QVERIFY(!resetMarks->isEnabled());
        QCOMPARE(m_app->playback()->positionFrames(), programBefore);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);

        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(!source->property("hasPicture").toBool());
        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(source->property("assetId").toString().isEmpty());
        QVERIFY(tabs->setProperty("currentIndex", 0));
    }

    void appendSourceExcerptUsesExactSourceFramesAndUndo()
    {
        auto* timeline = m_app->timeline();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        const Project before = m_app->project()->project();
        const FrameCount length = static_cast<FrameCount>(timeline->assetDuration(videoId));
        QVERIFY(length > 60);
        QVERIFY(timeline->appendAssetRange(videoId, 15, 45));
        const Project changed = m_app->project()->project();
        QCOMPARE(changed.timeline().clipCount(), before.timeline().clipCount() + 1);
        const Id id = timeline->selection().first();
        const Clip* excerpt = changed.timeline().clip(id);
        QVERIFY(excerpt);
        QCOMPARE(excerpt->assetId, videoId);
        QCOMPARE(excerpt->sourceIn, FrameCount{15});
        QCOMPARE(excerpt->duration, FrameCount{30});
        QCOMPARE(excerpt->start, before.timeline().tracks().front().end());
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Clip"));

        const QString previousUndo = m_app->project()->undoText();
        QVERIFY(!timeline->appendAssetRange(videoId, -1, 5));
        QVERIFY(!timeline->appendAssetRange(videoId, 10, 10));
        QVERIFY(!timeline->appendAssetRange(videoId, 10, 9));
        QVERIFY(!timeline->appendAssetRange(videoId, 0, static_cast<int>(length) + 1));
        QVERIFY(!timeline->appendAssetRange(QStringLiteral("missing"), 0, 5));
        QCOMPARE(m_app->project()->project(), changed);
        QCOMPARE(m_app->project()->undoText(), previousUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
    }

    void placeSourceRangeAtPlayheadNeverOverwritesAndIsUndoable()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        const QString stillId =
            m_app->media()->model()->get(2).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        QVERIFY(!stillId.isEmpty());

        const Project before = m_app->project()->project();
        const FrameCount previousPlayhead = playback->positionFrames();
        // A fresh lane guarantees an available destination even if earlier video
        // tracks have footage at frame zero. Only the new clip is undoable.
        QVERIFY(timeline->addTrack(QStringLiteral("video")));
        const Project withTrack = m_app->project()->project();
        playback->goToStart();
        const QStringList previousSelection = timeline->selection();
        const QString oldUndo = m_app->project()->undoText();

        // All invalid placements must preserve the document, selection and undo history.
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(videoId, -1, 12));
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(videoId, 12, 12));
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(videoId, 20, 10));
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(QStringLiteral("missing"), 0, 12));
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(stillId, 0, 12));
        QCOMPARE(m_app->project()->project(), withTrack);
        QCOMPARE(timeline->selection(), previousSelection);
        QCOMPARE(m_app->project()->undoText(), oldUndo);

        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QVERIFY(source);
        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(source->setProperty("markInFrame", 13));
        QVERIFY(source->setProperty("markOutFrame", 28));
        const FrameCount position = playback->positionFrames();
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* placeButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorPlace"));
        QVERIFY(tabs);
        QVERIFY(placeButton);
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QTRY_VERIFY(placeButton->isVisible());
        QVERIFY(placeButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(placeButton));
        const Project placed = m_app->project()->project();
        QCOMPARE(placed.timeline().clipCount(), withTrack.timeline().clipCount() + 1);
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        const Id createdId = timeline->selection().first();
        const Clip* created = placed.timeline().clip(createdId);
        QVERIFY(created);
        QCOMPARE(created->assetId, videoId);
        QCOMPARE(created->start, position);
        QCOMPARE(created->sourceIn, FrameCount{13});
        QCOMPARE(created->duration, FrameCount{15});
        QCOMPARE(playback->positionFrames(), position);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Clip"));

        Id expectedTrack;
        for (const Track& track : withTrack.timeline().tracks()) {
            if (track.kind == TrackKind::Video && !track.locked &&
                withTrack.timeline().isRangeFree(track.id, position, 15, {})) {
                expectedTrack = track.id;
                break;
            }
        }
        QVERIFY(!expectedTrack.isEmpty());
        QCOMPARE(placed.timeline().trackOfClip(createdId)->id, expectedTrack);

        // Occupy every other compatible free lane. The next placement must reject
        // the collision without modifying a clip, selection or undo history.
        int additionalPlacements = 0;
        for (const Track& track : placed.timeline().tracks()) {
            if (track.kind == TrackKind::Video && !track.locked &&
                placed.timeline().isRangeFree(track.id, position, 15, {})) {
                QVERIFY(timeline->placeAssetRangeAtPlayhead(videoId, 13, 28));
                ++additionalPlacements;
            }
        }
        const Project full = m_app->project()->project();
        const QStringList fullSelection = timeline->selection();
        const QString fullUndo = m_app->project()->undoText();
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(videoId, 13, 28));
        QCOMPARE(m_app->project()->project(), full);
        QCOMPARE(timeline->selection(), fullSelection);
        QCOMPARE(m_app->project()->undoText(), fullUndo);
        for (int i = 0; i < additionalPlacements; ++i) {
            m_app->project()->undo();
        }
        QCOMPARE(m_app->project()->project(), placed);

        // The clip is added without trimming, moving or overwriting existing content.
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), placed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);

        // Locking every track must reject placement without touching the project.
        QVERIFY(timeline->setAllTracksLocked(true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        const QStringList lockedSelection = timeline->selection();
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(videoId, 0, 15));
        QVERIFY(!timeline->placeAssetRangeAtPlayhead(audioId, 0, 15));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(timeline->selection(), lockedSelection);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);

        // Roll back the temporary extra track and source-monitor state.
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(!placeButton->isEnabled());
        QVERIFY(tabs->setProperty("currentIndex", 0));
        playback->seek(static_cast<double>(previousPlayhead));
    }

    void sourceMonitorCanTargetExactAudioTrackWithoutSilentFallback()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!audioId.isEmpty());

        const Project before = m_app->project()->project();
        const FrameCount oldPosition = playback->positionFrames();
        QVERIFY(timeline->addTrack(QStringLiteral("audio")));
        const Project withTrack = m_app->project()->project();
        Id targetTrackId;
        Id videoTrackId;
        for (const Track& track : withTrack.timeline().tracks()) {
            if (track.kind == TrackKind::Video && videoTrackId.isEmpty()) {
                videoTrackId = track.id;
            }
            if (track.kind == TrackKind::Audio && !before.timeline().track(track.id)) {
                targetTrackId = track.id;
            }
        }
        QVERIFY(!targetTrackId.isEmpty());
        QVERIFY(!videoTrackId.isEmpty());

        const QVariantList choices = timeline->sourceDestinationTracks(audioId);
        bool foundAudio = false;
        for (const QVariant& choice : choices) {
            const QVariantMap details = choice.toMap();
            QVERIFY(details.value(QStringLiteral("id")).toString() != videoTrackId);
            if (details.value(QStringLiteral("id")).toString() == targetTrackId) {
                foundAudio = true;
                QVERIFY(!details.value(QStringLiteral("locked")).toBool());
            }
        }
        QVERIFY(foundAudio);
        playback->goToStart();

        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* picker =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorTargetTrack"));
        QQuickItem* placeButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorPlace"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QVERIFY(source);
        QVERIFY(picker);
        QVERIFY(placeButton);
        QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(source->setProperty("markInFrame", 2));
        QVERIFY(source->setProperty("markOutFrame", 17));
        QVERIFY(source->setProperty("selectedTrackId", targetTrackId));
        QTRY_VERIFY(picker->property("currentIndex").toInt() > 0);
        QTRY_VERIFY(placeButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(placeButton));

        const Project placed = m_app->project()->project();
        QCOMPARE(placed.timeline().clipCount(), withTrack.timeline().clipCount() + 1);
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        const Clip* clip = placed.timeline().clip(timeline->selection().first());
        QVERIFY(clip);
        QCOMPARE(clip->assetId, audioId);
        QCOMPARE(clip->start, FrameCount{0});
        QCOMPARE(clip->sourceIn, FrameCount{2});
        QCOMPARE(clip->duration, FrameCount{15});
        QCOMPARE(placed.timeline().trackOfClip(clip->id)->id, targetTrackId);
        QCOMPARE(playback->positionFrames(), FrameCount{0});

        const QStringList selectionBefore = timeline->selection();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->placeAssetRangeOnTrackAtPlayhead(audioId, targetTrackId, 2, 17));
        QVERIFY(!timeline->placeAssetRangeOnTrackAtPlayhead(audioId, videoTrackId, 2, 17));
        QVERIFY(!timeline->placeAssetRangeOnTrackAtPlayhead(
            audioId, QStringLiteral("missing-track"), 2, 17));
        QCOMPARE(m_app->project()->project(), placed);
        QCOMPARE(timeline->selection(), selectionBefore);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), placed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);

        QVERIFY(timeline->setTrackFlag(targetTrackId, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        QTRY_VERIFY(!source->property("selectedTrackAvailable").toBool());
        QVERIFY(!placeButton->isEnabled());
        QVERIFY(!timeline->placeAssetRangeOnTrackAtPlayhead(audioId, targetTrackId, 2, 17));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);
        QTRY_VERIFY(source->property("selectedTrackAvailable").toBool());

        // Removing the selected target must not redirect placement to automatic mode.
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(source->property("selectedTrackId").toString(), targetTrackId);
        QTRY_VERIFY(!source->property("selectedTrackAvailable").toBool());
        QVERIFY(!placeButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        playback->seek(static_cast<double>(oldPosition));
    }

    void sourceAppendHonorsTargetTrackAndDoesNotSilentlyFallback()
    {
        auto* timeline = m_app->timeline();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        const Project before = m_app->project()->project();
        QVERIFY(timeline->addTrack(QStringLiteral("audio")));
        const Project withTrack = m_app->project()->project();
        Id targetAudio;
        Id targetVideo;
        for (const Track& track : withTrack.timeline().tracks()) {
            if (track.kind == TrackKind::Audio && !before.timeline().track(track.id)) {
                targetAudio = track.id;
            }
            if (track.kind == TrackKind::Video && targetVideo.isEmpty()) {
                targetVideo = track.id;
            }
        }
        QVERIFY(!targetAudio.isEmpty());
        QVERIFY(!targetVideo.isEmpty());
        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* picker =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorTargetTrack"));
        QQuickItem* append =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorAppend"));
        QVERIFY(source);
        QVERIFY(tabs);
        QVERIFY(picker);
        QVERIFY(append);
        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(source->setProperty("markInFrame", 6));
        QVERIFY(source->setProperty("markOutFrame", 18));
        QVERIFY(source->setProperty("selectedTrackId", targetAudio));
        QTRY_VERIFY(picker->property("currentIndex").toInt() > 0);
        QTRY_VERIFY(append->isVisible() && append->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(append));

        const Project appended = m_app->project()->project();
        QCOMPARE(appended.timeline().clipCount(), withTrack.timeline().clipCount() + 1);
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        const Clip* clip = appended.timeline().clip(timeline->selection().first());
        QVERIFY(clip);
        QCOMPARE(clip->assetId, audioId);
        QCOMPARE(clip->sourceIn, FrameCount{6});
        QCOMPARE(clip->duration, FrameCount{12});
        QCOMPARE(clip->start, FrameCount{0});
        QCOMPARE(appended.timeline().trackOfClip(clip->id)->id, targetAudio);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Add Clip"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), appended);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);

        // A video source with audio may be appended to an explicitly chosen audio lane.
        QVERIFY(timeline->appendAssetRangeOnTrack(videoId, targetAudio, 4, 15));
        const Project videoAudio = m_app->project()->project();
        const Clip* stream = videoAudio.timeline().clip(timeline->selection().first());
        QVERIFY(stream);
        QCOMPARE(stream->assetId, videoId);
        QCOMPARE(stream->sourceIn, FrameCount{4});
        QCOMPARE(stream->duration, FrameCount{11});
        QCOMPARE(videoAudio.timeline().trackOfClip(stream->id)->id, targetAudio);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);

        // An audio-only source cannot be routed to a video lane.
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->appendAssetRangeOnTrack(audioId, targetVideo, 0, 10));
        QVERIFY(
            !timeline->appendAssetRangeOnTrack(audioId, QStringLiteral("missing-track"), 0, 10));
        QVERIFY(!timeline->appendAssetRangeOnTrack(audioId, targetAudio, 10, 10));
        QCOMPARE(m_app->project()->project(), withTrack);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        // Locked or removed explicit destinations never fall back to another track.
        QVERIFY(timeline->setTrackFlag(targetAudio, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        QTRY_VERIFY(!source->property("selectedTrackAvailable").toBool());
        QVERIFY(!append->isEnabled());
        QVERIFY(!timeline->appendAssetRangeOnTrack(audioId, targetAudio, 6, 18));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTrack);
        QTRY_VERIFY(source->property("selectedTrackAvailable").toBool());

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(source->property("selectedTrackId").toString(), targetAudio);
        QTRY_VERIFY(!source->property("selectedTrackAvailable").toBool());
        QVERIFY(!append->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
    }

    void sourceMonitorPlacesSynchronizedPictureAndSoundWithOneUndo()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        if (!engine::MltRuntime::capabilities().volumeFilter) {
            QSKIP("Synchronized V+A placement needs the MLT volume filter.");
        }
        QVERIFY(
            m_app->media()->mediaProperties(videoId).value(QStringLiteral("hasVideo")).toBool());
        QVERIFY(
            m_app->media()->mediaProperties(videoId).value(QStringLiteral("hasAudio")).toBool());
        QVERIFY(
            !m_app->media()->mediaProperties(audioId).value(QStringLiteral("hasVideo")).toBool());

        const Project before = m_app->project()->project();
        const FrameCount previousPlayhead = playback->positionFrames();
        QVERIFY(timeline->addTrack(QStringLiteral("video")));
        QVERIFY(timeline->addTrack(QStringLiteral("audio")));
        const Project withTracks = m_app->project()->project();
        Id videoTarget;
        Id audioTarget;
        for (const Track& track : withTracks.timeline().tracks()) {
            if (before.timeline().track(track.id)) {
                continue;
            }
            if (track.kind == TrackKind::Video) {
                videoTarget = track.id;
            } else {
                audioTarget = track.id;
            }
        }
        QVERIFY(!videoTarget.isEmpty());
        QVERIFY(!audioTarget.isEmpty());
        playback->goToStart();

        QObject* monitor = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* avButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorPlaceVideoAudio"));
        QVERIFY(monitor);
        QVERIFY(tabs);
        QVERIFY(avButton);
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(monitor->property("hasSourceAudio").toBool());
        QVERIFY(monitor->setProperty("markInFrame", 13));
        QVERIFY(monitor->setProperty("markOutFrame", 28));
        QVERIFY(monitor->setProperty("selectedTrackId", videoTarget));
        QTRY_VERIFY(avButton->isVisible());
        QVERIFY(avButton->isEnabled());
        const FrameCount at = playback->positionFrames();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(avButton));

        const Project placed = m_app->project()->project();
        QCOMPARE(placed.timeline().clipCount(), withTracks.timeline().clipCount() + 2);
        QCOMPARE(timeline->selection().size(), qsizetype{2});
        QCOMPARE(playback->positionFrames(), at);
        QCOMPARE(m_app->project()->undoText(),
                 QStringLiteral("Place Synchronized Video and Audio"));
        bool foundVideo = false;
        bool foundAudio = false;
        for (const Id& id : timeline->selection()) {
            const Clip* clip = placed.timeline().clip(id);
            const Track* track = placed.timeline().trackOfClip(id);
            QVERIFY(clip);
            QVERIFY(track);
            QCOMPARE(clip->assetId, videoId);
            QCOMPARE(clip->start, at);
            QCOMPARE(clip->sourceIn, FrameCount{13});
            QCOMPARE(clip->duration, FrameCount{15});
            if (track->kind == TrackKind::Video) {
                foundVideo = true;
                QCOMPARE(track->id, videoTarget);
                QVERIFY(clip->properties.muted); // Audio must not play twice.
            } else {
                foundAudio = true;
                QVERIFY(!clip->properties.muted);
            }
        }
        QVERIFY(foundVideo && foundAudio);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTracks);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), placed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTracks);

        // The selected audio destination must be honored rather than swapped out.
        QVERIFY(monitor->setProperty("selectedTrackId", audioTarget));
        QTRY_VERIFY(avButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(monitor, "placeVideoAndAudio"));
        const Project audioPreferred = m_app->project()->project();
        QCOMPARE(audioPreferred.timeline().clipCount(), withTracks.timeline().clipCount() + 2);
        bool targetedSound = false;
        for (const Id& id : timeline->selection()) {
            const Track* track = audioPreferred.timeline().trackOfClip(id);
            QVERIFY(track);
            targetedSound |= track->id == audioTarget;
        }
        QVERIFY(targetedSound);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTracks);

        // An occupied exact video destination must not silently fall back.
        QVERIFY(timeline->placeAssetRangeOnTrackAtPlayhead(videoId, videoTarget, 13, 28));
        const Project occupied = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const QStringList selectionBefore = timeline->selection();
        QVERIFY(!timeline->placeSynchronizedAssetRangeAtPlayhead(videoId, videoTarget, 13, 28));
        QCOMPARE(m_app->project()->project(), occupied);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(timeline->selection(), selectionBefore);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTracks);

        // Bad source boundaries and a locked selected destination must be refused
        // without changing the project, clip selection or undo history.
        const QString validUndo = m_app->project()->undoText();
        QVERIFY(!timeline->placeSynchronizedAssetRangeAtPlayhead(videoId, videoTarget, -1, 17));
        QVERIFY(!timeline->placeSynchronizedAssetRangeAtPlayhead(videoId, videoTarget, 12, 12));
        QCOMPARE(m_app->project()->project(), withTracks);
        QCOMPARE(m_app->project()->undoText(), validUndo);
        QVERIFY(timeline->setTrackFlag(videoTarget, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        QVERIFY(!timeline->placeSynchronizedAssetRangeAtPlayhead(videoId, videoTarget, 13, 28));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withTracks);

        // Clearing or switching sources must not leave an enabled A/V action.
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(!monitor->property("hasSourceAudio").toBool() ||
                !monitor->property("hasPicture").toBool());
        QVERIFY(!avButton->isVisible());
        QVERIFY(!timeline->placeSynchronizedAssetRangeAtPlayhead(audioId, {}, 0, 15));
        QCOMPARE(m_app->project()->project(), withTracks);

        m_app->project()->undo();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        QVERIFY(QMetaObject::invokeMethod(monitor, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        playback->seek(static_cast<double>(previousPlayhead));
        timeline->clearSelection();
    }

    void sourceMonitorReplacesSelectedClipWithoutChangingTimelineCuts()
    {
        auto* timeline = m_app->timeline();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        const Project before = m_app->project()->project();

        QVERIFY(timeline->appendAssetRange(videoId, 5, 20));
        const Project inserted = m_app->project()->project();
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        const Id clipId = timeline->selection().first();
        const Clip* original = inserted.timeline().clip(clipId);
        QVERIFY(original);
        QCOMPARE(original->duration, FrameCount{15});
        const Id targetTrack = inserted.timeline().trackOfClip(clipId)->id;

        QObject* monitor = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* replaceButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorReplaceSelected"));
        QVERIFY(monitor);
        QVERIFY(tabs);
        QVERIFY(replaceButton);
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(monitor->setProperty("markInFrame", 40));
        QVERIFY(monitor->setProperty("markOutFrame", 55));
        QTRY_VERIFY(replaceButton->isVisible() && replaceButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(replaceButton));

        const Project replaced = m_app->project()->project();
        QCOMPARE(replaced.timeline().clipCount(), inserted.timeline().clipCount());
        const Clip* changed = replaced.timeline().clip(clipId);
        QVERIFY(changed);
        QCOMPARE(changed->id, clipId);
        QCOMPARE(changed->assetId, videoId);
        QCOMPARE(changed->sourceIn, FrameCount{40});
        QCOMPARE(changed->start, original->start);
        QCOMPARE(changed->duration, original->duration);
        QCOMPARE(changed->properties, original->properties);
        QCOMPARE(replaced.timeline().trackOfClip(clipId)->id, targetTrack);
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        QCOMPARE(timeline->selection().first(), clipId);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Replace Clip Source"));

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), inserted);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), replaced);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), inserted);

        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(monitor->setProperty("markOutFrame", 54));
        QTRY_VERIFY(!replaceButton->isEnabled());
        QVERIFY(!timeline->replaceSelectionWithAssetRange(videoId, 40, 54));
        QCOMPARE(m_app->project()->project(), inserted);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QVERIFY(monitor->setProperty("markOutFrame", 55));

        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(monitor->setProperty("markInFrame", 4));
        QVERIFY(monitor->setProperty("markOutFrame", 19));
        QTRY_VERIFY(!replaceButton->isEnabled());
        QVERIFY(!timeline->replaceSelectionWithAssetRange(audioId, 4, 19));
        QCOMPARE(m_app->project()->project(), inserted);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(monitor->setProperty("markInFrame", 40));
        QVERIFY(monitor->setProperty("markOutFrame", 55));
        QVERIFY(timeline->setTrackFlag(targetTrack, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        QTRY_VERIFY(!replaceButton->isEnabled());
        QVERIFY(!timeline->replaceSelectionWithAssetRange(videoId, 40, 55));
        QCOMPARE(m_app->project()->project(), locked);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), inserted);

        QVERIFY(QMetaObject::invokeMethod(monitor, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void sourceMonitorRippleInsertShiftsVideoAndAudioWithOneUndo()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        const Project initial = m_app->project()->project();
        const FrameCount oldPlayhead = playback->positionFrames();
        const FrameCount cut = initial.timeline().duration() + 20;
        const Id videoTrack = track(0).id;
        const Id audioTrack = track(2).id;

        Clip video;
        video.id = createId();
        video.assetId = videoId;
        video.start = cut;
        video.sourceIn = 5;
        video.duration = 30;
        Clip audio;
        audio.id = createId();
        audio.assetId = audioId;
        audio.start = cut + 10;
        audio.sourceIn = 2;
        audio.duration = 20;
        QVERIFY(m_app->project()->execute(edits::addClip(videoTrack, video)));
        QVERIFY(m_app->project()->execute(edits::addClip(audioTrack, audio)));
        const Id markerId = createId();
        const Id subtitleId = createId();
        const auto cutMs = subtitleMillisecondsAtFrame(cut, initial.settings().frameRate);
        QVERIFY(cutMs);
        QVERIFY(m_app->project()->execute(
            edits::addMarker({markerId, cut, QStringLiteral("At source cut")})));
        QVERIFY(m_app->project()->execute(edits::addSubtitle(
            {subtitleId, *cutMs + 100, *cutMs + 400, QStringLiteral("After source cut")})));
        const Project prepared = m_app->project()->project();

        playback->seek(static_cast<double>(cut));
        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* insertButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorRippleInsert"));
        QVERIFY(source);
        QVERIFY(tabs);
        QVERIFY(insertButton);
        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(source->setProperty("selectedTrackId", videoTrack));
        QVERIFY(source->setProperty("markInFrame", 40));
        QVERIFY(source->setProperty("markOutFrame", 55));
        QTRY_VERIFY(insertButton->isVisible() && insertButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(insertButton));
        const Project inserted = m_app->project()->project();
        QCOMPARE(inserted.timeline().clipCount(), prepared.timeline().clipCount() + 1);
        QCOMPARE(inserted.timeline().clip(video.id)->start, cut + 15);
        QCOMPARE(inserted.timeline().clip(audio.id)->start, cut + 25);
        const auto marker =
            std::find_if(inserted.markers().begin(), inserted.markers().end(),
                         [&](const TimelineMarker& value) { return value.id == markerId; });
        QVERIFY(marker != inserted.markers().end());
        QCOMPARE(marker->frame, cut + 15);
        const auto subtitle =
            std::find_if(inserted.subtitles().cues.begin(), inserted.subtitles().cues.end(),
                         [&](const SubtitleCue& value) { return value.id == subtitleId; });
        QVERIFY(subtitle != inserted.subtitles().cues.end());
        QVERIFY(subtitle->startMs > *cutMs + 100);
        QCOMPARE(inserted.timeline().clip(audio.id)->sourceIn, audio.sourceIn);
        QCOMPARE(inserted.timeline().clip(video.id)->sourceIn, video.sourceIn);
        QCOMPARE(timeline->selection().size(), qsizetype{1});
        const Id placedId = timeline->selection().first();
        const Clip* placed = inserted.timeline().clip(placedId);
        QVERIFY(placed);
        QCOMPARE(placed->assetId, videoId);
        QCOMPARE(placed->start, cut);
        QCOMPARE(placed->sourceIn, FrameCount{40});
        QCOMPARE(placed->duration, FrameCount{15});
        QCOMPARE(inserted.timeline().trackOfClip(placedId)->id, videoTrack);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Ripple Insert Source Range"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), inserted);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);

        // A locked later lane blocks the whole cross-track insertion, never
        // allowing only the picture lane to move.
        QVERIFY(timeline->setTrackFlag(audioTrack, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        QVERIFY(!timeline->rippleInsertAssetRangeAtPlayhead(videoId, videoTrack, 40, 55));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);

        // A clip spanning the edit on a third lane is never silently split.
        Clip spanning;
        spanning.id = createId();
        spanning.assetId = videoId;
        spanning.start = cut - 5;
        spanning.sourceIn = 15;
        spanning.duration = 10;
        QVERIFY(m_app->project()->execute(edits::addClip(track(1).id, spanning)));
        const Project crossed = m_app->project()->project();
        const QString crossedUndo = m_app->project()->undoText();
        QVERIFY(!timeline->rippleInsertAssetRangeAtPlayhead(videoId, videoTrack, 40, 55));
        QCOMPARE(m_app->project()->project(), crossed);
        QCOMPARE(m_app->project()->undoText(), crossedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);

        // The selected destination never falls back when it has been removed.
        QVERIFY(source->setProperty("selectedTrackId", QStringLiteral("unavailable-track")));
        QVERIFY(!timeline->rippleInsertAssetRangeAtPlayhead(
            videoId, QStringLiteral("unavailable-track"), 40, 55));
        QCOMPARE(m_app->project()->project(), prepared);

        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        m_app->project()->undo(); // subtitle
        m_app->project()->undo(); // marker
        m_app->project()->undo(); // audio clip
        m_app->project()->undo(); // video clip
        QCOMPARE(m_app->project()->project(), initial);
        timeline->clearSelection();
        playback->seek(static_cast<double>(oldPlayhead));
    }

    void sourceMonitorRippleVideoAudioKeepsSequenceAndUndoAtomic()
    {
        if (!engine::MltRuntime::capabilities().volumeFilter) {
            QSKIP("Synchronized ripple needs the MLT volume filter.");
        }
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        const Project initial = m_app->project()->project();
        const FrameCount oldPlayhead = playback->positionFrames();
        const FrameCount cut = initial.timeline().duration() + 20;
        const Id videoTrack = track(0).id;
        const Id audioTrack = track(2).id;
        Clip laterVideo;
        laterVideo.id = createId();
        laterVideo.assetId = videoId;
        laterVideo.start = cut;
        laterVideo.sourceIn = 5;
        laterVideo.duration = 20;
        Clip laterAudio = laterVideo;
        laterAudio.id = createId();
        QVERIFY(m_app->project()->execute(edits::addClip(videoTrack, laterVideo)));
        QVERIFY(m_app->project()->execute(edits::addClip(audioTrack, laterAudio)));
        const Id markerId = createId();
        const Id subtitleId = createId();
        const auto cutMs = subtitleMillisecondsAtFrame(cut, initial.settings().frameRate);
        QVERIFY(cutMs);
        QVERIFY(m_app->project()->execute(
            edits::addMarker({markerId, cut, QStringLiteral("Ripple V+A cut")})));
        QVERIFY(m_app->project()->execute(edits::addSubtitle(
            {subtitleId, *cutMs + 100, *cutMs + 400, QStringLiteral("After paired cut")})));
        const Project prepared = m_app->project()->project();

        playback->seek(static_cast<double>(cut));
        QObject* source = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* action =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorRippleVideoAudio"));
        QVERIFY(source && tabs && action);
        QVERIFY(QMetaObject::invokeMethod(source, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(source->setProperty("selectedTrackId", videoTrack));
        QVERIFY(source->setProperty("markInFrame", 40));
        QVERIFY(source->setProperty("markOutFrame", 55));
        QTRY_VERIFY(action->isVisible() && action->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(source, "rippleInsertVideoAndAudio"));

        const Project inserted = m_app->project()->project();
        QCOMPARE(inserted.timeline().clipCount(), prepared.timeline().clipCount() + 2);
        QCOMPARE(inserted.timeline().clip(laterVideo.id)->start, cut + 15);
        QCOMPARE(inserted.timeline().clip(laterAudio.id)->start, cut + 15);
        QCOMPARE(inserted.markers().front().id, markerId);
        QCOMPARE(inserted.markers().front().frame, cut + 15);
        QCOMPARE(inserted.subtitles().cues.front().id, subtitleId);
        QVERIFY(inserted.subtitles().cues.front().startMs > *cutMs + 100);
        QCOMPARE(timeline->selection().size(), qsizetype{2});
        bool foundPicture = false;
        bool foundSound = false;
        for (const Id& selectedId : timeline->selection()) {
            const Clip* clip = inserted.timeline().clip(selectedId);
            const Track* lane = inserted.timeline().trackOfClip(selectedId);
            QVERIFY(clip && lane);
            QCOMPARE(clip->assetId, videoId);
            QCOMPARE(clip->start, cut);
            QCOMPARE(clip->sourceIn, FrameCount{40});
            QCOMPARE(clip->duration, FrameCount{15});
            if (lane->kind == TrackKind::Video) {
                foundPicture = true;
                QCOMPARE(lane->id, videoTrack);
                QVERIFY(clip->properties.muted);
            } else {
                foundSound = true;
                QCOMPARE(lane->id, audioTrack);
                QVERIFY(!clip->properties.muted);
            }
        }
        QVERIFY(foundPicture && foundSound);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Ripple Insert Picture and Sound"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), inserted);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);

        QVERIFY(timeline->setTrackFlag(audioTrack, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        QVERIFY(
            !timeline->rippleInsertSynchronizedAssetRangeAtPlayhead(videoId, videoTrack, 40, 55));
        QCOMPARE(m_app->project()->project(), locked);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);

        QVERIFY(QMetaObject::invokeMethod(source, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        m_app->project()->undo(); // subtitle
        m_app->project()->undo(); // marker
        m_app->project()->undo(); // audio clip
        m_app->project()->undo(); // video clip
        QCOMPARE(m_app->project()->project(), initial);
        timeline->clearSelection();
        playback->seek(static_cast<double>(oldPlayhead));
    }

    void sourceMonitorMatchesSelectedClipLengthBeforeReplacing()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const QString videoId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!videoId.isEmpty());
        QVERIFY(!audioId.isEmpty());
        const Project before = m_app->project()->project();
        const FrameCount programBefore = playback->positionFrames();
        QVERIFY(timeline->appendAssetRange(videoId, 5, 20));
        const Project withClip = m_app->project()->project();
        const Id clipId = timeline->selection().first();
        const Clip* original = withClip.timeline().clip(clipId);
        QVERIFY(original);
        QCOMPARE(original->duration, FrameCount{15});
        const Id trackId = withClip.timeline().trackOfClip(clipId)->id;

        QObject* monitor = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* matchButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorMatchClipLength"));
        QQuickItem* replaceButton =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorReplaceSelected"));
        QVERIFY(monitor);
        QVERIFY(tabs);
        QVERIFY(matchButton);
        QVERIFY(replaceButton);
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(videoId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        const int maxFrames = monitor->property("maxFrames").toInt();
        QVERIFY(maxFrames > 60);
        QVERIFY(monitor->setProperty("previewFrame", 7));
        QVERIFY(monitor->setProperty("markInFrame", 30));
        QVERIFY(monitor->setProperty("markOutFrame", maxFrames));
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();
        QTRY_VERIFY(matchButton->isVisible() && matchButton->isEnabled());
        QVERIFY(!replaceButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(matchButton));
        QTRY_COMPARE(monitor->property("markOutFrame").toInt(), 45);
        QCOMPARE(monitor->property("markInFrame").toInt(), 30);
        QCOMPARE(monitor->property("previewFrame").toInt(), 7);
        QCOMPARE(playback->positionFrames(), programBefore);
        QCOMPARE(m_app->project()->project(), withClip);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);
        QTRY_VERIFY(!matchButton->isEnabled());
        QTRY_VERIFY(replaceButton->isEnabled());

        // Too few remaining source frames: never move a deliberately chosen In.
        QVERIFY(monitor->setProperty("markInFrame", maxFrames - 14));
        QTRY_VERIFY(!matchButton->isEnabled());
        const int outBefore = monitor->property("markOutFrame").toInt();
        QVERIFY(QMetaObject::invokeMethod(monitor, "matchSelectedClipLength"));
        QCOMPARE(monitor->property("markInFrame").toInt(), maxFrames - 14);
        QCOMPARE(monitor->property("markOutFrame").toInt(), outBefore);
        QCOMPARE(m_app->project()->project(), withClip);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        // Restore a feasible In but an unmatched Out. The shortcut is scoped to
        // the Source tab and cannot accidentally modify marks in Program mode.
        QVERIFY(monitor->setProperty("markInFrame", 30));
        QVERIFY(monitor->setProperty("markOutFrame", maxFrames));
        QTRY_VERIFY(matchButton->isEnabled());
        QVERIFY(tabs->setProperty("currentIndex", 0));
        QTest::keyClick(m_window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(monitor->property("markOutFrame").toInt(), maxFrames);
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QTest::keyClick(m_window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(monitor->property("markOutFrame").toInt(), 45);
        QTRY_VERIFY(replaceButton->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(replaceButton));
        const Project replaced = m_app->project()->project();
        const Clip* swapped = replaced.timeline().clip(clipId);
        QVERIFY(swapped);
        QCOMPARE(swapped->sourceIn, FrameCount{30});
        QCOMPARE(swapped->duration, original->duration);
        QCOMPARE(swapped->start, original->start);
        QCOMPARE(swapped->id, original->id);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Replace Clip Source"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withClip);

        // Audio-only media is not compatible with the selected video clip.
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(monitor->setProperty("markInFrame", 3));
        QTRY_VERIFY(!matchButton->isEnabled());
        QTRY_VERIFY(!replaceButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(videoId))));

        // A locked destination prevents mark-matching just as it prevents replacement.
        QVERIFY(timeline->setTrackFlag(trackId, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        QVERIFY(monitor->setProperty("markInFrame", 40));
        QTRY_VERIFY(!matchButton->isEnabled());
        QCOMPARE(m_app->project()->project(), locked);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withClip);

        timeline->clearSelection();
        QTRY_VERIFY(!matchButton->isVisible());
        QVERIFY(QMetaObject::invokeMethod(monitor, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(playback->positionFrames(), programBefore);
    }

    void sourceMonitorMatchesAudioOnlyClipLengthWithoutTouchingProject()
    {
        auto* timeline = m_app->timeline();
        const QString audioId =
            m_app->media()->model()->get(1).value(QStringLiteral("assetId")).toString();
        QVERIFY(!audioId.isEmpty());
        const Project before = m_app->project()->project();
        QVERIFY(timeline->appendAssetRange(audioId, 3, 17));
        const Project withClip = m_app->project()->project();
        const Id id = timeline->selection().first();
        const Clip* original = withClip.timeline().clip(id);
        QVERIFY(original);
        QCOMPARE(original->duration, FrameCount{14});
        QCOMPARE(withClip.timeline().trackOfClip(id)->kind, TrackKind::Audio);

        QObject* monitor = m_window->findChild<QObject*>(QStringLiteral("sourceMonitorPanel"));
        QQuickItem* tabs = findItem(m_window->contentItem(), QStringLiteral("monitorTabs"));
        QQuickItem* match =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorMatchClipLength"));
        QQuickItem* replace =
            findItem(m_window->contentItem(), QStringLiteral("sourceMonitorReplaceSelected"));
        QVERIFY(monitor);
        QVERIFY(tabs);
        QVERIFY(match);
        QVERIFY(replace);
        QVERIFY(QMetaObject::invokeMethod(monitor, "openFor", Q_ARG(QVariant, QVariant(audioId))));
        QVERIFY(tabs->setProperty("currentIndex", 1));
        QVERIFY(!monitor->property("hasPicture").toBool());
        QVERIFY(monitor->property("hasSourceAudio").toBool());
        QVERIFY(monitor->setProperty("markInFrame", 40));
        const int maxFrames = monitor->property("maxFrames").toInt();
        QVERIFY(maxFrames > 54);
        QVERIFY(monitor->setProperty("markOutFrame", maxFrames));
        const QString oldUndo = m_app->project()->undoText();
        const bool wasModified = m_app->project()->isModified();
        QTRY_VERIFY(match->isVisible() && match->isEnabled());
        QVERIFY(!replace->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(match));
        QTRY_COMPARE(monitor->property("markOutFrame").toInt(), 54);
        QCOMPARE(monitor->property("markInFrame").toInt(), 40);
        QCOMPARE(m_app->project()->project(), withClip);
        QCOMPARE(m_app->project()->undoText(), oldUndo);
        QCOMPARE(m_app->project()->isModified(), wasModified);
        QTRY_VERIFY(replace->isEnabled());
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(replace));
        const Project replaced = m_app->project()->project();
        const Clip* changed = replaced.timeline().clip(id);
        QVERIFY(changed);
        QCOMPARE(changed->sourceIn, FrameCount{40});
        QCOMPARE(changed->duration, original->duration);
        QCOMPARE(changed->start, original->start);
        QCOMPARE(changed->assetId, audioId);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Replace Clip Source"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withClip);

        QVERIFY(QMetaObject::invokeMethod(monitor, "clear"));
        QVERIFY(tabs->setProperty("currentIndex", 0));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void duplicateSelectionAtPlayheadRetainsGroupTimingAndReSelectsCopies()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip video = track(0).clips.back();
        const Clip audio = track(2).clips.front();
        const FrameCount earliest = std::min(video.start, audio.start);
        const Id videoTrack = track(0).id;
        const Id audioTrack = track(2).id;
        timeline->clearSelection();
        QVERIFY(!timeline->duplicateSelectionAtPlayhead());
        timeline->select(video.id, false);
        timeline->select(audio.id, true);

        // A copy onto the originals must fail without changing selection or history.
        m_app->playback()->seek(static_cast<double>(earliest));
        const QStringList selectionBefore = timeline->selection();
        const QString priorUndo = m_app->project()->undoText();
        QVERIFY(!timeline->duplicateSelectionAtPlayhead());
        QCOMPARE(timeline->selection(), selectionBefore);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), priorUndo);

        // Insert enough empty space at frame zero to hold the selected group,
        // keeping the original clips and offsets intact on their own tracks.
        m_app->playback()->goToStart();
        const int gap = static_cast<int>(before.timeline().duration() + 10);
        QVERIFY(timeline->insertGapAtPlayhead(gap));
        const Project gapped = m_app->project()->project();
        QCOMPARE(gapped.timeline().clip(video.id)->start, video.start + gap);
        QCOMPARE(gapped.timeline().clip(audio.id)->start, audio.start + gap);
        QVERIFY(timeline->duplicateSelectionAtPlayhead());
        const Project duplicated = m_app->project()->project();
        QCOMPARE(duplicated.timeline().clipCount(), gapped.timeline().clipCount() + 2);
        QCOMPARE(timeline->selection().size(), qsizetype{2});
        QCOMPARE(m_app->project()->undoText(),
                 QStringLiteral("Duplicate Selected Clips at Playhead"));

        bool foundVideo = false;
        bool foundAudio = false;
        for (const Id& id : timeline->selection()) {
            QVERIFY(id != video.id && id != audio.id);
            const Clip* copy = duplicated.timeline().clip(id);
            QVERIFY(copy);
            const Id trackId = duplicated.timeline().trackOfClip(id)->id;
            if (trackId == videoTrack) {
                foundVideo = true;
                QCOMPARE(copy->start, video.start - earliest);
                QCOMPARE(copy->duration, video.duration);
                QCOMPARE(copy->sourceIn, video.sourceIn);
                QCOMPARE(copy->properties, video.properties);
            } else if (trackId == audioTrack) {
                foundAudio = true;
                QCOMPARE(copy->start, audio.start - earliest);
                QCOMPARE(copy->duration, audio.duration);
                QCOMPARE(copy->sourceIn, audio.sourceIn);
                QCOMPARE(copy->properties, audio.properties);
            } else {
                QFAIL("A duplicate was placed on an unexpected track");
            }
        }
        QVERIFY(foundVideo && foundAudio);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), gapped);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), duplicated);
        m_app->project()->undo();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void alignWholeSelectionToPlayheadKeepsRelativeTimingAndUndo()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        // Choose the last picture clip: moving the first would collide with
        // the still image already appended immediately after it.
        const Clip video = track(0).clips.back();
        const Clip audio = track(2).clips.front();
        const FrameCount first = std::min(video.start, audio.start);
        timeline->clearSelection();
        QVERIFY(!timeline->alignSelectionStartToPlayhead());
        timeline->select(video.id, false);
        timeline->select(audio.id, true);
        m_app->playback()->seek(static_cast<double>(first + 5));
        QVERIFY(timeline->alignSelectionStartToPlayhead());
        const Project moved = m_app->project()->project();
        const Clip* videoAfter = moved.timeline().clip(video.id);
        const Clip* audioAfter = moved.timeline().clip(audio.id);
        QVERIFY(videoAfter);
        QVERIFY(audioAfter);
        QCOMPARE(videoAfter->start - video.start, FrameCount{5});
        QCOMPARE(audioAfter->start - audio.start, FrameCount{5});
        QCOMPARE(videoAfter->sourceIn, video.sourceIn);
        QCOMPARE(audioAfter->sourceIn, audio.sourceIn);
        QCOMPARE(videoAfter->duration, video.duration);
        QCOMPARE(audioAfter->duration, audio.duration);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Nudge Clips Right"));

        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->alignSelectionStartToPlayhead());
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), moved);
        m_app->project()->undo();
        timeline->clearSelection();
    }

    void centerSelectedRangeOnPlayheadPreservesGroupAndRejectsUnsafeMoves()
    {
        auto* timeline = m_app->timeline();
        const Project original = m_app->project()->project();
        timeline->clearSelection();
        QVERIFY(!timeline->alignSelectionCenterToPlayhead());

        // Shift every clip away from zero so a one-frame group move is safe.
        m_app->playback()->goToStart();
        QVERIFY(timeline->insertGapAtPlayhead(10));
        const Project gapped = m_app->project()->project();
        timeline->selectAll();
        const QStringList selected = timeline->selection();
        QVERIFY(!selected.isEmpty());
        const QVariantMap bounds = timeline->selectedFrameRange();
        const FrameCount start =
            static_cast<FrameCount>(bounds.value(QStringLiteral("start")).toDouble());
        const FrameCount end =
            static_cast<FrameCount>(bounds.value(QStringLiteral("end")).toDouble());
        QVERIFY(end > start);
        QVERIFY(start >= 10);
        const FrameCount midpoint = start + (end - start) / 2;
        QVERIFY(midpoint >= 1);
        m_app->playback()->seek(static_cast<double>(midpoint - 1));
        QVERIFY(timeline->alignSelectionCenterToPlayhead());

        const Project centered = m_app->project()->project();
        for (const Id& id : selected) {
            const Clip* before = gapped.timeline().clip(id);
            const Clip* after = centered.timeline().clip(id);
            QVERIFY(before);
            QVERIFY(after);
            QCOMPARE(after->start, before->start - FrameCount{1});
            QCOMPARE(after->sourceIn, before->sourceIn);
            QCOMPARE(after->duration, before->duration);
            QCOMPARE(after->properties, before->properties);
        }
        QCOMPARE(timeline->selection(), selected);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Nudge Clips Left"));
        const QVariantMap newBounds = timeline->selectedFrameRange();
        const FrameCount newStart =
            static_cast<FrameCount>(newBounds.value(QStringLiteral("start")).toDouble());
        const FrameCount newEnd =
            static_cast<FrameCount>(newBounds.value(QStringLiteral("end")).toDouble());
        QCOMPARE(newStart + (newEnd - newStart) / 2, midpoint - FrameCount{1});
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->alignSelectionCenterToPlayhead());
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        m_app->playback()->goToStart();
        QVERIFY(!timeline->alignSelectionCenterToPlayhead());
        QCOMPARE(m_app->project()->project(), centered);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), gapped);
        const Id firstVideoTrackId = gapped.timeline().tracks().front().id;
        QVERIFY(timeline->setTrackFlag(firstVideoTrackId, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        m_app->playback()->seek(static_cast<double>(midpoint - 1));
        QVERIFY(!timeline->alignSelectionCenterToPlayhead());
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), gapped);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), original);
        timeline->clearSelection();
    }

    void alignWholeSelectionEndToPlayheadPreservesClipTimingAndHistory()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        timeline->clearSelection();
        QVERIFY(!timeline->alignSelectionEndToPlayhead());

        // Create headroom to allow a one-frame left shift of the complete
        // selection, including any clips that originally began at frame zero.
        m_app->playback()->goToStart();
        QVERIFY(timeline->insertGapAtPlayhead(10));
        const Project gapped = m_app->project()->project();
        timeline->selectAll();
        const QStringList selectionBefore = timeline->selection();
        QVERIFY(!selectionBefore.isEmpty());
        const QVariantMap range = timeline->selectedFrameRange();
        QVERIFY(!range.isEmpty());
        const FrameCount start =
            static_cast<FrameCount>(range.value(QStringLiteral("start")).toDouble());
        const FrameCount end =
            static_cast<FrameCount>(range.value(QStringLiteral("end")).toDouble());
        QVERIFY(start >= 10);
        m_app->playback()->goToEnd();
        const FrameCount cursor = m_app->playback()->positionFrames();
        QCOMPARE(cursor, end - FrameCount{1});
        QVERIFY(timeline->alignSelectionEndToPlayhead());
        const Project moved = m_app->project()->project();
        for (const Id& id : selectionBefore) {
            const Clip* original = gapped.timeline().clip(id);
            const Clip* shifted = moved.timeline().clip(id);
            QVERIFY(original);
            QVERIFY(shifted);
            QCOMPARE(shifted->start, original->start - FrameCount{1});
            QCOMPARE(shifted->sourceIn, original->sourceIn);
            QCOMPARE(shifted->duration, original->duration);
            QCOMPARE(shifted->properties, original->properties);
        }
        QCOMPARE(timeline->selection(), selectionBefore);
        QCOMPARE(static_cast<FrameCount>(
                     timeline->selectedFrameRange().value(QStringLiteral("end")).toDouble()),
                 cursor);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Nudge Clips Left"));
        const QString undoBefore = m_app->project()->undoText();

        m_app->playback()->goToStart();
        QVERIFY(!timeline->alignSelectionEndToPlayhead());
        QCOMPARE(m_app->project()->project(), moved);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), gapped);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void trimSelectedClipToPlayheadKeepsUndoAndSourceTrims()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const Clip clip = track(0).clips.back();
        QVERIFY(clip.duration > 25);
        timeline->select(clip.id, false);

        m_app->playback()->seek(static_cast<double>(clip.start + 10));
        QVERIFY(timeline->trimSelectionToPlayhead(false));
        const Clip* trimmed = m_app->project()->project().timeline().clip(clip.id);
        QVERIFY(trimmed);
        QCOMPARE(trimmed->start, clip.start + FrameCount{10});
        QCOMPARE(trimmed->sourceIn, clip.sourceIn + FrameCount{10});
        QCOMPARE(trimmed->duration, clip.duration - FrameCount{10});
        QCOMPARE(trimmed->properties, clip.properties);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);

        m_app->playback()->seek(static_cast<double>(clip.end() - 10));
        QVERIFY(timeline->trimSelectionToPlayhead(true));
        trimmed = m_app->project()->project().timeline().clip(clip.id);
        QVERIFY(trimmed);
        QCOMPARE(trimmed->start, clip.start);
        QCOMPARE(trimmed->sourceIn, clip.sourceIn);
        QCOMPARE(trimmed->duration, clip.duration - FrameCount{10});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);

        const QString undoBefore = m_app->project()->undoText();
        m_app->playback()->seek(static_cast<double>(clip.start));
        QVERIFY(!timeline->trimSelectionToPlayhead(false));
        QVERIFY(!timeline->trimSelectionToPlayhead(true));
        timeline->clearSelection();
        QVERIFY(!timeline->trimSelectionToPlayhead(true));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        // A locked track rejects trimming without recording or applying changes.
        const Id trackId = track(0).id;
        timeline->select(clip.id, false);
        QVERIFY(timeline->setTrackFlag(trackId, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        m_app->playback()->seek(static_cast<double>(clip.start + 10));
        QVERIFY(!timeline->trimSelectionToPlayhead(false));
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void selectClipsBySelectedTimeSpanKeepsProjectAndHistoryUntouched()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString assetId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!assetId.isEmpty());
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip first = track(0).clips.front();
        const Id audioTrackId = track(2).id;
        const Clip sound = track(2).clips.front();

        timeline->clearSelection();
        QVERIFY(!timeline->selectClipsInSelectionRange(true));
        QVERIFY(!timeline->selectClipsInSelectionRange(false));

        // Append three adjacent clips to ensure the one beginning exactly at the
        // selected range's exclusive end is excluded from both selection modes.
        QVERIFY(timeline->appendAsset(assetId));
        QVERIFY(timeline->appendAsset(assetId));
        QVERIFY(timeline->appendAsset(assetId));
        const auto& clips = track(0).clips;
        QVERIFY(clips.size() >= size_t{4});
        const Clip penultimate = clips.at(clips.size() - 2);
        const Clip outside = clips.back();
        QCOMPARE(outside.start, penultimate.end());
        const FrameCount rangeStart = first.start;
        const FrameCount rangeEnd = penultimate.end();
        QVERIFY(rangeEnd > rangeStart);
        QVERIFY(timeline->setTrackFlag(audioTrackId, QStringLiteral("locked"), true));
        const Project prepared = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();

        QStringList expectedInside;
        QStringList expectedOverlapping;
        for (const Track& track : prepared.timeline().tracks()) {
            for (const Clip& clip : track.clips) {
                if (clip.start >= rangeStart && clip.end() <= rangeEnd) {
                    expectedInside.append(clip.id);
                }
                if (clip.start < rangeEnd && clip.end() > rangeStart) {
                    expectedOverlapping.append(clip.id);
                }
            }
        }
        QVERIFY(expectedInside.size() > 2);
        QVERIFY(expectedOverlapping.size() > 2);
        QVERIFY(expectedOverlapping.contains(sound.id)); // read-only locked tracks included
        QVERIFY(!expectedInside.contains(outside.id));
        QVERIFY(!expectedOverlapping.contains(outside.id));
        expectedInside.sort();
        expectedOverlapping.sort();

        timeline->select(first.id, false);
        timeline->select(penultimate.id, true);
        QVERIFY(timeline->selectClipsInSelectionRange(false));
        QStringList result = timeline->selection();
        result.sort();
        QCOMPARE(result, expectedInside);
        QVERIFY(!timeline->selectClipsInSelectionRange(false));
        QCOMPARE(m_app->project()->project(), prepared);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        timeline->select(first.id, false);
        timeline->select(penultimate.id, true);
        QVERIFY(timeline->selectClipsInSelectionRange(true));
        result = timeline->selection();
        result.sort();
        QCOMPARE(result, expectedOverlapping);
        QVERIFY(!timeline->selectClipsInSelectionRange(true));
        QCOMPARE(m_app->project()->project(), prepared);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        timeline->clearSelection();
        m_app->project()->undo(); // unlock audio track
        m_app->project()->undo(); // remove third appended clip
        m_app->project()->undo(); // remove second appended clip
        m_app->project()->undo(); // remove first appended clip
        QCOMPARE(m_app->project()->project(), before);
    }

    void navigateIndividualSelectedClipEdgesWithoutChangingTimeline()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip picture = track(0).clips.back();
        const Clip sound = track(2).clips.front();

        timeline->clearSelection();
        m_app->playback()->goToStart();
        QVERIFY(!timeline->seekToSelectedClipEdge(true));
        QVERIFY(!timeline->seekToSelectedClipEdge(false));
        timeline->select(picture.id, false);
        timeline->select(sound.id, true);
        const QStringList selectionBefore = timeline->selection();

        QList<FrameCount> edges{picture.start, picture.end() - 1, sound.start, sound.end() - 1};
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        QVERIFY(edges.size() >= 2);
        m_app->playback()->seek(static_cast<double>(edges.front()));
        for (qsizetype i = 1; i < edges.size(); ++i) {
            QVERIFY(timeline->seekToSelectedClipEdge(true));
            QCOMPARE(m_app->playback()->positionFrames(), edges.at(i));
        }
        QVERIFY(!timeline->seekToSelectedClipEdge(true));
        QCOMPARE(m_app->playback()->positionFrames(), edges.back());
        for (qsizetype i = edges.size() - 1; i > 0; --i) {
            QVERIFY(timeline->seekToSelectedClipEdge(false));
            QCOMPARE(m_app->playback()->positionFrames(), edges.at(i - 1));
        }
        QVERIFY(!timeline->seekToSelectedClipEdge(false));
        QCOMPARE(m_app->playback()->positionFrames(), edges.front());
        QCOMPARE(timeline->selection(), selectionBefore);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        timeline->clearSelection();
    }

    void copySelectionTimecodesDocumentsExactFrameBoundsWithoutEditing()
    {
        auto* timeline = m_app->timeline();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Project before = m_app->project()->project();
        const QString undo = m_app->project()->undoText();
        const bool modified = m_app->project()->isModified();
        const Clip picture = track(0).clips.back();
        const Clip sound = track(2).clips.front();
        QClipboard* clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);

        timeline->clearSelection();
        clipboard->setText(QStringLiteral("unchanged on empty selection"));
        QVERIFY(!timeline->copySelectionTimecodes());
        QCOMPARE(clipboard->text(), QStringLiteral("unchanged on empty selection"));

        timeline->select(picture.id, false);
        timeline->select(sound.id, true);
        const QStringList originalSelection = timeline->selection();
        const FrameCount first = std::min(picture.start, sound.start);
        const FrameCount end = std::max(picture.end(), sound.end());
        const FrameRate& rate = before.settings().frameRate;
        QVERIFY(timeline->copySelectionTimecodes());
        const QString expected =
            QStringLiteral("Start: %1\nEnd (exclusive): %2\nLast visible: %3\n"
                           "Duration: %4 frames\nFrame rate: %5/%6 fps")
                .arg(timecode::format(first, rate), timecode::format(end, rate),
                     timecode::format(end - 1, rate))
                .arg(end - first)
                .arg(rate.numerator())
                .arg(rate.denominator());
        QCOMPARE(clipboard->text(), expected);
        QCOMPARE(timeline->selection(), originalSelection);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undo);
        QCOMPARE(m_app->project()->isModified(), modified);
        timeline->clearSelection();
    }

    void navigateToSelectedRangeEdgesWithoutEditing()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool wasModified = m_app->project()->isModified();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip picture = track(0).clips.back();
        const Clip sound = track(2).clips.front();

        timeline->clearSelection();
        m_app->playback()->seek(13);
        const FrameCount emptyPosition = m_app->playback()->positionFrames();
        QVERIFY(!timeline->seekToSelectionBoundary(false));
        QVERIFY(!timeline->seekToSelectionBoundary(true));
        QVERIFY(!timeline->seekToSelectionMidpoint());
        QCOMPARE(m_app->playback()->positionFrames(), emptyPosition);

        timeline->select(picture.id, false);
        timeline->select(sound.id, true);
        const QStringList originalSelection = timeline->selection();
        const FrameCount first = std::min(picture.start, sound.start);
        const FrameCount last = std::max(picture.end(), sound.end()) - 1;
        QVERIFY(timeline->seekToSelectionBoundary(false));
        QCOMPARE(m_app->playback()->positionFrames(), first);
        QVERIFY(timeline->seekToSelectionBoundary(true));
        QCOMPARE(m_app->playback()->positionFrames(), last);
        QVERIFY(timeline->seekToSelectionMidpoint());
        QCOMPARE(m_app->playback()->positionFrames(), first + (last - first) / 2);
        QCOMPARE(timeline->selection(), originalSelection);
        // Single-clip midpoint uses the last visible frame, not the exclusive end.
        timeline->clearSelection();
        timeline->select(picture.id, false);
        QVERIFY(timeline->seekToSelectionMidpoint());
        QCOMPARE(m_app->playback()->positionFrames(), picture.start + (picture.duration - 1) / 2);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), wasModified);
        timeline->clearSelection();
    }

    void customPictureAdjustmentDialogAcceptsExactPercentAndRejectsMixedSelection()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString previousUndo = m_app->project()->undoText();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip picture = track(0).clips.back();
        const Clip audio = track(2).clips.front();

        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("customPictureDialog"));
        QVERIFY(dialog);
        timeline->clearSelection();
        timeline->select(picture.id, false);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openEditor"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QQuickItem* type =
            findItem(m_window->contentItem(), QStringLiteral("customPictureProperty"));
        QQuickItem* percent =
            findItem(m_window->contentItem(), QStringLiteral("customPicturePercent"));
        QQuickItem* apply = findItem(m_window->contentItem(), QStringLiteral("customPictureApply"));
        QVERIFY(type);
        QVERIFY(percent);
        QVERIFY(apply);
        QVERIFY(type->setProperty("currentIndex", 2)); // saturation
        QCOMPARE(percent->property("from").toInt(), 0);
        QCOMPARE(percent->property("to").toInt(), 300);
        QVERIFY(percent->setProperty("value", 137));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(apply));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        const Project adjusted = m_app->project()->project();
        ClipProperties expected = picture.properties;
        expected.video.saturation = 1.37;
        QCOMPARE(adjusted.timeline().clip(picture.id)->properties, expected);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Set Selected Video Adjustment"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), adjusted);
        m_app->project()->undo();

        // Mixed video/audio selections are rejected by the underlying atomic edit.
        timeline->select(audio.id, true);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openEditor"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(type->setProperty("currentIndex", 0)); // brightness
        QCOMPARE(percent->property("from").toInt(), -100);
        QCOMPARE(percent->property("to").toInt(), 100);
        QVERIFY(percent->setProperty("value", -17));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(apply));
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), previousUndo);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        timeline->clearSelection();
    }

    void selectedPictureAdjustmentPreservesOtherClipAttributes()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip picture = track(0).clips.back();
        const Clip audio = track(2).clips.front();

        timeline->clearSelection();
        QVERIFY(!timeline->setSelectionVideoAdjustment(QStringLiteral("saturation"), 0.0));
        timeline->select(picture.id, false);
        timeline->select(audio.id, true);
        QVERIFY(!timeline->setSelectionVideoAdjustment(QStringLiteral("saturation"), 0.0));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        timeline->clearSelection();
        timeline->select(picture.id, false);
        QVERIFY(!timeline->setSelectionVideoAdjustment(QStringLiteral("gain"), 0.5));
        QVERIFY(!timeline->setSelectionVideoAdjustment(QStringLiteral("contrast"),
                                                       std::numeric_limits<double>::quiet_NaN()));
        const double target = picture.properties.video.saturation == 0.0 ? 1.25 : 0.0;
        QVERIFY(timeline->setSelectionVideoAdjustment(QStringLiteral("saturation"), target));
        const Project changed = m_app->project()->project();
        ClipProperties expected = picture.properties;
        expected.video.saturation = target;
        QCOMPARE(changed.timeline().clip(picture.id)->properties, expected);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Set Selected Video Adjustment"));
        QVERIFY(!timeline->setSelectionVideoAdjustment(QStringLiteral("saturation"), target));
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
        timeline->clearSelection();
    }

    void selectedVideoOpacityPreservesOtherClipProperties()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip picture = track(0).clips.back();
        const Clip sound = track(2).clips.front();
        const double target = picture.properties.video.opacity == 0.5 ? 0.25 : 0.5;

        timeline->clearSelection();
        QVERIFY(!timeline->setSelectionOpacity(target));
        timeline->select(picture.id, false);
        timeline->select(sound.id, true);
        QVERIFY(!timeline->setSelectionOpacity(target));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        timeline->clearSelection();
        timeline->select(picture.id, false);
        QVERIFY(!timeline->setSelectionOpacity(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!timeline->setSelectionOpacity(1.1));
        QVERIFY(timeline->setSelectionOpacity(target));
        const Project changed = m_app->project()->project();
        ClipProperties expected = picture.properties;
        expected.video.opacity = target;
        QCOMPARE(changed.timeline().clip(picture.id)->properties, expected);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Set Selected Video Opacity"));
        QVERIFY(!timeline->setSelectionOpacity(target));
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
    }

    void selectedAudioGainIsUndoableWithoutChangingOtherProperties()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!track(0).clips.empty());
        const Clip picture = track(0).clips.back();
        const double target = picture.properties.gain == 1.5 ? 0.5 : 1.5;

        timeline->clearSelection();
        QVERIFY(!timeline->setSelectionGain(target));
        timeline->select(picture.id, false);
        QVERIFY(!timeline->setSelectionGain(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!timeline->setSelectionGain(4.5));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QVERIFY(timeline->setSelectionGain(target));
        const Project changed = m_app->project()->project();
        ClipProperties expected = picture.properties;
        expected.gain = target;
        QCOMPARE(changed.timeline().clip(picture.id)->properties, expected);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Set Selected Audio Gain"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), changed);
        timeline->clearSelection();
    }

    void fadeSelectedClipFromEditMenuIsUndoable()
    {
        m_qmlWarnings.clear();
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(!track(0).clips.empty());
        const Clip picture = track(0).clips.back();
        QVERIFY(picture.duration > 7);

        timeline->clearSelection();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->setSelectionFade(false, 7));
        QCOMPARE(m_app->project()->undoText(), undoBefore);

        timeline->select(picture.id, false);
        QVERIFY(timeline->setSelectionFade(false, 7));
        const Project changed = m_app->project()->project();
        const Clip* faded = changed.timeline().clip(picture.id);
        QVERIFY(faded);
        QCOMPARE(faded->properties.fadeIn, FrameCount{7});
        QCOMPARE(faded->properties.fadeOut, picture.properties.fadeOut);
        QCOMPARE(faded->properties.video, picture.properties.video);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Set Selected Fade Ins"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), changed);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        timeline->clearSelection();
        QVERIFY2(m_qmlWarnings.isEmpty(), "Fading clips emitted QML paint warnings");
    }

    void selectionZoomAndPlayheadCenterPreserveProject()
    {
        auto* timeline = m_app->timeline();
        QQuickItem* lanes = findItem(m_window->contentItem(), QStringLiteral("timelineLanes"));
        QVERIFY(lanes);
        QVERIFY(lanes->width() > 0);
        timeline->clearSelection();
        QVERIFY(timeline->selectedFrameRange().isEmpty());

        // Pick clips on distinct tracks to exercise whole-selection bounds.
        QVERIFY(!track(0).clips.empty());
        QVERIFY(!track(2).clips.empty());
        const Clip video = track(0).clips.back();
        const Clip audio = track(2).clips.front();
        timeline->select(video.id, false);
        timeline->select(audio.id, true);
        const QVariantMap selected = timeline->selectedFrameRange();
        QVERIFY(!selected.isEmpty());
        QCOMPARE(selected.value(QStringLiteral("start")).toDouble(),
                 static_cast<double>(std::min(video.start, audio.start)));
        QCOMPARE(selected.value(QStringLiteral("end")).toDouble(),
                 static_cast<double>(std::max(video.end(), audio.end())));

        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        const bool modifiedBefore = m_app->project()->isModified();
        timeline->setPixelsPerFrame(timeline->minPixelsPerFrame());
        const double minimum = timeline->pixelsPerFrame();
        timeline->setPixelsPerFrame(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(timeline->pixelsPerFrame(), minimum);
        timeline->setPixelsPerFrame(std::numeric_limits<double>::infinity());
        QCOMPARE(timeline->pixelsPerFrame(), minimum);
        timeline->setPixelsPerFrame(-std::numeric_limits<double>::infinity());
        QCOMPARE(timeline->pixelsPerFrame(), minimum);

        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_0, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor([&] { return timeline->pixelsPerFrame() > minimum; }, 2000));

        const double ppf = timeline->pixelsPerFrame();
        const double offset = lanes->property("contentX").toDouble();
        const double left = selected.value(QStringLiteral("start")).toDouble() * ppf - offset;
        const double right = selected.value(QStringLiteral("end")).toDouble() * ppf - offset;
        QVERIFY2(left >= -2.0, qPrintable(QStringLiteral("selection begins at %1 px").arg(left)));
        QVERIFY2(right <= lanes->width() + 2.0,
                 qPrintable(QStringLiteral("selection ends at %1 px").arg(right)));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);

        timeline->clearSelection();
        QVERIFY(timeline->selectedFrameRange().isEmpty());
        const double zoomBefore = timeline->pixelsPerFrame();
        QTest::keyClick(m_window, Qt::Key_0, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(timeline->pixelsPerFrame(), zoomBefore);

        // Centering is a viewport action: it must not seek or create an undo entry.
        timeline->setPixelsPerFrame(timeline->maxPixelsPerFrame());
        m_app->playback()->seek(40);
        const double frameBefore = m_app->playback()->position();
        lanes->setProperty("contentX", 0);
        QTest::keyClick(m_window, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        const double expectedX = std::max(
            0.0, std::min(lanes->property("contentWidth").toDouble() - lanes->width(),
                          frameBefore * timeline->pixelsPerFrame() - lanes->width() * 0.5));
        QVERIFY(QTest::qWaitFor(
            [&] { return std::abs(lanes->property("contentX").toDouble() - expectedX) < 2.0; },
            2000));
        QCOMPARE(m_app->playback()->position(), frameBefore);
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QCOMPARE(m_app->project()->isModified(), modifiedBefore);

        timeline->zoomToFit(1200);
        lanes->setProperty("contentX", 0);
    }

    void selectRelatedClipsWithoutEditingProject()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        const QString undoLabel = m_app->project()->undoText();
        const bool dirty = m_app->project()->isModified();
        QVERIFY(!track(0).clips.empty());

        // Select clips from all tracks whose half-open ranges cover frame zero.
        m_app->playback()->goToStart();
        QStringList expected;
        for (const Track& trackItem : before.timeline().tracks()) {
            for (const Clip& clip : trackItem.clips) {
                if (clip.contains(0)) {
                    expected.append(clip.id);
                }
            }
        }
        QVERIFY(!expected.isEmpty());
        timeline->clearSelection();
        m_window->requestActivate();
        QTest::keyClick(m_window, Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(timeline->selection().size(), expected.size());
        for (const Id& id : expected) {
            QVERIFY(timeline->selection().contains(id));
        }

        // A missing track does not erase the existing selection; locked tracks
        // remain selectable for inspection without granting permission to edit.
        QVERIFY(!timeline->selectTrackClips(QStringLiteral("missing-track")));
        QCOMPARE(timeline->selection().size(), expected.size());
        const Track& video = before.timeline().tracks().front();
        QVERIFY(!video.clips.empty());
        QVERIFY(timeline->selectTrackClips(video.id));
        QCOMPARE(timeline->selection().size(), static_cast<qsizetype>(video.clips.size()));
        for (const Clip& clip : video.clips) {
            QVERIFY(timeline->selection().contains(clip.id));
        }

        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoLabel);
        QCOMPARE(m_app->project()->isModified(), dirty);

        // A duplicated use of a source must be included across the timeline.
        const Id assetId = video.clips.front().assetId;
        QVERIFY(timeline->appendAsset(assetId));
        const std::vector<Id> uses =
            m_app->project()->project().timeline().clipsUsingAsset(assetId);
        QVERIFY(uses.size() >= size_t{2});
        timeline->select(video.clips.front().id, false);
        QTest::keyClick(m_window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(timeline->selection().size(), static_cast<qsizetype>(uses.size()));
        for (const Id& id : uses) {
            QVERIFY(timeline->selection().contains(id));
        }
        const Timeline withDuplicate = m_app->project()->project().timeline();
        const QString undoAfterAppend = m_app->project()->undoText();
        QVERIFY(timeline->selectAllMediaOccurrences() == false); // multiple clips selected
        QCOMPARE(m_app->project()->project().timeline(), withDuplicate);
        QCOMPARE(m_app->project()->undoText(), undoAfterAppend);

        timeline->clearSelection();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), before.timeline());
    }

    void selectWholeClipsBeforeOrAfterPlayheadWithoutEditing()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(!before.timeline().tracks().front().clips.empty());
        const Clip anchor = before.timeline().tracks().front().clips.front();
        const FrameCount boundary = anchor.end();
        m_app->playback()->seek(static_cast<double>(boundary));
        QSet<Id> earlier;
        QSet<Id> later;
        for (const Track& track : before.timeline().tracks()) {
            for (const Clip& clip : track.clips) {
                if (clip.end() <= boundary) {
                    earlier.insert(clip.id);
                }
                if (clip.start >= boundary) {
                    later.insert(clip.id);
                }
            }
        }
        QVERIFY(!earlier.isEmpty());
        QVERIFY(timeline->selectClipsBeforePlayhead());
        const QStringList earlierSelection = timeline->selection();
        QCOMPARE(QSet<Id>(earlierSelection.cbegin(), earlierSelection.cend()), earlier);

        // At the beginning no complete clip can have ended.
        m_app->playback()->goToStart();
        const QString undoBefore = m_app->project()->undoText();
        const QStringList selectedBefore = timeline->selection();
        QVERIFY(!timeline->selectClipsBeforePlayhead());
        QCOMPARE(timeline->selection(), selectedBefore);

        if (!later.isEmpty()) {
            m_app->playback()->seek(static_cast<double>(boundary));
            QVERIFY(timeline->selectClipsAfterPlayhead());
            const QStringList laterSelection = timeline->selection();
            QCOMPARE(QSet<Id>(laterSelection.cbegin(), laterSelection.cend()), later);
        }
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        timeline->clearSelection();
    }

    void invalidFloatingPointTimelinePositionsLeaveEditUnchanged()
    {
        auto* timeline = m_app->timeline();
        const Timeline before = m_app->project()->project().timeline();
        const QString undoBefore = m_app->project()->undoText();
        const Clip clip = track(0).clips.front();
        const Id trackId = track(0).id;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();

        for (double invalid : {nan, inf, -inf, 1.0e100}) {
            QVERIFY(!timeline->moveClip(clip.id, trackId, invalid));
            QVERIFY(!timeline->trimStart(clip.id, invalid));
            QVERIFY(!timeline->trimEnd(clip.id, invalid));
            QVERIFY(!timeline->dropAsset(clip.assetId, trackId, invalid));
            QVERIFY(!timeline->canPlace(trackId, invalid, 20.0, clip.id));
            QCOMPARE(timeline->snapFrame(invalid, clip.id, 2.0), 0.0);
            QCOMPARE(timeline->snapClipStart(invalid, 20.0, clip.id, 2.0), 0.0);
        }

        QVERIFY(!timeline->canPlace(trackId, 10.0, nan, clip.id));
        QVERIFY(!timeline->canPlace(trackId, 10.0, inf, clip.id));
        QVERIFY(!timeline->canPlace(trackId, 10.0, -inf, clip.id));
        QVERIFY(!timeline->canPlace(trackId, 10.0, 0.0, clip.id));
        QCOMPARE(timeline->snapClipStart(10.0, nan, clip.id, 2.0), 10.0);
        QVERIFY(std::isfinite(timeline->snapFrame(10.0, clip.id, nan)));
        QVERIFY(std::isfinite(timeline->snapClipStart(10.0, 20.0, clip.id, inf)));
        QCOMPARE(m_app->project()->project().timeline(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
    }

    void insertTimelineGapAtPlayheadPreservesSourcesAndUndo()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(before.timeline().clipCount() > 0);
        QVERIFY(!before.timeline().tracks().front().clips.empty());
        const Clip first = before.timeline().tracks().front().clips.front();
        QVERIFY(first.duration > 3);

        // A split-free edit at frame zero shifts every clip consistently.
        m_app->playback()->goToStart();
        QVERIFY(timeline->insertGapAtPlayhead(10));
        const Project after = m_app->project()->project();
        QCOMPARE(after.timeline().clipCount(), before.timeline().clipCount());
        for (const Track& track : before.timeline().tracks()) {
            for (const Clip& clip : track.clips) {
                const Clip* shifted = after.timeline().clip(clip.id);
                QVERIFY(shifted);
                QCOMPARE(shifted->start, clip.start + FrameCount{10});
                QCOMPARE(shifted->sourceIn, clip.sourceIn);
                QCOMPARE(shifted->duration, clip.duration);
                QCOMPARE(shifted->properties, clip.properties);
            }
        }
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Insert Timeline Gap"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), after);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);

        // A playhead inside a clip must not create a partial gap or undo entry.
        m_app->playback()->seek(static_cast<double>(first.start + 2));
        const QString undoText = m_app->project()->undoText();
        QVERIFY(!timeline->insertGapAtPlayhead(10));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoText);
    }

    void customFrameGapAndAutomaticGapClosureAreUndoable()
    {
        auto* timeline = m_app->timeline();
        const Project before = m_app->project()->project();
        QVERIFY(before.timeline().clipCount() > 0);
        QVERIFY(!before.timeline().tracks().front().clips.empty());
        m_app->playback()->goToStart();
        const QString originalUndo = m_app->project()->undoText();

        // A clip already occupies frame zero, so there is nothing to close.
        QVERIFY(!timeline->closeEmptyGapToNextClip());
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), originalUndo);

        // Exercise the actual QML form, not just the controller's fast path.
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("preciseGapDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QQuickItem* frames =
            findItem(m_window->contentItem(), QStringLiteral("preciseGapFrameCount"));
        QQuickItem* apply = findItem(m_window->contentItem(), QStringLiteral("preciseGapConfirm"));
        QVERIFY(frames);
        QVERIFY(apply);
        QVERIFY(frames->setProperty("value", 37));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(apply));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        const Project withGap = m_app->project()->project();
        for (const Track& originalTrack : before.timeline().tracks()) {
            for (const Clip& clip : originalTrack.clips) {
                const Clip* shifted = withGap.timeline().clip(clip.id);
                QVERIFY(shifted);
                QCOMPARE(shifted->start, clip.start + FrameCount{37});
                QCOMPARE(shifted->sourceIn, clip.sourceIn);
                QCOMPARE(shifted->duration, clip.duration);
            }
        }

        // Locked affected tracks refuse auto-close without touching undo history.
        const Id videoTrackId = before.timeline().tracks().front().id;
        QVERIFY(timeline->setTrackFlag(videoTrackId, QStringLiteral("locked"), true));
        const Project locked = m_app->project()->project();
        const QString lockedUndo = m_app->project()->undoText();
        QVERIFY(!timeline->closeEmptyGapToNextClip());
        QCOMPARE(m_app->project()->project(), locked);
        QCOMPARE(m_app->project()->undoText(), lockedUndo);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withGap);

        QVERIFY(timeline->closeEmptyGapToNextClip());
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Remove Empty Timeline Time"));
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withGap);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), withGap);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->undo();
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);

        const Clip first = before.timeline().tracks().front().clips.front();
        m_app->playback()->seek(static_cast<double>(first.start + 2));
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!timeline->closeEmptyGapToNextClip());
        QVERIFY(!timeline->insertGapAtPlayhead(0));
        QVERIFY(!timeline->removeEmptyTimeAtPlayhead(0));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
    }

    void timelineGapUiMovesMarkersAndSubtitlesAsOneSequenceEdit()
    {
        auto* timeline = m_app->timeline();
        auto* playback = m_app->playback();
        const Project initial = m_app->project()->project();
        const FrameCount originalPlayhead = playback->positionFrames();
        const QString assetId =
            m_app->media()->model()->get(0).value(QStringLiteral("assetId")).toString();
        QVERIFY(!assetId.isEmpty());
        const FrameCount cut = initial.timeline().duration() + 10;
        const Id trackId = initial.timeline().tracks().front().id;
        Clip later;
        later.id = createId();
        later.assetId = assetId;
        later.start = cut + 10;
        later.sourceIn = 0;
        later.duration = 10;
        QVERIFY(m_app->project()->execute(edits::addClip(trackId, later)));

        const Id markerId = createId();
        const Id subtitleId = createId();
        const auto cutMs = subtitleMillisecondsAtFrame(cut, initial.settings().frameRate);
        QVERIFY(cutMs);
        QVERIFY(m_app->project()->execute(
            edits::addMarker({markerId, cut, QStringLiteral("Scene after cut")})));
        QVERIFY(m_app->project()->execute(edits::addSubtitle(
            {subtitleId, *cutMs + 100, *cutMs + 300, QStringLiteral("Scene caption")})));
        const Project prepared = m_app->project()->project();

        playback->seek(static_cast<double>(cut));
        QVERIFY(timeline->insertGapAtPlayhead(10));
        const Project after = m_app->project()->project();
        QCOMPARE(after.timeline().clip(later.id)->start, cut + 20);
        const auto marker =
            std::find_if(after.markers().begin(), after.markers().end(),
                         [&](const TimelineMarker& value) { return value.id == markerId; });
        QVERIFY(marker != after.markers().end());
        QCOMPARE(marker->frame, cut + 10);
        const auto subtitle =
            std::find_if(after.subtitles().cues.begin(), after.subtitles().cues.end(),
                         [&](const SubtitleCue& value) { return value.id == subtitleId; });
        QVERIFY(subtitle != after.subtitles().cues.end());
        QVERIFY(subtitle->startMs > *cutMs + 100);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Insert Timeline Gap"));

        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), prepared);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), after);
        QVERIFY(timeline->removeEmptyTimeAtPlayhead(10));
        QCOMPARE(m_app->project()->project(), prepared);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), after);
        // GuiTest reuses one application across test cases. Restore the fixture
        // precisely so later editor, marker and subtitle tests are independent.
        m_app->project()->undo(); // insert gap
        QCOMPARE(m_app->project()->project(), prepared);
        m_app->project()->undo(); // add subtitle
        m_app->project()->undo(); // add marker
        m_app->project()->undo(); // add clip
        QCOMPARE(m_app->project()->project(), initial);
        playback->seek(static_cast<double>(originalPlayhead));
    }

    void removingEmptyTimeRestoresClipsAfterGapInsertion()
    {
        const Project before = m_app->project()->project();
        QVERIFY(before.timeline().clipCount() > 0);
        m_app->playback()->goToStart();
        QVERIFY(m_app->timeline()->insertGapAtPlayhead(10));
        const Project withGap = m_app->project()->project();
        QVERIFY(m_app->timeline()->removeEmptyTimeAtPlayhead(10));
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), withGap);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
    }

    void reorderAdjacentTracksWithoutChangingClipData()
    {
        const Timeline original = m_app->project()->project().timeline();
        QVERIFY(original.trackCount() >= 3);
        const Id firstVideo = original.tracks().front().id;
        const Id secondVideo = original.tracks()[1].id;
        const Id audio = original.tracks()[2].id;
        QVERIFY(original.tracks()[0].kind == TrackKind::Video);
        QVERIFY(original.tracks()[1].kind == TrackKind::Video);
        QVERIFY(original.tracks()[2].kind == TrackKind::Audio);

        QVERIFY(!m_app->timeline()->canMoveTrack(firstVideo, -1));
        QVERIFY(m_app->timeline()->canMoveTrack(firstVideo, 1));
        QVERIFY(!m_app->timeline()->canMoveTrack(secondVideo, 1));
        QVERIFY(!m_app->timeline()->canMoveTrack(QStringLiteral("missing"), 1));
        QVERIFY(!m_app->timeline()->canMoveTrack(firstVideo, 2));
        QVERIFY(!m_app->timeline()->moveTrack(firstVideo, -1));
        QCOMPARE(m_app->project()->project().timeline(), original);

        QVERIFY(m_app->timeline()->moveTrack(firstVideo, 1));
        const Timeline reordered = m_app->project()->project().timeline();
        QCOMPARE(reordered.tracks().front().id, secondVideo);
        QCOMPARE(reordered.tracks()[1].id, firstVideo);
        QCOMPARE(reordered.tracks()[2].id, audio);
        QCOMPARE(reordered.clipCount(), original.clipCount());
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), original);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().timeline(), reordered);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), original);
    }

    void alignSelectedClipEdgesToPlayhead()
    {
        auto* timeline = m_app->timeline();
        const Timeline original = m_app->project()->project().timeline();
        const Clip clip = track(0).clips.back();
        QVERIFY(clip.duration > 5);
        timeline->select(clip.id, false);

        // Move the start by five frames. The last clip on this track has space
        // to its right, so its duration and source trims must not change.
        m_app->playback()->seek(static_cast<double>(clip.start + 5));
        QVERIFY(timeline->alignSelectionToPlayhead(false));
        const Clip* moved = m_app->project()->project().timeline().clip(clip.id);
        QVERIFY(moved);
        QCOMPARE(moved->start, clip.start + FrameCount{5});
        QCOMPARE(moved->duration, clip.duration);
        QCOMPARE(moved->sourceIn, clip.sourceIn);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), original);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().timeline().clip(clip.id)->start,
                 clip.start + FrameCount{5});

        // Align the exclusive end back to the old end, restoring placement.
        m_app->playback()->seek(static_cast<double>(clip.end()));
        QVERIFY(timeline->alignSelectionToPlayhead(true));
        QCOMPARE(m_app->project()->project().timeline(), original);

        // Invalid end alignment is non-destructive and adds no undo command.
        const QString undoText = m_app->project()->undoText();
        m_app->playback()->goToStart();
        QVERIFY(!timeline->alignSelectionToPlayhead(true));
        QCOMPARE(m_app->project()->project().timeline(), original);
        QCOMPARE(m_app->project()->undoText(), undoText);

        // Undo both movements and leave the next GUI test with its original edit.
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline().clip(clip.id)->start,
                 clip.start + FrameCount{5});
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project().timeline(), original);
        timeline->clearSelection();
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
        // Exercise the actual Find and Replace QML controls, including model
        // bindings, literal matching, and undo. The scope guard restores data.
        QQuickItem* search = findItem(m_window->contentItem(), QStringLiteral("subtitleFindText"));
        QQuickItem* replacement =
            findItem(m_window->contentItem(), QStringLiteral("subtitleReplacementText"));
        QQuickItem* findNext =
            findItem(m_window->contentItem(), QStringLiteral("subtitleFindNext"));
        QQuickItem* replaceAll =
            findItem(m_window->contentItem(), QStringLiteral("subtitleReplaceAll"));
        QVERIFY(search && replacement && findNext && replaceAll);
        QVERIFY(search->setProperty("text", QStringLiteral("Welcome")));
        QVERIFY(replacement->setProperty("text", QStringLiteral("Greetings")));
        QTRY_VERIFY(findNext->isEnabled() && replaceAll->isEnabled());
        QVERIFY(click(QStringLiteral("subtitleFindNext")));
        QVERIFY(click(QStringLiteral("subtitleReplaceAll")));
        QTRY_VERIFY(m_app->project()->project().subtitles().cues.front().text.contains(
            QStringLiteral("Greetings")));
        QCOMPARE(m_app->project()->project().subtitles().cues.front().startMs, std::int64_t{123});
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
        QVERIFY(click(QStringLiteral("subtitleShiftPlus100")));
        QTRY_COMPARE(m_app->project()->project().subtitles().cues.front().startMs,
                     std::int64_t{223});
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
        QQuickItem* offset =
            findItem(m_window->contentItem(), QStringLiteral("subtitleTimingOffset"));
        QVERIFY(offset && offset->isVisible());
        QVERIFY(offset->setProperty("value", 250));
        QVERIFY(click(QStringLiteral("subtitleApplyTimingOffset")));
        QTRY_COMPARE(m_app->project()->project().subtitles().cues.front().startMs,
                     std::int64_t{373});
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
        // Exercise the actual QML SpinBox and Apply button, including 123ms
        // * 150% = 184.5ms rounding to 185ms. One Undo restores both boundaries.
        QQuickItem* stretch =
            findItem(m_window->contentItem(), QStringLiteral("subtitleStretchPercent"));
        QVERIFY(stretch && stretch->isVisible());
        QVERIFY(stretch->setProperty("value", 150));
        QVERIFY(click(QStringLiteral("subtitleApplyTimingStretch")));
        QTRY_COMPARE(m_app->project()->project().subtitles().cues.front().startMs,
                     std::int64_t{185});
        QCOMPARE(m_app->project()->project().subtitles().cues.front().endMs, std::int64_t{3000});
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
        // Snap captions to the next project-frame boundary in one undo step.
        QVERIFY(click(QStringLiteral("subtitleAlignFrames")));
        const FrameRate& projectRate = m_app->project()->project().settings().frameRate;
        const auto firstFrame = subtitleFrameAtMilliseconds(123, projectRate);
        QVERIFY(firstFrame);
        const auto sampled = subtitleMillisecondsAtFrame(*firstFrame, projectRate);
        QVERIFY(sampled);
        QTRY_COMPARE(m_app->project()->project().subtitles().cues.front().startMs, *sampled);
        QCOMPARE(m_app->project()->project().timeline(), initial);
        QVERIFY(click(QStringLiteral("subtitleUndo")));
        QTRY_COMPARE(m_app->project()->project().subtitles(), applied);
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
        // The additional caption timing controls must not push the cue editor
        // action row underneath the dialog footer at the 1200x800 baseline.
        QQuickItem* cueActions =
            findItem(m_window->contentItem(), QStringLiteral("subtitleCueActions"));
        QQuickItem* closeButton =
            findItem(m_window->contentItem(), QStringLiteral("subtitleClose"));
        QQuickItem* workflowHint =
            findItem(m_window->contentItem(), QStringLiteral("subtitleWorkflowHint"));
        QVERIFY(cueActions && closeButton && workflowHint);
        QVERIFY(cueActions->isVisible() && closeButton->isVisible() && workflowHint->isVisible());
        // Compare real visible controls and note text with the button row:
        // the footer item's own padded bounding box can extend above its buttons.
        const auto closeTop = [&] { return closeButton->mapToScene(QPointF(0.0, 0.0)).y(); };
        QTRY_VERIFY(cueActions->mapToScene(QPointF(0.0, cueActions->height())).y() + 4.0 <
                    closeTop());
        QTRY_VERIFY(workflowHint->mapToScene(QPointF(0.0, workflowHint->height())).y() + 2.0 <
                    closeTop());
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

    void transportSkipsAndSequenceMidpointKeepProjectUnchanged()
    {
        auto* playback = m_app->playback();
        const Project snapshot = m_app->project()->project();
        const QString undo = m_app->project()->undoText();
        const bool modified = m_app->project()->isModified();
        const FrameCount length = snapshot.timeline().duration();
        QVERIFY(length > 0);

        playback->goToStart();
        QVERIFY(!playback->skipSeconds(0));
        QVERIFY(!playback->skipSeconds(61));
        QVERIFY(!playback->skipSeconds(-61));
        QVERIFY(!playback->skipSeconds(-5));
        QCOMPARE(playback->positionFrames(), FrameCount{0});
        QVERIFY(playback->skipSeconds(5));
        const FrameCount five =
            static_cast<FrameCount>(std::llround(5.0 * snapshot.settings().frameRate.toDouble()));
        QCOMPARE(playback->positionFrames(), std::min(five, length - 1));

        QVERIFY(playback->goToSequenceMidpoint());
        QCOMPARE(playback->positionFrames(), (length - 1) / 2);
        playback->goToEnd();
        QVERIFY(!playback->skipSeconds(10));
        QVERIFY(playback->skipSeconds(-10));
        const FrameCount ten =
            static_cast<FrameCount>(std::llround(10.0 * snapshot.settings().frameRate.toDouble()));
        QCOMPARE(playback->positionFrames(), std::max<FrameCount>(length - 1 - ten, 0));

        // Exercise real QAction shortcuts, not only direct controller methods.
        playback->goToStart();
        QTest::keyClick(m_window, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(playback->positionFrames(), std::min(five, length - 1));
        QTest::keyClick(m_window, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(playback->positionFrames(), FrameCount{0});
        QTest::keyClick(m_window, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(playback->positionFrames(), std::min(ten, length - 1));

        QCOMPARE(m_app->project()->project(), snapshot);
        QCOMPARE(m_app->project()->undoText(), undo);
        QCOMPARE(m_app->project()->isModified(), modified);
        playback->goToStart();
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

    void pasteVideoChaptersAndClearMarkersWithUndo()
    {
        const Project original = m_app->project()->project();
        const Timeline timelineBefore = original.timeline();
        // The chapter list includes an opening cue. Preserve any markers from
        // earlier tests by clearing them in a reversible setup step.
        const bool hadMarkers = !original.markers().empty();
        if (hadMarkers) {
            QVERIFY(m_app->timeline()->clearAllMarkers());
        }

        QClipboard* clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->setText(QStringLiteral("00:00 Intro\n00:01 Scene two\n"));
        QVERIFY(m_app->timeline()->pasteChaptersAsMarkers());
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Import Chapter Markers"));
        QCOMPARE(m_app->project()->project().markers().size(), size_t{2});
        QCOMPARE(m_app->project()->project().markers()[0].frame, FrameCount{0});
        QCOMPARE(m_app->project()->project().markers()[1].frame,
                 static_cast<FrameCount>(std::ceil(m_app->timeline()->frameRate())));

        const Project imported = m_app->project()->project();
        clipboard->setText(QStringLiteral("not timed chapter text"));
        QVERIFY(!m_app->timeline()->pasteChaptersAsMarkers());
        QCOMPARE(m_app->project()->project(), imported);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Import Chapter Markers"));

        QVERIFY(m_app->timeline()->clearAllMarkers());
        QVERIFY(m_app->project()->project().markers().empty());
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), imported);
        m_app->project()->undo();
        if (hadMarkers) {
            m_app->project()->undo();
        }
        QCOMPARE(m_app->project()->project().markers(), original.markers());
        QCOMPARE(m_app->project()->project().timeline(), timelineBefore);
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

    void batchRelinkMissingMediaFromFolderIsUndoable()
    {
        const Project before = m_app->project()->project();
        const QString root = m_dir.filePath(QStringLiteral("relink-folder"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/nested")));
        const QString still =
            makeStill(root + QStringLiteral("/nested"), QStringLiteral("recover-still.png"),
                      QColor(QStringLiteral("#1a6c83")), QSize(32, 18));
        QVERIFY(!still.isEmpty());
        const auto probed = media::createAsset(still);
        QVERIFY2(probed, qPrintable(probed ? QString() : probed.error().toString()));

        MediaAsset missing = *probed;
        missing.filePath = m_dir.filePath(QStringLiteral("no-longer-here/recover-still.png"));
        missing.status = MediaStatus::Missing;
        QVERIFY(m_app->project()->execute(
            std::make_unique<AddMediaCommand>(std::vector<MediaAsset>{missing})));
        const Project offline = m_app->project()->project();
        QVERIFY(offline.asset(missing.id));
        QCOMPARE(offline.asset(missing.id)->status, MediaStatus::Missing);
        QVERIFY(m_app->media()->relinkMissingFolder(root));
        QTRY_VERIFY_WITH_TIMEOUT(!m_app->media()->importing(), 15000);

        const MediaAsset* found = m_app->project()->project().asset(missing.id);
        QVERIFY(found);
        QCOMPARE(found->status, MediaStatus::Online);
        QCOMPARE(found->filePath, still);
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Relink Missing Media"));
        QCOMPARE(m_app->project()->project().timeline(), before.timeline());

        m_app->project()->undo(); // undo all relinked media in one step
        QCOMPARE(m_app->project()->project(), offline);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project().asset(missing.id)->filePath, still);
        m_app->project()->undo(); // undo relink
        m_app->project()->undo(); // remove the temporary test media
        QCOMPARE(m_app->project()->project(), before);
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
        QSignalSpy stateChanges(m_app->media(), &ui::MediaController::importingChanged);
        QVERIFY(m_app->media()->importFolder(root));
        QVERIFY(QTest::qWaitFor([&] { return m_app->media()->model()->rowCount() == before + 1; },
                                15000));
        QVERIFY(m_app->project()->project().assetByPath(still));
        QVERIFY(stateChanges.count() >= 2); // scanning and probing status transitions
        QVERIFY(m_app->media()->importFolder(root));
        // A duplicate scan still completes asynchronously. Do not remove an asset
        // while its scan can race with the resulting duplicate check.
        QTRY_VERIFY(!m_app->media()->importing());
        QCOMPARE(m_app->media()->model()->rowCount(), before + 1);
        const MediaAsset* added = m_app->project()->project().assetByPath(still);
        QVERIFY(added);
        const Id addedId = added->id;
        m_app->media()->remove(addedId);
        QCOMPARE(m_app->media()->model()->rowCount(), before);

        // Validation errors are delivered asynchronously without altering the project.
        QSignalSpy problems(m_app->media(), &ui::MediaController::importProblems);
        QVERIFY(m_app->media()->importFolder(root + QStringLiteral("/missing")));
        QTRY_COMPARE(problems.count(), 1);
        QTRY_VERIFY(!m_app->media()->importing());
        QCOMPARE(m_app->media()->model()->rowCount(), before);
        // Import errors open the application's modal MessageBox. A GUI regression test
        // must dismiss it before later mouse/drag tests can interact with the timeline.
        QTRY_VERIFY(m_window->property("dialogOpen").toBool());
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!m_window->property("dialogOpen").toBool());
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
        QVERIFY(
            !m_app->playback()->savePreviewFrame(m_dir.filePath(QStringLiteral("not-a-png.jpg"))));
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

    void stereoBalanceInspectorEditsAndPreservesClipHistory()
    {
        if (!engine::MltRuntime::capabilities().pannerFilter) {
            QSKIP("This MLT installation does not provide the optional stereo panner.");
        }
        const auto& audioTracks = m_app->project()->project().timeline().tracks();
        const auto audioTrack =
            std::find_if(audioTracks.begin(), audioTracks.end(), [](const Track& track) {
                return track.kind == TrackKind::Audio && !track.clips.empty();
            });
        QVERIFY(audioTrack != audioTracks.end());
        const Id clipId = audioTrack->clips.front().id;
        const MediaAsset* asset =
            m_app->project()->project().asset(audioTrack->clips.front().assetId);
        QVERIFY(asset);
        QCOMPARE(asset->info.audioChannels, 2);
        const Project before = m_app->project()->project();

        m_app->timeline()->select(clipId, false);
        QQuickItem* balance =
            findItem(m_window->contentItem(), QStringLiteral("clipStereoBalance"));
        QVERIFY(balance);
        QTRY_VERIFY(balance->isVisible());
        QCOMPARE(balance->property("value").toDouble(), 0.0);
        QVERIFY(QMetaObject::invokeMethod(balance, "moved", Q_ARG(double, -0.5)));
        QTRY_COMPARE(m_app->project()->project().timeline().clip(clipId)->properties.balance, -0.5);
        m_app->timeline()->endEdit();
        QCOMPARE(m_app->project()->undoText(), QStringLiteral("Change Stereo Balance"));

        const Project edited = m_app->project()->project();
        QVERIFY(!m_app->timeline()->setClipProperty(clipId, QStringLiteral("balance"),
                                                    std::numeric_limits<double>::infinity()));
        QCOMPARE(m_app->project()->project(), edited);
        m_app->project()->undo();
        QCOMPARE(m_app->project()->project(), before);
        m_app->project()->redo();
        QCOMPARE(m_app->project()->project(), edited);
        m_app->project()->undo();
    }

    void exportDialogValidatesAndExports()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        QVERIFY(QTest::qWaitFor([&] { return dialog->property("opened").toBool(); }, 2000));
        dialog->setProperty("outputPath", m_dir.filePath(QStringLiteral("gui-export.mp4")));
        QQuickItem* fpsSelector =
            findItem(m_window->contentItem(), QStringLiteral("exportFrameRateBox"));
        QVERIFY(fpsSelector);
        QVERIFY(fpsSelector->isVisible());
        QCOMPARE(fpsSelector->property("currentIndex").toInt(), 0);
        fpsSelector->setProperty("currentIndex", 3); // 25 fps instead of the 30 fps project
        QTest::qWait(200);
        screenshot(QStringLiteral("05-export-dialog"));
        QVariantMap settings{
            {QStringLiteral("presetId"), QStringLiteral("draft")},
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("gui-export.mp4"))},
            {QStringLiteral("speedPreset"), QStringLiteral("ultrafast")},
            {QStringLiteral("frameRate"), QStringLiteral("25")}};
        QCOMPARE(m_app->exporter()->validate(settings), QString());
        QVERIFY(m_app->exporter()->describeOutput(settings).contains(QStringLiteral("25 fps")));
        QSignalSpy finished(m_app->exporter(), &ui::ExportController::finished);
        QVERIFY(m_app->exporter()->start(settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(m_app->project()->project().settings().frameRate, FrameRate::integer(30));
        QTest::qWait(200);
        screenshot(QStringLiteral("06-export-finished"));
        QMetaObject::invokeMethod(dialog, "close");
    }

    void exportDialogOffersSafeRangeModesWithoutChangingTimeline()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QQuickItem* selector = findItem(m_window->contentItem(), QStringLiteral("exportRangeBox"));
        QQuickItem* start = findItem(m_window->contentItem(), QStringLiteral("exportStartFrame"));
        QQuickItem* end = findItem(m_window->contentItem(), QStringLiteral("exportEndFrame"));
        QVERIFY(selector);
        QVERIFY(start);
        QVERIFY(end);
        QCOMPARE(selector->property("currentIndex").toInt(), 0);
        selector->setProperty("currentIndex", 2); // custom source-time range
        QTRY_VERIFY(start->isVisible());
        QTRY_VERIFY(end->isVisible());
        start->setProperty("text", QStringLiteral("5"));
        end->setProperty("text", QStringLiteral("15"));

        const QVariantMap validSettings{
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("gui-range.mp4"))},
            {QStringLiteral("rangeStartFrame"), QStringLiteral("5")},
            {QStringLiteral("rangeEndFrame"), QStringLiteral("15")}};
        QCOMPARE(m_app->exporter()->validate(validSettings), QString());
        const QString summary = m_app->exporter()->describeOutput(validSettings);
        QVERIFY(summary.contains(QStringLiteral("frames 5–15")));
        QVERIFY(summary.contains(QStringLiteral("10 output frames")));
        QVariantMap at60 = validSettings;
        at60[QStringLiteral("frameRate")] = QStringLiteral("60");
        QCOMPARE(m_app->exporter()->validate(at60), QString());
        QVERIFY(
            m_app->exporter()->describeOutput(at60).contains(QStringLiteral("20 output frames")));
        QQuickItem* outputSummary =
            findItem(m_window->contentItem(), QStringLiteral("exportOutputSummary"));
        QVERIFY(outputSummary);
        QVERIFY(outputSummary->isVisible());

        QVariantMap invalid = validSettings;
        invalid[QStringLiteral("rangeEndFrame")] = QStringLiteral("not-an-integer");
        QVERIFY(!m_app->exporter()->validate(invalid).isEmpty());
        invalid[QStringLiteral("rangeStartFrame")] = QStringLiteral("50");
        invalid[QStringLiteral("rangeEndFrame")] = QStringLiteral("5");
        QVERIFY(!m_app->exporter()->validate(invalid).isEmpty());
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        selector->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(dialog, "close");
    }

    void exportDialogShowsCustomWidthAndPreservesProjectSettings()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialog->setProperty("outputPath", m_dir.filePath(QStringLiteral("custom-width-gui.mp4")));

        QQuickItem* noUpscale =
            findItem(m_window->contentItem(), QStringLiteral("exportAvoidUpscaling"));
        QQuickItem* enabled =
            findItem(m_window->contentItem(), QStringLiteral("exportCustomWidthEnabled"));
        QQuickItem* width = findItem(m_window->contentItem(), QStringLiteral("exportCustomWidth"));
        QQuickItem* summary =
            findItem(m_window->contentItem(), QStringLiteral("exportOutputSummary"));
        QVERIFY(enabled && width && summary && noUpscale);
        QVERIFY(noUpscale->setProperty("checked", false));
        QVERIFY(enabled->isVisible());
        QVERIFY(!width->property("enabled").toBool());
        QVERIFY(enabled->setProperty("checked", true));
        QVERIFY(width->setProperty("text", QStringLiteral("1280")));
        QTRY_VERIFY(width->property("enabled").toBool());

        const int projectWidth = before.settings().width;
        const int projectHeight = before.settings().height;
        const int derivedHeight =
            std::max(2, static_cast<int>(std::lround(static_cast<double>(projectHeight) * 1280 /
                                                     projectWidth / 2.0)) *
                            2);
        const QString expected = QStringLiteral("%1×%2").arg(1280).arg(derivedHeight);
        QTRY_VERIFY2(summary->property("text").toString().contains(expected),
                     qPrintable(summary->property("text").toString()));

        QVariantMap options{
            {QStringLiteral("presetId"), QStringLiteral("match")},
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("custom-width-gui.mp4"))},
            {QStringLiteral("customWidth"), QStringLiteral("1280")}};
        QCOMPARE(m_app->exporter()->validate(options), QString());
        options[QStringLiteral("customWidth")] = QStringLiteral("127");
        QVERIFY(!m_app->exporter()->validate(options).isEmpty());
        options[QStringLiteral("customWidth")] = QStringLiteral("not-a-number");
        QVERIFY(!m_app->exporter()->validate(options).isEmpty());

        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(enabled->property("checked").toBool(), true);
        QCOMPARE(width->property("text").toString(), QStringLiteral("1280"));
        QVERIFY(enabled->setProperty("checked", false));
        QVERIFY(!width->property("enabled").toBool());
        QMetaObject::invokeMethod(dialog, "close");
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
    }

    void exportDialogBatchControlsValidateSelectionWithoutChangingProject()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        m_qmlWarnings.clear();
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialog->setProperty("outputPath", m_dir.filePath(QStringLiteral("gui-batch.mp4")));
        QQuickItem* creator =
            findItem(m_window->contentItem(), QStringLiteral("exportBatchCreator"));
        QQuickItem* review = findItem(m_window->contentItem(), QStringLiteral("exportBatchReview"));
        QQuickItem* mobile = findItem(m_window->contentItem(), QStringLiteral("exportBatchMobile"));
        QQuickItem* start = findItem(m_window->contentItem(), QStringLiteral("exportBatchPresets"));
        QQuickItem* range = findItem(m_window->contentItem(), QStringLiteral("exportRangeBox"));
        QQuickItem* fps = findItem(m_window->contentItem(), QStringLiteral("exportFrameRateBox"));
        QQuickItem* creator1440 =
            findItem(m_window->contentItem(), QStringLiteral("exportBatchCreator1440"));
        QQuickItem* creator4k =
            findItem(m_window->contentItem(), QStringLiteral("exportBatchCreator4k"));
        QVERIFY(creator && review && mobile && creator1440 && creator4k && start && range && fps);
        QVERIFY(range->setProperty("currentIndex", 0));
        QVERIFY(fps->setProperty("currentIndex", 0));
        QVERIFY(creator->setProperty("checked", true));
        QVERIFY(review->setProperty("checked", true));
        QVERIFY(mobile->setProperty("checked", false));
        QVERIFY(creator1440->setProperty("checked", true));
        QVERIFY(creator4k->setProperty("checked", true));
        QVERIFY(mobile->setProperty("checked", true));
        QTRY_COMPARE(dialog->property("batchSelectionCount").toInt(), 5);
        const QVariantMap allSettings{
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("gui-batch-all.mp4"))},
            {QStringLiteral("frameRate"), QStringLiteral("match")}};
        QCOMPARE(m_app->exporter()->validateBatch(
                     allSettings, {QStringLiteral("youtube1080p"), QStringLiteral("draft"),
                                   QStringLiteral("review480p"), QStringLiteral("youtube1440p"),
                                   QStringLiteral("youtube4k")}),
                 QString());
        QVERIFY(creator1440->setProperty("checked", false));
        QVERIFY(creator4k->setProperty("checked", false));
        QVERIFY(mobile->setProperty("checked", false));
        QTRY_VERIFY(start->isEnabled());
        QVERIFY(creator->setProperty("checked", false));
        QTRY_VERIFY(!start->isEnabled()); // one preset is not a batch
        QVERIFY(mobile->setProperty("checked", true));
        QTRY_VERIFY(start->isEnabled());
        const QVariantMap settings{
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("gui-batch.mp4"))},
            {QStringLiteral("frameRate"), QStringLiteral("match")}};
        QCOMPARE(m_app->exporter()->validateBatch(
                     settings, {QStringLiteral("draft"), QStringLiteral("review480p")}),
                 QString());
        QCOMPARE(m_app->exporter()->batchTotal(), 0); // UI selection alone never exports
        // Exercise the real QML -> QStringList invokable path. A short, exact
        // five-frame source interval keeps the end-to-end GUI check bounded.
        QQuickItem* startFrame =
            findItem(m_window->contentItem(), QStringLiteral("exportStartFrame"));
        QQuickItem* endFrame = findItem(m_window->contentItem(), QStringLiteral("exportEndFrame"));
        QVERIFY(startFrame && endFrame);
        QVERIFY(range->setProperty("currentIndex", 2));
        QVERIFY(startFrame->setProperty("text", QStringLiteral("5")));
        QVERIFY(endFrame->setProperty("text", QStringLiteral("10")));
        QTRY_VERIFY(start->isEnabled());
        QSignalSpy finished(m_app->exporter(), &ui::ExportController::finished);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centreOf(start));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120'000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(m_app->exporter()->batchState(), QStringLiteral("succeeded"));
        QCOMPARE(m_app->exporter()->batchCompleted(), 2);
        QQuickItem* statusProgress =
            findItem(m_window->contentItem(), QStringLiteral("exportStatusProgress"));
        QQuickItem* statusLabel =
            findItem(m_window->contentItem(), QStringLiteral("exportStatusLabel"));
        QVERIFY(statusProgress && statusLabel);
        QCOMPARE(statusProgress->property("value").toDouble(), 1.0);
        QVERIFY(QFileInfo::exists(m_dir.filePath(QStringLiteral("gui-batch-draft.mp4"))));
        QVERIFY(QFileInfo::exists(m_dir.filePath(QStringLiteral("gui-batch-review480p.mp4"))));
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
        QVERIFY2(m_qmlWarnings.isEmpty(), "Batch export dialog produced QML warnings");
        QMetaObject::invokeMethod(dialog, "close");
    }

    void exportDialogRetainsCustomQualityAndFpsAcrossReopen()
    {
        QObject* dialog = m_window->findChild<QObject*>(QStringLiteral("exportDialog"));
        QVERIFY(dialog);
        const Project before = m_app->project()->project();
        const QString undoBefore = m_app->project()->undoText();
        QVERIFY(!m_app->exporter()->copyFailureDetails()); // no failed helper to report

        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QQuickItem* quality =
            findItem(m_window->contentItem(), QStringLiteral("exportQualitySlider"));
        QQuickItem* fps = findItem(m_window->contentItem(), QStringLiteral("exportFrameRateBox"));
        QQuickItem* custom =
            findItem(m_window->contentItem(), QStringLiteral("exportCustomFrameRate"));
        QVERIFY(quality);
        QVERIFY(fps);
        QVERIFY(custom);

        QVERIFY(quality->setProperty("value", 28));
        QVERIFY(fps->setProperty("currentIndex", 9)); // Custom FPS
        QVERIFY(custom->setProperty("text", QStringLiteral("24000/1001")));
        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("opened").toBool());

        // Opening must not silently reapply the selected preset over an
        // intentional custom quality adjustment or the chosen FPS.
        QMetaObject::invokeMethod(dialog, "open");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(quality->property("value").toInt(), 28);
        QCOMPARE(fps->property("currentIndex").toInt(), 9);
        QCOMPARE(custom->property("text").toString(), QStringLiteral("24000/1001"));
        QMetaObject::invokeMethod(dialog, "close");

        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->undoText(), undoBefore);
    }

    void startingExportPausesPreviewWithoutChangingProject()
    {
        // Keep the transport looping so a natural end-of-sequence pause cannot
        // masquerade as the export pause during this regression.
        const Project before = m_app->project()->project();
        m_app->playback()->setLoopPreview(true);
        m_app->playback()->play();
        QTRY_VERIFY(m_app->playback()->playing());

        const QVariantMap settings{
            {QStringLiteral("presetId"), QStringLiteral("draft")},
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("pause-preview.mp4"))},
            {QStringLiteral("speedPreset"), QStringLiteral("ultrafast")},
            {QStringLiteral("frameRate"), QStringLiteral("30")}};
        QSignalSpy finished(m_app->exporter(), &ui::ExportController::finished);
        QVERIFY(m_app->exporter()->start(settings));
        QTRY_VERIFY(!m_app->playback()->playing());
        QVERIFY(finished.count() > 0 || finished.wait(60'000));
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        QVERIFY(!m_app->playback()->playing()); // never auto-resume after export
        m_app->playback()->setLoopPreview(false);
        QCOMPARE(m_app->project()->project(), before);
        QVERIFY(m_window->isVisible());
    }

    void repeatedGuiExportsWhileAutosavingKeepEditorResponsive()
    {
        // Reproduce the editor-side lifecycle, not just the in-process RenderRunner:
        // keep Qt Quick and project autosave alive across successive helper exports.
        const Project before = m_app->project()->project();
        const QString destination = m_dir.filePath(QStringLiteral("gui-repeated-fps.mp4"));
        for (const QString& fps : {QStringLiteral("30"), QStringLiteral("24")}) {
            const QVariantMap settings{{QStringLiteral("presetId"), QStringLiteral("draft")},
                                       {QStringLiteral("outputPath"), destination},
                                       {QStringLiteral("speedPreset"), QStringLiteral("ultrafast")},
                                       {QStringLiteral("frameRate"), fps}};
            QCOMPARE(m_app->exporter()->validate(settings), QString());
            QSignalSpy finished(m_app->exporter(), &ui::ExportController::finished);
            QVERIFY(m_app->exporter()->start(settings));
            m_app->project()->autosave(); // exercise recovery serialization during rendering
            QVERIFY(finished.count() > 0 || finished.wait(60'000));
            QVERIFY2(finished.first().at(0).toBool(),
                     qPrintable(finished.first().at(1).toString()));
            QVERIFY(QFileInfo::exists(destination));
            QVERIFY(m_window->isVisible());
            QCOMPARE(m_app->project()->project(), before);
        }
    }

    void exportClipListCsvPreservesProjectAndProtectsUnsafePaths()
    {
        const Project before = m_app->project()->project();
        QVERIFY(before.timeline().clipCount() > 0);
        const QString undoLabel = m_app->project()->undoText();
        const bool dirty = m_app->project()->isModified();
        const QString path = m_dir.filePath(QStringLiteral("clip-list.csv"));
        QVERIFY(m_app->project()->exportClipListCsv(path));

        QFile report(path);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const QByteArray data = report.readAll();
        QVERIFY(data.startsWith("\xEF\xBB\xBF"));
        QVERIFY(data.contains("source_name,source_path"));
        QVERIFY(data.contains("start_timecode,end_timecode_exclusive\r\n"));
        QVERIFY(data.contains(m_media.front().toUtf8()));
        QCOMPARE(data.count('\n'), before.timeline().clipCount() + 1);
        QVERIFY(!data.contains("\r\r\n"));
        report.close();

        // Reject a report path that aliases imported footage without changing bytes.
        const QString original = m_media.front();
        QFile footage(original);
        QVERIFY(footage.open(QIODevice::ReadOnly));
        const QByteArray sourceBytes = footage.readAll();
        footage.close();
        QSignalSpy errors(m_app->project(), &ui::ProjectController::errorOccurred);
        QVERIFY(!m_app->project()->exportClipListCsv(original));
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(footage.open(QIODevice::ReadOnly));
        QCOMPARE(footage.readAll(), sourceBytes);
        footage.close();

        // The application displays errors modally; dismiss before subsequent GUI tests.
        QTRY_VERIFY(m_window->property("dialogOpen").toBool());
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!m_window->property("dialogOpen").toBool());
        QCOMPARE(m_app->project()->project(), before);
        QCOMPARE(m_app->project()->isModified(), dirty);
        QCOMPARE(m_app->project()->undoText(), undoLabel);
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
