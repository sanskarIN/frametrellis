// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/media_asset.h"

#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

namespace ft::media {

/// Probes files on a background thread pool and reports the results on the owner's thread.
///
/// Each project generation shares a cancellation token. cancelAll() invalidates it before
/// notifying observers; workers stop before the next file and stale completions are discarded.
/// Import/cancel operations and signal delivery must occur on the importer's owning thread.
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
    void finished(const std::vector<ft::MediaAsset>& assets,
                  const QList<ft::media::MediaImporter::Failure>& failures);
    void pendingCountChanged(int pending);

private:
    std::shared_ptr<std::atomic<bool>> m_cancelled = std::make_shared<std::atomic<bool>>(false);
    int m_pending = 0;
};

} // namespace ft::media
