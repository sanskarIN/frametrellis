// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QSortFilterProxyModel>

namespace ft::ui {

/// The project's media files as a list for the media bin.
class MediaBinModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        DurationRole,
        DetailsRole,
        StatusRole,
        FilePathRole,
        ThumbnailRole,
        UsageRole,
        TooltipRole,
    };
    Q_ENUM(Role)

    explicit MediaBinModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int missingCount() const;

    /// Rebuilds the rows from the project (media lists are short, so a reset is cheap).
    void refresh(const Project& project);
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    struct Row
    {
        MediaAsset asset;
        int usage = 0;
        QString duration;
    };
    std::vector<Row> m_rows;
    int m_revision = 0;
};

/// Keeps the media bin responsive by filtering model rows before QML creates tiles.
/// Filtering does not change or reorder the project's actual media collection.
class MediaBinFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit MediaBinFilterModel(QObject* parent = nullptr);

    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString& text);
    [[nodiscard]] QString category() const;
    void setCategory(const QString& category);
    [[nodiscard]] int count() const;

signals:
    void searchTextChanged();
    void categoryChanged();
    void countChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;

private:
    QString m_searchText;
    QString m_category = QStringLiteral("all");
};

QString formatDuration(std::int64_t microseconds);
QString describeMedia(const MediaAsset& asset);

} // namespace ft::ui
