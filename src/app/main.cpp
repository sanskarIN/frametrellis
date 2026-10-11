// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "core/logging.h"
#include "core/version.h"
#include "engine/mlt_runtime.h"
#include "ui/app_controller.h"
#include "ui/log_file.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(FrameTrellisPlugin)

namespace {

QString renderHelperPath()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("frametrellis-render.exe");
#else
    const QString name = QStringLiteral("frametrellis-render");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName(QLatin1String(ft::version::organizationName));
    QCoreApplication::setOrganizationDomain(QLatin1String(ft::version::organizationDomain));
    QCoreApplication::setApplicationName(QLatin1String(ft::version::applicationName));
    QCoreApplication::setApplicationVersion(QLatin1String(ft::version::string));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    }

    QGuiApplication app(argc, argv);
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

    ft::ui::installFileLogger();
    qCInfo(lcUi) << "FrameTrellis" << ft::version::string << "starting";

    const ft::Status engineStatus = ft::engine::MltRuntime::initialize();
    int exitCode = 0;
    {
        ft::ui::AppController controller(engineStatus, renderHelperPath());
        {
            QQmlApplicationEngine engine;
            engine.addImageProvider(QStringLiteral("ft-thumbnail"),
                                    controller.createThumbnailProvider());
            engine.setInitialProperties(
                {{QStringLiteral("app"), QVariant::fromValue(&controller)}});
            QObject::connect(
                &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
            engine.loadFromModule("FrameTrellis", "Main");
            if (engine.rootObjects().isEmpty()) {
                return 1;
            }
            const QStringList positional = parser.positionalArguments();
            if (!positional.isEmpty()) {
                controller.project()->openProject(QFileInfo(positional.first()).absoluteFilePath());
            }
            if (parser.isSet(screenshotOption)) {
                auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                const QString target = parser.value(screenshotOption);
                QTimer::singleShot(parser.value(delayOption).toInt(), &app, [window, target] {
                    const bool saved = window && window->grabWindow().save(target);
                    qCInfo(lcUi) << "Screenshot" << target << (saved ? "saved" : "failed");
                    QCoreApplication::exit(saved ? 0 : 2);
                });
            }
            exitCode = QGuiApplication::exec();
        } // the QML engine goes first: it references the controllers
    } // controllers next: playback and export stop their threads and processes
    QThreadPool::globalInstance()->waitForDone(); // pending probes and thumbnails use MLT
    ft::engine::MltRuntime::shutdown();
    return exitCode;
}
