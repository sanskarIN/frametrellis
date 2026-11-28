// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project_bundle.h"

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

#include <memory>

namespace ft::ui {

class ProjectController;

class ProjectBundleController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged)

public:
    explicit ProjectBundleController(ProjectController* project, QObject* parent = nullptr);
    ~ProjectBundleController() override;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] bool cancelling() const { return m_cancelling; }
    [[nodiscard]] QVariantMap progress() const { return m_progress; }
    [[nodiscard]] QVariantMap result() const { return m_result; }

    Q_INVOKABLE bool createBundle(const QString& parentPath, const QString& folderName,
                                  bool usedOnly);
    Q_INVOKABLE bool verifyBundle(const QString& path);
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void progressChanged();

private:
    struct WorkState;
    using Work = std::function<Result<QVariantMap>(const std::shared_ptr<WorkState>&)>;
    bool start(Work work, const QString& operation);
    bool reject(const QString& message);
    void poll();
    void finished();

    ProjectController* m_project;
    std::shared_ptr<WorkState> m_work;
    QFutureWatcher<Result<QVariantMap>> m_watcher;
    QTimer m_timer;
    QString m_operation;
    QVariantMap m_progress;
    QVariantMap m_result;
    bool m_busy = false;
    bool m_cancelling = false;
};

} // namespace ft::ui
