// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickItem>

namespace ft::ui {

class PlaybackController;

/// Shows the playback engine's frames, aspect-fitted, using the scene graph so it works with
/// every Qt Quick graphics backend (Direct3D, Vulkan, Metal, OpenGL and software).
class FrameView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ft::ui::PlaybackController* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QRectF pictureRect READ pictureRect NOTIFY pictureRectChanged)

public:
    explicit FrameView(QQuickItem* parent = nullptr);

    [[nodiscard]] PlaybackController* source() const { return m_source; }
    void setSource(PlaybackController* source);
    [[nodiscard]] QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor& color);
    [[nodiscard]] QRectF pictureRect() const { return m_pictureRect; }

signals:
    void sourceChanged();
    void backgroundColorChanged();
    void pictureRectChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onFrame();
    void updatePictureRect();

    QPointer<PlaybackController> m_source;
    QColor m_background{QStringLiteral("#0b0d10")};
    QImage m_image;
    QSize m_aspect{16, 9};
    QRectF m_pictureRect;
    bool m_imageDirty = false;
};

} // namespace ft::ui
