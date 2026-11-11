// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "core/logging.h"
#include "core/version.h"
#include "engine/mlt_runtime.h"
#include "ui/app_controller.h"
#include "ui/log_file.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(FrameTrellisPlugin)

namespace {

// Optional, local-only smoke-test checkpoints. Windows GUI-subsystem executables
// may produce no usable stdout/stderr when startup stalls; these markers preserve
// the last reached phase without relying on the Qt message handler or event loop.
void smokeCheckpoint(const QString& phase)
{
    const QString path = qEnvironmentVariable("FT_GUI_SMOKE_LOG");
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.write(QStringLiteral("[%1] %2\n")
                   .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), phase)
                   .toUtf8());
    file.flush();
}

QString renderHelperPath()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("frametrellis-render.exe");
#else
    const QString name = QStringLiteral("frametrellis-render");
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString adjacent = appDir.filePath(name);
#ifdef Q_OS_MACOS
    // CMake packs the helper inside Contents/MacOS, but support older
    // unbundled developer builds where it was emitted to bin/ instead.
    if (!QFileInfo::exists(adjacent)) {
        const QString developerHelper =
            QDir::cleanPath(appDir.filePath(QStringLiteral("../../../") + name));
        if (QFileInfo(developerHelper).isFile()) {
            return developerHelper;
        }
    }
#endif
    return adjacent;
}

} // namespace

int main(int argc, char* argv[])
{
    smokeCheckpoint(QStringLiteral("main entered"));
    QCoreApplication::setOrganizationName(QLatin1String(ft::version::organizationName));
    QCoreApplication::setOrganizationDomain(QLatin1String(ft::version::organizationDomain));
    QCoreApplication::setApplicationName(QLatin1String(ft::version::applicationName));
    QCoreApplication::setApplicationVersion(QLatin1String(ft::version::string));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

    QGuiApplication app(argc, argv);
    smokeCheckpoint(QStringLiteral("QGuiApplication initialized"));
    QGuiApplication::setWindowIcon(
        QIcon(QStringLiteral(":/qt/qml/FrameTrellis/icons/app-icon.svg")));
    QGuiApplication::setDesktopFileName(QStringLiteral("frametrellis"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FrameTrellis video editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QStringLiteral("Project file (.ftproj) to open."),
                                 QStringLiteral("[project]"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a picture of the main window to <file> after loading, then quit (used "
                       "for documentation)."),
        QStringLiteral("file"));
    const QCommandLineOption delayOption(
        QStringLiteral("screenshot-delay"),
        QStringLiteral("Milliseconds to wait before taking the screenshot."), QStringLiteral("ms"),
        QStringLiteral("2500"));
    parser.addOptions({screenshotOption, delayOption});
    parser.process(app);
    smokeCheckpoint(QStringLiteral("command line parsed"));

    ft::ui::installFileLogger();
    smokeCheckpoint(QStringLiteral("Qt file logger installed"));
    qCInfo(lcUi) << "FrameTrellis" << ft::version::string << "starting";

    const ft::Status engineStatus = ft::engine::MltRuntime::initialize();
    smokeCheckpoint(QStringLiteral("MLT initialized"));
    int exitCode = 0;
    {
        ft::ui::AppController controller(engineStatus, renderHelperPath());
        {
            QQmlApplicationEngine engine;
            engine.addImageProvider(QStringLiteral("ft-thumbnail"),
                                    controller.createThumbnailProvider());
            engine.addImageProvider(QStringLiteral("ft-waveform"),
                                    controller.createWaveformProvider());
            engine.setInitialProperties(
                {{QStringLiteral("app"), QVariant::fromValue(&controller)}});
            QObject::connect(
                &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
            qCInfo(lcUi) << "Loading QML main window";
            engine.loadFromModule("FrameTrellis", "Main");
            smokeCheckpoint(QStringLiteral("QML load completed"));
            if (engine.rootObjects().isEmpty()) {
                // Do not return here: even failed QML startup must destroy controllers,
                // join background work and shut MLT down in the usual order.
                qCCritical(lcUi) << "QML main window creation failed";
                exitCode = 1;
            } else {
                qCInfo(lcUi) << "QML main window created";
                // Distinguish the actual Qt Quick graphics backend in a native Windows
                // crash report. A software-versus-default comparison is otherwise
                // ambiguous when the app exits before its screenshot callback.
                qCInfo(lcUi) << "Qt Quick graphics API"
                             << static_cast<int>(QQuickWindow::graphicsApi()) << "backend override"
                             << qEnvironmentVariable("QT_QUICK_BACKEND", QStringLiteral("default"));
                const QStringList positional = parser.positionalArguments();
                if (!positional.isEmpty()) {
                    smokeCheckpoint(QStringLiteral("opening project"));
                    const bool opened = controller.project()->openProject(
                        QFileInfo(positional.first()).absoluteFilePath());
                    smokeCheckpoint(opened ? QStringLiteral("project opened")
                                           : QStringLiteral("project open failed"));
                }
                if (parser.isSet(screenshotOption)) {
                    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                    const QString target = parser.value(screenshotOption);
                    QTimer::singleShot(parser.value(delayOption).toInt(), &app, [window, target] {
                        smokeCheckpoint(QStringLiteral("screenshot callback entered"));
                        const QImage capture = window ? window->grabWindow() : QImage{};
                        smokeCheckpoint(QStringLiteral("window grab returned"));
                        const bool saved = !capture.isNull() && capture.save(target);
                        smokeCheckpoint(saved ? QStringLiteral("screenshot saved")
                                              : QStringLiteral("screenshot failed"));
                        qCInfo(lcUi) << "Screenshot" << target << (saved ? "saved" : "failed");
                        QCoreApplication::exit(saved ? 0 : 2);
                    });
                    smokeCheckpoint(QStringLiteral("screenshot timer scheduled"));
                }
                smokeCheckpoint(QStringLiteral("entering GUI event loop"));
                exitCode = QGuiApplication::exec();
                smokeCheckpoint(QStringLiteral("GUI event loop returned"));
                qCInfo(lcUi) << "Qt event loop stopped with exit code" << exitCode;
            }
            qCInfo(lcUi) << "Destroying QML engine";
        } // the QML engine goes first: it references the controllers
        qCInfo(lcUi) << "Destroying application controllers";
    } // controllers next: playback and export stop their threads and processes
    qCInfo(lcUi) << "Waiting for outstanding worker tasks";
    smokeCheckpoint(QStringLiteral("waiting for worker tasks"));
    QThreadPool::globalInstance()->waitForDone(); // pending probes and thumbnails use MLT
    qCInfo(lcUi) << "Shutting down MLT";
    ft::engine::MltRuntime::shutdown();
    smokeCheckpoint(QStringLiteral("shutdown complete"));
    qCInfo(lcUi) << "Startup and shutdown sequence finished with code" << exitCode;
    return exitCode;
}
