// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "media/media_importer.h"
#include "ui/media_bin_model.h"
#include "ui/thumbnail_provider.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>

#include <memory>

namespace ft::ui {

class ProjectController;

/// Media bin actions: importing (probed in the background), removing and relinking media.
class MediaController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(ft::ui::MediaBinModel* model READ model CONSTANT)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(int pendingImports READ pendingImports NOTIFY importingChanged)
    Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)

public:
    MediaController(ProjectController* project, std::shared_ptr<ThumbnailRegistry> registry,
                    QObject* parent = nullptr);

    [[nodiscard]] MediaBinModel* model() { return &m_model; }
    [[nodiscard]] bool importing() const { return m_importer.pendingCount() > 0; }
    [[nodiscard]] int pendingImports() const { return m_importer.pendingCount(); }
    [[nodiscard]] QStringList nameFilters() const;

    /// Accepts file URLs or local paths. Folders are not expanded.
    Q_INVOKABLE void importFiles(const QVariantList& urls);
    /// Recursively import up to 500 supported files from a selected folder.
    Q_INVOKABLE bool importFolder(const QString& urlOrPath);
    /// Save a transparent PNG alongside the project and import it into the media bin.
    /// Returns the new asset id, or an empty string on failure.
    Q_INVOKABLE QString createTitle(const QString& text, int fontPixels, const QString& color);
    Q_INVOKABLE void remove(const QString& assetId);
    Q_INVOKABLE bool relink(const QString& assetId, const QString& urlOrPath);
    Q_INVOKABLE void rescan();
    /// Save a read-only media inventory JSON without modifying project or source files.
    Q_INVOKABLE bool exportMediaAudit(const QString& urlOrPath);
    /// Opens the folder that contains the asset's file in the system file manager.
    Q_INVOKABLE void revealInFolder(const QString& assetId) const;

signals:
    void importingChanged();
    void imported(int count);
    void importProblems(const QString& title, const QString& message);
    void statusMessage(const QString& message);

private:
    void refresh();
    void onImported(const std::vector<MediaAsset>& assets,
                    const QList<media::MediaImporter::Failure>& failures);

    ProjectController* m_project;
    std::shared_ptr<ThumbnailRegistry> m_registry;
    MediaBinModel m_model;
    media::MediaImporter m_importer;
};

} // namespace ft::ui
