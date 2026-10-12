// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/app_controller.h"

#include "core/logging.h"
#include "core/version.h"
#include "engine/mlt_runtime.h"
#include "playback/audio_output.h"
#include "ui/log_file.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QUrl>

namespace ft::ui {

AppController::AppController(const Status& engineStatus, const QString& renderHelper,
                             QObject* parent)
    : QObject(parent)
    , m_engineReady(engineStatus.ok())
    , m_engineMessage(engineStatus.ok() ? QString() : engineStatus.error().toString())
    , m_thumbnails(std::make_shared<ThumbnailRegistry>())
    , m_thumbnailCache(
          std::make_shared<media::ThumbnailCache>(media::ThumbnailCache::defaultDirectory()))
    , m_waveforms(std::make_shared<WaveformService>(
          m_thumbnails,
          std::make_shared<media::WaveformCache>(media::WaveformCache::defaultDirectory())))
{
    QSettings settings;
    m_firstRun = !settings.value(QStringLiteral("app/firstRunDone"), false).toBool();
    m_theme = settings.value(QStringLiteral("app/theme"), QStringLiteral("dark")).toString();

    m_project = std::make_unique<ProjectController>();
    m_media = std::make_unique<MediaController>(m_project.get(), m_thumbnails);
    m_playback = std::make_unique<PlaybackController>(m_project.get());
    m_timeline = std::make_unique<TimelineController>(m_project.get(), m_playback.get());
    m_exporter = std::make_unique<ExportController>(m_project.get(), renderHelper);

    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(7000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        m_status.clear();
        emit statusMessageChanged();
    });
    connect(m_project.get(), &ProjectController::statusMessage, this, &AppController::showStatus);
    connect(m_media.get(), &MediaController::statusMessage, this, &AppController::showStatus);
    connect(m_timeline.get(), &TimelineController::statusMessage, this, &AppController::showStatus);
    connect(m_project.get(), &ProjectController::errorOccurred, this,
            &AppController::errorOccurred);
    connect(m_media.get(), &MediaController::importProblems, this, &AppController::errorOccurred);
    connect(m_playback.get(), &PlaybackController::errorOccurred, this,
            [this](const QString& message) {
                showStatus(QStringLiteral("Preview problem: %1").arg(message));
            });
    connect(m_exporter.get(), &ExportController::finished, this,
            [this](bool, const QString& message) { showStatus(message); });
    m_thumbnailCache->prune(60);
}

AppController::~AppController()
{
    m_waveforms->shutdown(); // background waveform work stops early
    m_exporter.reset();
    m_timeline.reset();
    m_playback.reset();
    m_media.reset();
    m_project.reset();
}

QString AppController::version() const
{
    return QLatin1String(ft::version::string);
}

void AppController::showStatus(const QString& message)
{
    m_status = message;
    emit statusMessageChanged();
    m_statusTimer.start();
}

QString AppController::localPath(const QVariant& urlOrPath) const
{
    const QString text =
        urlOrPath.typeId() == QMetaType::QUrl ? urlOrPath.toUrl().toString() : urlOrPath.toString();
    return QDir::toNativeSeparators(ProjectController::localPath(text));
}

QUrl AppController::fileUrl(const QString& path) const
{
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(QDir::fromNativeSeparators(path));
}

QVariantList AppController::links() const
{
    // Only links verified to belong to the maintainer are listed here (see docs/maintainers.md).
    auto link = [](const QString& name, const QString& url, const QString& group) {
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("url"), url},
                           {QStringLiteral("group"), group}};
    };
    return {
        link(QStringLiteral("Website"), QStringLiteral("https://sanskarin.github.io/frametrellis/"),
             QStringLiteral("project")),
        link(QStringLiteral("Source code"), QLatin1String(ft::version::repository),
             QStringLiteral("project")),
        link(QStringLiteral("Report an issue"),
             QStringLiteral("https://github.com/sanskarIN/frametrellis/issues"),
             QStringLiteral("project")),
        link(QStringLiteral("GitHub"), QStringLiteral("https://github.com/sanskarIN"),
             QStringLiteral("maintainer")),
        link(QStringLiteral("Personal website"), QStringLiteral("https://sanskarin.github.io"),
             QStringLiteral("maintainer")),
        link(QStringLiteral("Contact"), QStringLiteral("https://sanskarin.github.io/contact"),
             QStringLiteral("maintainer")),
    };
}

