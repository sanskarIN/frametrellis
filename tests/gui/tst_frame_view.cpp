// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// Qt Quick node factories return nullptr before the scene graph initializes.
// A FrameView detached from its window or rendered during startup/teardown
// must never dereference those results.
#include "ui/frame_view.h"

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

    void sceneGraphNotInitializedDoesNotCreateNodes()
    {
        QQuickWindow window;
        QVERIFY(!window.isSceneGraphInitialized());
        ExposedFrameView view;
        view.setParentItem(window.contentItem());
        QVERIFY(view.window() == &window);
        QVERIFY(view.updatePaintNode(nullptr, nullptr) == nullptr);
        QVERIFY(!window.isSceneGraphInitialized());
    }
};

QTEST_MAIN(FrameViewTest)
#include "tst_frame_view.moc"
