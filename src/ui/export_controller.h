// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "rendering/render_job.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>
#include <optional>

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
    Q_PROPERTY(bool batchActive READ batchActive NOTIFY batchChanged)
    Q_PROPERTY(int batchTotal READ batchTotal NOTIFY batchChanged)
    Q_PROPERTY(int batchCompleted READ batchCompleted NOTIFY batchChanged)
    Q_PROPERTY(double batchProgress READ batchProgress NOTIFY progressChanged)
    Q_PROPERTY(QString batchState READ batchState NOTIFY batchChanged)
    Q_PROPERTY(QString batchText READ batchText NOTIFY progressChanged)

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
    [[nodiscard]] bool batchActive() const { return m_batchActive; }
    [[nodiscard]] int batchTotal() const { return static_cast<int>(m_batchJobs.size()); }
    [[nodiscard]] int batchCompleted() const { return m_batchCompleted; }
    [[nodiscard]] double batchProgress() const;
    [[nodiscard]] QString batchState() const { return m_batchState; }
    [[nodiscard]] QString batchText() const;

    /// settings: presetId, outputPath (path or file URL), crf, speedPreset, audioBitrateKbps,
    /// includeAudio, frameRate (match, integer, decimal or rational), customWidth (0 or even pixels).
    /// Returns an empty string when valid, otherwise a user-facing reason.
    Q_INVOKABLE QString validate(const QVariantMap& settings) const;
    Q_INVOKABLE QString describeOutput(const QVariantMap& settings) const;
    Q_INVOKABLE bool start(const QVariantMap& settings);
    /// Sequentially render 2–5 distinct export presets from one frozen project snapshot.
    /// Uses output-base + "-<preset-id>.mp4" and refuses any existing output.
    /// Each preset keeps its own quality/scaling/speed while inheriting FPS/range/audio choice.
    Q_INVOKABLE QString validateBatch(const QVariantMap& settings,
                                      const QStringList& presetIds) const;
    Q_INVOKABLE bool startBatch(const QVariantMap& settings, const QStringList& presetIds);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString suggestedOutputPath() const;
    Q_INVOKABLE bool fileExists(const QString& pathOrUrl) const;
    Q_INVOKABLE void revealOutput() const;
    Q_INVOKABLE bool copyFailureDetails() const;
    Q_INVOKABLE void reset();

signals:
    void stateChanged();
    void progressChanged();
    void batchChanged();
    void finished(bool success, const QString& message);

private:
    [[nodiscard]] rendering::ExportSettings toSettings(const QVariantMap& map) const;
    [[nodiscard]] Result<QVector<rendering::ExportSettings>>
    prepareBatch(const QVariantMap& settings, const QStringList& presetIds) const;
    bool startBatchJob();
    void finishBatch(rendering::RenderJob::State state, const QString& explicitError = {});

    ProjectController* m_project;
    QString m_helperProgram;
    std::unique_ptr<rendering::RenderJob> m_job;
    QTimer m_tick;
    QVector<rendering::ExportSettings> m_batchJobs;
    std::optional<Project> m_batchSnapshot;
    QString m_batchProjectFile;
    int m_batchCompleted = 0;
    bool m_batchActive = false;
    bool m_batchCancelRequested = false;
    QString m_batchState = QStringLiteral("idle");
    QString m_batchMessage;
};

} // namespace ft::ui