QVariantMap AppController::about() const
{
    const auto& caps = engine::MltRuntime::capabilities();
    return {
        {QStringLiteral("version"), version()},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("mlt"),
         caps.mltVersion.isEmpty() ? QStringLiteral("not available") : caps.mltVersion},
        {QStringLiteral("audio"), playback::audioBackendName().isEmpty()
                                      ? QStringLiteral("none (silent preview)")
                                      : playback::audioBackendName()},
        {QStringLiteral("platform"), QSysInfo::prettyProductName()},
        {QStringLiteral("license"), QStringLiteral("GPL-3.0-or-later")},
    };
}

void AppController::openUrl(const QString& url) const
{
    const QUrl target(url);
    // Only web links and local folders are opened; never arbitrary schemes.
    if (target.scheme() == QLatin1String("https") || target.isLocalFile()) {
        QDesktopServices::openUrl(target);
    }
}

void AppController::openLogFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(logFilePath()).absolutePath()));
}

QString AppController::diagnostics() const
{
    const auto& caps = engine::MltRuntime::capabilities();
    QStringList lines;
    lines << QStringLiteral("FrameTrellis %1").arg(version())
          << QStringLiteral("Qt %1, %2")
                 .arg(QString::fromLatin1(qVersion()), QSysInfo::prettyProductName())
          << QStringLiteral("CPU: %1, %2 threads")
                 .arg(QSysInfo::currentCpuArchitecture())
                 .arg(QThread::idealThreadCount())
          << QStringLiteral("MLT %1 (%2)").arg(caps.mltVersion, caps.repositoryPath)
          << QStringLiteral("Compositor: %1")
                 .arg(caps.compositor.isEmpty() ? QStringLiteral("none") : caps.compositor)
          << QStringLiteral("Picture adjustments: %1")
                 .arg(caps.eqFilter ? QStringLiteral("available") : QStringLiteral("unavailable"))
          << QStringLiteral("Audio output: %1")
                 .arg(m_playback->audioAvailable() ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("Engine status: %1")
                 .arg(m_engineReady ? QStringLiteral("ready") : m_engineMessage)
          << QStringLiteral("Log file: %1").arg(logFilePath());
    const QStringList optional = caps.missingOptional();
    if (!optional.isEmpty()) {
        lines << QStringLiteral("Unavailable optional services: %1")
                     .arg(optional.join(QStringLiteral("; ")));
    }
    return lines.join(QLatin1Char('\n'));
}

void AppController::copyText(const QString& text) const
{
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void AppController::finishFirstRun()
{
    if (m_firstRun) {
        m_firstRun = false;
        QSettings().setValue(QStringLiteral("app/firstRunDone"), true);
        emit firstRunChanged();
    }
}

void AppController::setTheme(const QString& theme)
{
    const QString value =
        theme == QLatin1String("light") ? QStringLiteral("light") : QStringLiteral("dark");
    if (value != m_theme) {
        m_theme = value;
        QSettings().setValue(QStringLiteral("app/theme"), value);
        emit themeChanged();
    }
}

QQuickAsyncImageProvider* AppController::createThumbnailProvider() const
{
    return new ThumbnailProvider(m_thumbnails, m_thumbnailCache);
}

QQuickAsyncImageProvider* AppController::createWaveformProvider() const
{
    return new WaveformProvider(m_waveforms);
}

void AppController::prepareQuit(bool keepAutosave)
{
    m_waveforms->shutdown();
    m_playback->pause();
    m_exporter->cancel();
    if (!keepAutosave) {
        m_project->removeOwnAutosave();
    }
}

} // namespace ft::ui
