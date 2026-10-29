// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_runtime.h"

#include "core/logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <mlt++/Mlt.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>

#ifdef Q_OS_WIN
#include <string>
#include <windows.h>
#endif

namespace ft::engine {

namespace {

std::mutex g_mutex;
bool g_initialized = false;
EngineCapabilities g_capabilities;
/// While MLT loads its modules, collects the reasons why a module could not be loaded.
thread_local QStringList* t_moduleLoadErrors = nullptr;

void mltLogCallback(void* service, int level, const char* format, va_list args)
{
    if (level > MLT_LOG_WARNING) {
        return;
    }
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    QString message = QString::fromUtf8(buffer).trimmed();
    if (message.isEmpty()) {
        return;
    }
    const char* serviceName =
        service ? mlt_properties_get(static_cast<mlt_properties>(service), "mlt_service") : nullptr;
    if (serviceName) {
        message = QStringLiteral("[%1] %2").arg(QString::fromUtf8(serviceName), message);
    }
    if (t_moduleLoadErrors && message.contains(QLatin1String("failed to dlopen"))) {
        t_moduleLoadErrors->append(message.simplified());
        qCWarning(lcEngine).noquote() << "MLT:" << message.simplified();
        return;
    }
    if (level <= MLT_LOG_ERROR) {
        qCWarning(lcEngine).noquote() << "MLT error:" << message;
    } else {
        qCDebug(lcEngine).noquote() << "MLT:" << message;
    }
}

#ifdef Q_OS_WIN
/// MLT loads its modules with LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH). For the dependencies
/// of a module, Windows then searches the module's own folder (lib/mlt) instead of the
/// application folder, where a relocatable package keeps FFmpeg and the other libraries. Adding
/// the application folder to the DLL search path lets the modules find them without relying on
/// PATH. It also takes the current folder out of the search path.
void addApplicationFolderToDllSearchPath()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) ==
           path.size()) {
        path.resize(path.size() * 2);
    }
    const auto separator = path.find_last_of(L"\\/", length);
    if (length == 0 || separator == std::wstring::npos) {
        qCWarning(lcEngine) << "Cannot determine the application folder for the DLL search path";
        return;
    }
    path.resize(separator);
    if (!SetDllDirectoryW(path.c_str())) {
        qCWarning(lcEngine) << "SetDllDirectory failed with error" << GetLastError();
    }
}
#endif

/// MLT's loader producer, which opens every file, sets up two global tables the first time it is
/// used, without a lock: the normalising filters (loader.ini) and the file-type dictionary
/// (loader.dict). The editor opens files on several threads at once (preview, thumbnails and
/// waveforms), so concurrent first uses could initialise and free the same table twice. Use the
/// loader once here, before any other thread can.
void initializeLoaderTables()
{
    Mlt::Profile profile;
    // A service:resource that exists loads the normalising filters...
    const Mlt::Producer color(profile, "color:0x00000000");
    // ...and one whose service does not exist makes the loader fall back to its dictionary.
    // Nothing is opened: no MLT service and no FFmpeg protocol has this name.
    const Mlt::Producer none(profile, "frametrellis-loader-setup:");
}

QString findRepository()
{
    for (const char* variable : {"FRAMETRELLIS_MLT_REPOSITORY", "MLT_REPOSITORY"}) {
        const QString value = qEnvironmentVariable(variable);
        if (!value.isEmpty() && QFileInfo(value).isDir()) {
            return value;
        }
    }
    if (QCoreApplication::instance()) {
        // Relocatable packages ship the MLT modules next to the executable.
        const QDir appDir(QCoreApplication::applicationDirPath());
        for (const QString& candidate : {QStringLiteral("lib/mlt-7"), QStringLiteral("lib/mlt"),
                                         QStringLiteral("../lib/mlt-7"),
                                         QStringLiteral("../lib/mlt"), QStringLiteral("mlt-7")}) {
            const QString path = QDir::cleanPath(appDir.filePath(candidate));
            if (QFileInfo(path).isDir()) {
                return path;
            }
        }
    }
#ifdef FT_MLT_MODULE_DIR
    // Running from a build tree: use the modules of the MLT we were built against.
    if (const QString buildTime = QStringLiteral(FT_MLT_MODULE_DIR); QFileInfo(buildTime).isDir()) {
        return buildTime;
    }
#endif
    return {}; // let MLT use its compiled-in default
}

