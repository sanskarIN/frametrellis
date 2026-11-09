// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/export_file.h"

#include "core/logging.h"

#include <QDir>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ft::rendering {

Status publishRenderedFile(const QString& partPath, const QString& destinationPath,
                           bool replaceExisting)
{
    if (!replaceExisting && QFileInfo::exists(destinationPath)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The batch output already exists. Nothing was replaced."));
    }
#ifdef Q_OS_WIN
    const std::wstring source = QDir::toNativeSeparators(partPath).toStdWString();
    const std::wstring target = QDir::toNativeSeparators(destinationPath).toStdWString();
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(source.c_str(), target.c_str(), flags)) {
        const DWORD error = GetLastError();
        return makeError(!replaceExisting &&
                                 (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
                             ? ErrorCode::Conflict
                             : ErrorCode::Io,
                         QStringLiteral("The exported file could not be moved into place."),
                         QStringLiteral("Windows error %1").arg(error));
    }
#else
    const std::filesystem::path source(partPath.toStdString());
    const std::filesystem::path target(destinationPath.toStdString());
    std::error_code error;
    if (replaceExisting) {
        std::filesystem::rename(source, target, error);
    } else {
        // This atomic same-directory publication cannot replace a destination
        // that another process created between the initial check and commit.
        std::filesystem::create_hard_link(source, target, error);
        if (!error) {
            std::error_code cleanup;
            std::filesystem::remove(source, cleanup);
            if (cleanup) {
                qCWarning(lcRender) << "Could not remove published batch temporary file:" << partPath
                                    << QString::fromStdString(cleanup.message());
            }
        }
    }
    if (error) {
        return makeError(!replaceExisting && error == std::errc::file_exists ? ErrorCode::Conflict
                                                                             : ErrorCode::Io,
                         QStringLiteral("The exported file could not be moved into place."),
                         QString::fromStdString(error.message()));
    }
#endif
    return {};
}

} // namespace ft::rendering
