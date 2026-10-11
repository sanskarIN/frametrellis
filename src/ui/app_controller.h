// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "ui/export_controller.h"
#include "ui/media_controller.h"
#include "ui/playback_controller.h"
#include "ui/project_controller.h"
#include "ui/thumbnail_provider.h"
#include "ui/timeline_controller.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

#include <memory>

class QQuickAsyncImageProvider;

namespace ft::ui {

/// Root object handed to QML as the "app" property. It composes the feature controllers and
/// owns them in an order that makes shutdown safe (export and playback stop first).
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(ft::ui::ProjectController* project READ project CONSTANT)
    Q_PROPERTY(ft::ui::MediaController* media READ media CONSTANT)
    Q_PROPERTY(ft::ui::TimelineController* timeline READ timeline CONSTANT)
    Q_PROPERTY(ft::ui::PlaybackController* playback READ playback CONSTANT)
    Q_PROPERTY(ft::ui::ExportController* exporter READ exporter CONSTANT)
    Q_PROPERTY(bool engineReady READ engineReady CONSTANT)
    Q_PROPERTY(QString engineMessage READ engineMessage CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList links READ links CONSTANT)
    Q_PROPERTY(QVariantMap about READ about CONSTANT)
    Q_PROPERTY(bool firstRun READ firstRun NOTIFY firstRunChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)

public:
    AppController(const Status& engineStatus, const QString& renderHelper,
                  QObject* parent = nullptr);
    ~AppController() override;

    [[nodiscard]] ProjectController* project() const { return m_project.get(); }
    [[nodiscard]] MediaController* media() const { return m_media.get(); }
    [[nodiscard]] TimelineController* timeline() const { return m_timeline.get(); }
    [[nodiscard]] PlaybackController* playback() const { return m_playback.get(); }
    [[nodiscard]] ExportController* exporter() const { return m_exporter.get(); }
    [[nodiscard]] bool engineReady() const { return m_engineReady; }
    [[nodiscard]] QString engineMessage() const { return m_engineMessage; }
    [[nodiscard]] QString version() const;
    [[nodiscard]] QString statusMessage() const { return m_status; }
    [[nodiscard]] QVariantList links() const;
    [[nodiscard]] QVariantMap about() const;
    [[nodiscard]] bool firstRun() const { return m_firstRun; }
    [[nodiscard]] QString theme() const { return m_theme; }
    void setTheme(const QString& theme);

    /// Image provider for "image://ft-thumbnail/..."; the QML engine takes ownership.
    [[nodiscard]] QQuickAsyncImageProvider* createThumbnailProvider() const;

    Q_INVOKABLE void showStatus(const QString& message);
    /// Converts a file URL (from a file dialog or drop) to a local path, and back.
    Q_INVOKABLE QString localPath(const QVariant& urlOrPath) const;
    Q_INVOKABLE QUrl fileUrl(const QString& path) const;
    Q_INVOKABLE void openUrl(const QString& url) const;
    Q_INVOKABLE void openLogFolder() const;
    Q_INVOKABLE QString diagnostics() const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void finishFirstRun();
    /// Called when the main window closes after the user decided about unsaved changes.
    Q_INVOKABLE void prepareQuit(bool keepAutosave);

signals:
    void statusMessageChanged();
    void errorOccurred(const QString& title, const QString& message);
    void firstRunChanged();
    void themeChanged();

private:
    bool m_engineReady;
    QString m_engineMessage;
    QString m_status;
    QTimer m_statusTimer;
    bool m_firstRun = true;
    QString m_theme;
    std::shared_ptr<ThumbnailRegistry> m_thumbnails;
    std::shared_ptr<media::ThumbnailCache> m_thumbnailCache;
    // Declaration order is destruction order in reverse: export, then timeline, playback, media,
    // and the project last.
    std::unique_ptr<ProjectController> m_project;
    std::unique_ptr<MediaController> m_media;
    std::unique_ptr<PlaybackController> m_playback;
    std::unique_ptr<TimelineController> m_timeline;
    std::unique_ptr<ExportController> m_exporter;
};

} // namespace ft::ui
