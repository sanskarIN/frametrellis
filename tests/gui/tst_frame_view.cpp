// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// FrameView must not access Qt Quick scene graph factories without a window.
// Factories can also return nullptr during graphics initialization or teardown.
#include "ui/frame_view.h"
#include "ui/playback_controller.h"

#include <QQuickWindow>
#include <QTest>

using namespace ft::ui;

class ExposedFrameView : public FrameView
{
public:
    using FrameView::updatePaintNode;
};

class FrameViewTest : public QObject
{
    Q_OBJECT

private slots:
    void detachedViewNeverDereferencesWindow()
    {
        ExposedFrameView view;
        QVERIFY(view.window() == nullptr);
        view.setBackgroundColor(QColor(QStringLiteral("#303030")));
        QVERIFY(view.updatePaintNode(nullptr, nullptr) == nullptr);
        QVERIFY(view.updatePaintNode(nullptr, nullptr) == nullptr);
    }

    void detachingFromWindowAvoidsSceneGraphAccess()
    {
        QQuickWindow window;
        ExposedFrameView view;
        view.setParentItem(window.contentItem());
        QVERIFY(view.window() == &window);
        view.setParentItem(nullptr);
        QVERIFY(view.window() == nullptr);
        QVERIFY(view.updatePaintNode(nullptr, nullptr) == nullptr);
    }
};

QTEST_MAIN(FrameViewTest)
#include "tst_frame_view.moc"