void configureDataPath(const QString& repository)
{
    if (!qEnvironmentVariableIsEmpty("MLT_DATA") || repository.isEmpty()) {
        return;
    }
#ifdef FT_MLT_DATA_DIR
    if (repository == QLatin1String(FT_MLT_MODULE_DIR) &&
        QFileInfo(QStringLiteral(FT_MLT_DATA_DIR)).isDir()) {
        qputenv("MLT_DATA", QFile::encodeName(QStringLiteral(FT_MLT_DATA_DIR)));
        return;
    }
#endif
    // A repository at <prefix>/lib/mlt-7 has its data at <prefix>/share/mlt-7.
    const QDir repoDir(repository);
    for (const QString& candidate :
         {QStringLiteral("../../share/mlt-7"), QStringLiteral("../../share/mlt"),
          QStringLiteral("../share/mlt-7"), QStringLiteral("../share/mlt")}) {
        const QString path = QDir::cleanPath(repoDir.filePath(candidate));
        if (QFileInfo(path).isDir()) {
            qputenv("MLT_DATA", QFile::encodeName(path));
            return;
        }
    }
}

bool hasService(Mlt::Repository* repository, mlt_service_type type, const char* name)
{
    std::unique_ptr<Mlt::Properties> services;
    switch (type) {
    case mlt_service_producer_type:
        services.reset(repository->producers());
        break;
    case mlt_service_consumer_type:
        services.reset(repository->consumers());
        break;
    case mlt_service_filter_type:
        services.reset(repository->filters());
        break;
    case mlt_service_transition_type:
        services.reset(repository->transitions());
        break;
    default:
        return false;
    }
    return services && services->get_data(name) != nullptr;
}

/// Some MLT/FFmpeg combinations register avfilter.eq but fail when it runs (for example MLT
/// 7.42 with FFmpeg 6.1). Run it once on a red test frame and check that saturation 0 turns it
/// grey, so picture adjustments are only offered when they actually work.
bool eqFilterWorks()
{
    Mlt::Profile profile;
    profile.set_explicit(1);
    profile.set_width(32);
    profile.set_height(18);
    profile.set_frame_rate(25, 1);
    profile.set_sample_aspect(1, 1);
    profile.set_progressive(1);
    Mlt::Producer red(profile, "color", "0xff0000ff");
    Mlt::Filter eq(profile, "avfilter.eq");
    if (!red.is_valid() || !eq.is_valid()) {
        return false;
    }
    eq.set("av.saturation", 0.0);
    red.attach(eq);
    std::unique_ptr<Mlt::Frame> frame(red.get_frame());
    if (!frame) {
        return false;
    }
    mlt_image_format format = mlt_image_rgba;
    int width = 32;
    int height = 18;
    const uint8_t* image = frame->get_image(format, width, height);
    if (!image || format != mlt_image_rgba) {
        return false;
    }
    const uint8_t* centre =
        image + (static_cast<std::ptrdiff_t>(height / 2) * width + width / 2) * 4;
    return std::abs(int(centre[0]) - int(centre[1])) < 40 &&
           std::abs(int(centre[1]) - int(centre[2])) < 40;
}

} // namespace

QStringList EngineCapabilities::missingEssentials() const
{
    QStringList missing;
    if (!avformatProducer) {
        missing << QStringLiteral("avformat producer (FFmpeg)");
    }
    if (!avformatConsumer) {
        missing << QStringLiteral("avformat consumer (FFmpeg)");
    }
    if (!colorProducer) {
        missing << QStringLiteral("color producer");
    }
    if (!mixTransition) {
        missing << QStringLiteral("mix transition");
    }
    if (compositor.isEmpty()) {
        missing << QStringLiteral("affine transition (MLT \"plus\" module)");
    }
    return missing;
}

