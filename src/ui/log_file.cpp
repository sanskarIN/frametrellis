// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/log_file.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <cstdio>
#include <mutex>

namespace ft::ui {

namespace {

constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;
std::mutex g_logMutex;
QFile* g_logFile = nullptr;
QtMessageHandler g_previous = nullptr;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "log";
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QByteArray line =
        QStringLiteral("[%1] [%2] %3: %4\n")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                 QLatin1String(levelName(type)),
                 QLatin1String(context.category ? context.category : "default"), message)
            .toUtf8();
    {
        std::lock_guard lock(g_logMutex);
        if (g_logFile && g_logFile->isOpen()) {
            g_logFile->write(line);
            g_logFile->flush();
        }
    }
    if (g_previous) {
        g_previous(type, context, message);
    } else {
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    }
}

} // namespace

QString logFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/logs/frametrellis.log");
}

void installFileLogger()
{
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > kMaxLogBytes) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
    static QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        g_logFile = &file;
    }
    g_previous = qInstallMessageHandler(handler);
}

} // namespace ft::ui
