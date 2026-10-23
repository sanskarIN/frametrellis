// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Drives the real QML interface with simulated mouse and keyboard input. When the environment
// variable FT_SCREENSHOT_DIR is set, screenshots of the running application are saved there
// (they are used for the documentation).

#include "engine/mlt_runtime.h"
#include "media_fixtures.h"
#include "ui/app_controller.h"
#include "ui/title_image.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QtQml/qqmlextensionplugin.h>

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
        QList<QQmlError> errors;
        connect(m_engine.get(), &QQmlEngine::warnings, this,
                [&errors](const QList<QQmlError>& w) { errors << w; });
        m_engine->loadFromModule("FrameTrellis", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        m_window->resize(1600, 960);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QTest::qWait(200);
        for (const QQmlError& e : std::as_const(errors)) {
            qWarning() << e.toString();
        }
        QVERIFY2(errors.isEmpty(), "QML produced warnings while loading");
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

    void doubleClickingATileAppendsIt()
    {
        QQuickItem* grid = findItem(m_window->contentItem(), QStringLiteral("mediaGrid"));
        QVERIFY(grid);
        const int before = m_app->project()->project().timeline().clipCount();
        // The first tile sits at the top-left of the grid.
        const QPoint tile = grid->mapToScene(QPointF(60, 60)).toPoint();
        QTest::mouseDClick(m_window, Qt::LeftButton, Qt::NoModifier, tile);
        QVERIFY(QTest::qWaitFor(
            [&] { return m_app->project()->project().timeline().clipCount() == before + 1; },
            3000));
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

        QQuickItem* field =
            findItem(m_window->contentItem(), QStringLiteral("goToTimecodeField"));
        QVERIFY(field);
        field->setProperty("text", QStringLiteral("00:00:01:00"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "trySeek"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(static_cast<FrameCount>(m_app->playback()->position()), FrameCount{30});
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
