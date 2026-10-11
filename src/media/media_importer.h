// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/media_asset.h"

#include <QObject>
#include <QStringList>

#include <vector>

namespace ft::media {

/// Probes files on a background thread pool and reports the results on the owner's thread.
///
/// Every batch carries a generation number; cancelAll() (called when a project is closed)
/// increments it, so results that arrive afterwards are discarded instead of being added to the
/// wrong project.
class MediaImporter : public QObject
{
    Q_OBJECT

public:
    explicit MediaImporter(QObject* parent = nullptr);
    ~MediaImporter() override;

    void import(const QStringList& filePaths);
    void cancelAll();
    [[nodiscard]] int pendingCount() const { return m_pending; }

    struct Failure
    {
        QString filePath;
        QString message;
    };

signals:
    void finished(const std::vector<ft::MediaAsset>& assets, const QList<ft::media::MediaImporter::Failure>& failures);
    void pendingCountChanged(int pending);

private:
    int m_generation = 0;
    int m_pending = 0;
};

} // namespace ft::media
