// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/media_bin_model.h"

#include <QFileInfo>
#include <QLocale>
#include <QtGlobal>

namespace ft::ui {

QString formatDuration(std::int64_t microseconds)
{
    const qint64 totalSeconds = microseconds / 1'000'000;
    const qint64 tenths = (microseconds % 1'000'000) / 100'000;
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds / 60) % 60;
    const qint64 s = totalSeconds % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2.%3").arg(m).arg(s, 2, 10, QLatin1Char('0')).arg(tenths);
}

QString describeMedia(const MediaAsset& asset)
{
    const MediaInfo& i = asset.info;
    QStringList parts;
    if (i.hasVideo) {
        parts << QStringLiteral("%1×%2").arg(i.width).arg(i.height);
    }
    if (i.kind == MediaKind::Video && i.frameRate.isValid()) {
        parts << QStringLiteral("%1 fps").arg(QString::number(i.frameRate.toDouble(), 'g', 4));
    }
    if (!i.videoCodec.isEmpty() && i.kind != MediaKind::Audio) {
        parts << i.videoCodec.toUpper();
    }
    if (i.hasAudio) {
        parts << QStringLiteral("%1 kHz %2")
                     .arg(QString::number(i.audioSampleRate / 1000.0, 'g', 3),
                          i.audioChannels == 1   ? QStringLiteral("mono")
                          : i.audioChannels == 2 ? QStringLiteral("stereo")
                                                 : QStringLiteral("%1 ch").arg(i.audioChannels));
    }
    return parts.join(QStringLiteral(" · "));
}

MediaBinModel::MediaBinModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int MediaBinModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int MediaBinModel::missingCount() const
{
    return static_cast<int>(std::count_if(m_rows.begin(), m_rows.end(), [](const Row& r) {
        return r.asset.status == MediaStatus::Missing;
    }));
}

QVariant MediaBinModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const Row& row = m_rows[static_cast<size_t>(index.row())];
    const MediaAsset& a = row.asset;
    switch (role) {
    case IdRole:
        return a.id;
    case NameRole:
    case Qt::DisplayRole:
        return a.name;
    case KindRole:
        return mediaKindToString(a.info.kind);
    case DurationRole:
        return row.duration;
    case DetailsRole:
        return describeMedia(a);
    case StatusRole:
        switch (a.status) {
        case MediaStatus::Missing:
            return QStringLiteral("missing");
        case MediaStatus::Changed:
            return QStringLiteral("changed");
        default:
            return QStringLiteral("online");
        }
    case FilePathRole:
        return a.filePath;
    case ThumbnailRole:
        return a.info.hasVideo && a.status != MediaStatus::Missing
                   ? QStringLiteral("image://ft-thumbnail/%1?r=%2").arg(a.id).arg(m_revision)
                   : QString();
    case UsageRole:
        return row.usage;
    case TooltipRole: {
        QString tip = a.filePath + QLatin1Char('\n') + describeMedia(a);
        if (a.status == MediaStatus::Missing) {
            tip += QStringLiteral("\nOffline: the file is missing. Use Relink to locate it.");
        } else if (a.status == MediaStatus::Changed) {
            tip += QStringLiteral("\nThe file changed since it was imported.");
        }
        return tip;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> MediaBinModel::roleNames() const
{
    return {{IdRole, "assetId"},        {NameRole, "name"},           {KindRole, "kind"},
            {DurationRole, "duration"}, {DetailsRole, "details"},     {StatusRole, "status"},
            {FilePathRole, "filePath"}, {ThumbnailRole, "thumbnail"}, {UsageRole, "usage"},
            {TooltipRole, "tooltip"}};
}

void MediaBinModel::refresh(const Project& project)
{
    beginResetModel();
    m_rows.clear();
    ++m_revision;
    for (const MediaAsset& asset : project.media()) {
        Row row;
        row.asset = asset;
        row.usage = static_cast<int>(project.timeline().clipsUsingAsset(asset.id).size());
        row.duration = asset.info.kind == MediaKind::Image ? QStringLiteral("Still")
                                                           : formatDuration(asset.info.durationUs);
        m_rows.push_back(std::move(row));
    }
    endResetModel();
    emit countChanged();
}

QVariantMap MediaBinModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount()) {
        return map;
    }
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }
    return map;
}

MediaBinFilterModel::MediaBinFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &MediaBinFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &MediaBinFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &MediaBinFilterModel::countChanged);
}

QString MediaBinFilterModel::searchText() const
{
    return m_searchText;
}

void MediaBinFilterModel::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed();
    if (m_searchText == normalized) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_searchText = normalized;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    emit searchTextChanged();
}

QString MediaBinFilterModel::category() const
{
    return m_category;
}

void MediaBinFilterModel::setCategory(const QString& category)
{
    const QString normalized = category.toLower();
    const QString next =
        (normalized == QLatin1String("video") || normalized == QLatin1String("audio") ||
         normalized == QLatin1String("image") || normalized == QLatin1String("offline"))
            ? normalized
            : QStringLiteral("all");
    if (m_category == next) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_category = next;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    emit categoryChanged();
}

int MediaBinFilterModel::count() const
{
    return rowCount();
}

bool MediaBinFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QAbstractItemModel* model = sourceModel();
    if (!model) {
        return false;
    }
    const QModelIndex sourceIndex = model->index(sourceRow, 0, sourceParent);
    if (!sourceIndex.isValid()) {
        return false;
    }
    if (m_category == QLatin1String("offline")) {
        if (model->data(sourceIndex, MediaBinModel::StatusRole).toString() !=
            QLatin1String("missing")) {
            return false;
        }
    } else if (m_category != QLatin1String("all") &&
               model->data(sourceIndex, MediaBinModel::KindRole).toString() != m_category) {
        return false;
    }
    if (m_searchText.isEmpty()) {
        return true;
    }
    return model->data(sourceIndex, MediaBinModel::NameRole)
               .toString()
               .contains(m_searchText, Qt::CaseInsensitive) ||
           model->data(sourceIndex, MediaBinModel::FilePathRole)
               .toString()
               .contains(m_searchText, Qt::CaseInsensitive);
}

} // namespace ft::ui
