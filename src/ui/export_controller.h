// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "rendering/render_job.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace ft::ui {

class ProjectController;

/// Drives the export dialog: presets, validation and the background render job.
class ExportController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY stateChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)
    Q_PROPERTY(QString encoder READ encoder NOTIFY progressChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QStringList speedPresets READ speedPresets CONSTANT)

public:
    ExportController(ProjectController* project, QString helperProgram, QObject* parent = nullptr);
    ~ExportController() override;

    [[nodiscard]] QString state() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] QString progressText() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString errorDetail() const;
    [[nodiscard]] QString outputPath() const;
    [[nodiscard]] QString encoder() const;
    [[nodiscard]] QVariantList presets() const;
    [[nodiscard]] QStringList speedPresets() const;

    /// settings: presetId, outputPath (path or file URL), crf, speedPreset, audioBitrateKbps,
    /// includeAudio. Returns an empty string when valid, otherwise a user-facing reason.
    Q_INVOKABLE QString validate(const QVariantMap& settings) const;
    Q_INVOKABLE QString describeOutput(const QVariantMap& settings) const;
    Q_INVOKABLE bool start(const QVariantMap& settings);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString suggestedOutputPath() const;
    Q_INVOKABLE bool fileExists(const QString& pathOrUrl) const;
    Q_INVOKABLE void revealOutput() const;
    Q_INVOKABLE void reset();

signals:
    void stateChanged();
    void progressChanged();
    void finished(bool success, const QString& message);

private:
    [[nodiscard]] rendering::ExportSettings toSettings(const QVariantMap& map) const;

    ProjectController* m_project;
    QString m_helperProgram;
    std::unique_ptr<rendering::RenderJob> m_job;
    QTimer m_tick;
};

} // namespace ft::ui
