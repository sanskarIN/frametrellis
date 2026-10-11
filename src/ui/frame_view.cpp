// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/frame_view.h"

#include "ui/playback_controller.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>

namespace ft::ui {

FrameView::FrameView(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void FrameView::setSource(PlaybackController* source)
{
    if (source == m_source) {
        return;
    }
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    if (m_source) {
        connect(m_source, &PlaybackController::frameReady, this, &FrameView::onFrame);
        onFrame();
    }
    emit sourceChanged();
}

void FrameView::setBackgroundColor(const QColor& color)
{
    if (color == m_background) {
        return;
    }
    m_background = color;
    emit backgroundColorChanged();
    update();
}

void FrameView::onFrame()
{
    if (!m_source) {
        return;
    }
    m_image = m_source->currentFrame();
    if (!m_image.isNull() && m_image.size() != m_aspect) {
        m_aspect = m_image.size();
        updatePictureRect();
    }
    m_imageDirty = true;
    update();
}

void FrameView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updatePictureRect();
    update();
}

void FrameView::updatePictureRect()
{
    const QSizeF scaled = QSizeF(m_aspect).scaled(size(), Qt::KeepAspectRatio);
    const QRectF rect(QPointF((width() - scaled.width()) / 2.0, (height() - scaled.height()) / 2.0),
                      scaled);
    if (rect != m_pictureRect) {
        m_pictureRect = rect;
        emit pictureRectChanged();
    }
}

QSGNode* FrameView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* background = static_cast<QSGRectangleNode*>(old);
    if (!background) {
        background = window()->createRectangleNode();
    }
    background->setRect(boundingRect());
    background->setColor(m_background);

    auto* picture = static_cast<QSGImageNode*>(background->firstChild());
    if (m_image.isNull()) {
        if (picture) {
            background->removeChildNode(picture);
            delete picture;
        }
        return background;
    }
    if (!picture) {
        // Configure the node completely before it joins the scene graph: the software renderer
        // inspects nodes as soon as they are added and cannot handle one without a texture.
        picture = window()->createImageNode();
        picture->setOwnsTexture(true);
        picture->setFiltering(QSGTexture::Linear);
        picture->setTexture(window()->createTextureFromImage(m_image));
        picture->setRect(m_pictureRect);
        m_imageDirty = false;
        background->appendChildNode(picture);
        return background;
    }
    if (m_imageDirty) {
        picture->setTexture(window()->createTextureFromImage(m_image));
        m_imageDirty = false;
    }
    picture->setRect(m_pictureRect);
    return background;
}

} // namespace ft::ui