QStringList EngineCapabilities::missingOptional() const
{
    QStringList missing;
    if (!volumeFilter) {
        missing << QStringLiteral("volume filter (clip volume and audio fades)");
    }
    if (!brightnessFilter) {
        missing << QStringLiteral("brightness filter (video fades and opacity)");
    }
    if (!eqFilter) {
        missing << QStringLiteral("avfilter.eq (brightness, contrast and saturation)");
    }
    if (!transformFilter) {
        missing << QStringLiteral("affine filter (clip position, scale and rotation)");
    }
    return missing;
}

Status MltRuntime::initialize()
{
    std::lock_guard lock(g_mutex);
    if (g_initialized) {
        return {};
    }
#ifdef Q_OS_WIN
    addApplicationFolderToDllSearchPath();
#endif
    const QString repositoryPath = findRepository();
    configureDataPath(repositoryPath);
    mlt_log_set_level(MLT_LOG_WARNING);
    mlt_log_set_callback(mltLogCallback);

    const QByteArray encoded = QFile::encodeName(repositoryPath);
    QStringList moduleLoadErrors;
    t_moduleLoadErrors = &moduleLoadErrors;
    Mlt::Repository* repository =
        Mlt::Factory::init(repositoryPath.isEmpty() ? nullptr : encoded.constData());
    t_moduleLoadErrors = nullptr;
    if (!repository) {
        return makeError(
            ErrorCode::Engine, QStringLiteral("The MLT multimedia framework could not be started."),
            QStringLiteral("repository: %1")
                .arg(repositoryPath.isEmpty() ? QStringLiteral("<default>") : repositoryPath));
    }
    initializeLoaderTables();

    EngineCapabilities caps;
    caps.mltVersion = QString::fromLatin1(mlt_version_get_string());
    caps.repositoryPath =
        repositoryPath.isEmpty() ? QStringLiteral("<built-in default>") : repositoryPath;
    caps.avformatProducer = hasService(repository, mlt_service_producer_type, "avformat");
    caps.avformatConsumer = hasService(repository, mlt_service_consumer_type, "avformat");
    caps.colorProducer = hasService(repository, mlt_service_producer_type, "color");
    caps.mixTransition = hasService(repository, mlt_service_transition_type, "mix");
    // Video tracks are layered with the "affine" transition. MLT's older "composite" transition
    // is not a usable fallback: with current MLT releases it draws black frames.
    if (hasService(repository, mlt_service_transition_type, "affine")) {
        caps.compositor = QStringLiteral("affine");
    }
    caps.volumeFilter = hasService(repository, mlt_service_filter_type, "volume");
    caps.brightnessFilter = hasService(repository, mlt_service_filter_type, "brightness");
    caps.transformFilter = hasService(repository, mlt_service_filter_type, "affine");
    caps.eqFilter =
        hasService(repository, mlt_service_filter_type, "avfilter.eq") && eqFilterWorks();
    g_capabilities = caps;
    g_initialized = true;

    qCInfo(lcEngine).noquote() << "MLT" << caps.mltVersion << "repository" << caps.repositoryPath;
    const QStringList essentials = caps.missingEssentials();
    if (!essentials.isEmpty()) {
        qCWarning(lcEngine).noquote()
            << "Missing MLT services:" << essentials.join(QStringLiteral(", "));
        QString details = essentials.join(QStringLiteral(", "));
        if (!moduleLoadErrors.isEmpty()) {
            // The modules that failed to load usually explain why services are missing.
            details += QStringLiteral("; ") + moduleLoadErrors.join(u' ');
        }
        return makeError(
            ErrorCode::Engine,
            QStringLiteral("The installation is incomplete: required MLT services are missing."),
            details);
    }
    for (const QString& optional : caps.missingOptional()) {
        qCWarning(lcEngine).noquote() << "Optional MLT service unavailable:" << optional;
    }
    return {};
}

bool MltRuntime::isInitialized()
{
    std::lock_guard lock(g_mutex);
    return g_initialized;
}

void MltRuntime::shutdown()
{
    std::lock_guard lock(g_mutex);
    if (g_initialized) {
        Mlt::Factory::close();
        g_initialized = false;
    }
}

const EngineCapabilities& MltRuntime::capabilities()
{
    return g_capabilities;
}

} // namespace ft::engine
