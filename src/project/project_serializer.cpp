// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_serializer.h"

#include "core/logging.h"
#include "core/version.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace ft::project_format {

namespace {

QString rationalToJson(const Rational& r)
{
    return r.isValid() ? r.toString() : QString();
}

QJsonObject mediaInfoToJson(const MediaInfo& info)
{
    QJsonObject o;
    o[QLatin1String("kind")] = mediaKindToString(info.kind);
    o[QLatin1String("durationUs")] = static_cast<double>(info.durationUs);
    o[QLatin1String("hasVideo")] = info.hasVideo;
    o[QLatin1String("hasAudio")] = info.hasAudio;
    o[QLatin1String("width")] = info.width;
    o[QLatin1String("height")] = info.height;
    o[QLatin1String("frameRate")] = rationalToJson(info.frameRate);
    o[QLatin1String("audioChannels")] = info.audioChannels;
    o[QLatin1String("audioSampleRate")] = info.audioSampleRate;
    o[QLatin1String("container")] = info.container;
    o[QLatin1String("videoCodec")] = info.videoCodec;
    o[QLatin1String("audioCodec")] = info.audioCodec;
    o[QLatin1String("fileSize")] = static_cast<double>(info.fileSize);
    if (info.lastModified.isValid()) {
        o[QLatin1String("lastModified")] = info.lastModified.toUTC().toString(Qt::ISODate);
    }
    return o;
}

QJsonObject clipToJson(const Clip& c)
{
    QJsonObject o;
    o[QLatin1String("id")] = c.id;
    o[QLatin1String("media")] = c.assetId;
    o[QLatin1String("start")] = static_cast<double>(c.start);
    o[QLatin1String("in")] = static_cast<double>(c.sourceIn);
    o[QLatin1String("duration")] = static_cast<double>(c.duration);
    const ClipProperties& p = c.properties;
    o[QLatin1String("gain")] = p.gain;
    o[QLatin1String("muted")] = p.muted;
    o[QLatin1String("fadeIn")] = static_cast<double>(p.fadeIn);
    o[QLatin1String("fadeOut")] = static_cast<double>(p.fadeOut);
    o[QLatin1String("brightness")] = p.video.brightness;
    o[QLatin1String("contrast")] = p.video.contrast;
    o[QLatin1String("saturation")] = p.video.saturation;
    o[QLatin1String("opacity")] = p.video.opacity;
    return o;
}

/// Strict field reader that records the JSON path of the first problem encountered.
class Reader
{
public:
    explicit Reader(QString path) : m_path(std::move(path)) {}

    [[nodiscard]] bool failed() const { return m_error.has_value(); }
    [[nodiscard]] Error error() const { return m_error.value_or(Error{}); }

    void fail(const QString& field, const QString& problem)
    {
        if (!m_error) {
            m_error = makeError(ErrorCode::InvalidDocument,
                                QStringLiteral("The project file is damaged or invalid."),
                                QStringLiteral("%1.%2: %3").arg(m_path, field, problem));
        }
    }

    void adopt(const Reader& child)
    {
        if (!m_error && child.m_error) {
            m_error = child.m_error;
        }
    }

    QString string(const QJsonObject& o, const char* key, bool required, int maxLength = 4096)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() || v.isNull()) {
            if (required) {
                fail(QLatin1String(key), QStringLiteral("missing"));
            }
            return {};
        }
        if (!v.isString()) {
            fail(QLatin1String(key), QStringLiteral("expected a string"));
            return {};
        }
        QString s = v.toString();
        if (s.size() > maxLength) {
            fail(QLatin1String(key), QStringLiteral("too long"));
            return {};
        }
        return s;
    }

    std::int64_t integer(const QJsonObject& o, const char* key, std::int64_t min, std::int64_t max,
                         std::optional<std::int64_t> fallback = std::nullopt)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) {
            if (!fallback) {
                fail(QLatin1String(key), QStringLiteral("missing"));
                return min;
            }
            return *fallback;
        }
        const double d = v.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!v.isDouble() || !std::isfinite(d) || std::trunc(d) != d || d < static_cast<double>(min)
            || d > static_cast<double>(max)) {
            fail(QLatin1String(key), QStringLiteral("expected an integer between %1 and %2").arg(min).arg(max));
            return min;
        }
        return static_cast<std::int64_t>(d);
    }

    double number(const QJsonObject& o, const char* key, double min, double max, double fallback)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) {
            return fallback;
        }
        const double d = v.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!v.isDouble() || !std::isfinite(d) || d < min || d > max) {
            fail(QLatin1String(key), QStringLiteral("expected a number between %1 and %2").arg(min).arg(max));
            return fallback;
        }
        return d;
    }

    bool boolean(const QJsonObject& o, const char* key, bool fallback)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) {
            return fallback;
        }
        if (!v.isBool()) {
            fail(QLatin1String(key), QStringLiteral("expected true or false"));
            return fallback;
        }
        return v.toBool();
    }

    QJsonObject object(const QJsonObject& o, const char* key)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isObject()) {
            fail(QLatin1String(key), QStringLiteral("expected an object"));
            return {};
        }
        return v.toObject();
    }

    QJsonArray array(const QJsonObject& o, const char* key, qsizetype maxItems)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) {
            return {};
        }
        if (!v.isArray()) {
            fail(QLatin1String(key), QStringLiteral("expected an array"));
            return {};
        }
        QJsonArray a = v.toArray();
        if (a.size() > maxItems) {
            fail(QLatin1String(key), QStringLiteral("too many entries"));
            return {};
        }
        return a;
    }

    Id id(const QJsonObject& o, const char* key)
    {
        const QString value = string(o, key, true, 64);
        if (!failed() && !isValidId(value)) {
            fail(QLatin1String(key), QStringLiteral("invalid identifier"));
        }
        return value;
    }

    [[nodiscard]] const QString& path() const { return m_path; }

private:
    QString m_path;
    std::optional<Error> m_error;
};

MediaInfo readMediaInfo(Reader& r, const QJsonObject& o)
{
    MediaInfo info;
    const auto kind = mediaKindFromString(r.string(o, "kind", true, 16));
    if (!kind && !r.failed()) {
        r.fail(QStringLiteral("kind"), QStringLiteral("unknown media kind"));
    }
    info.kind = kind.value_or(MediaKind::Video);
    info.durationUs = r.integer(o, "durationUs", 0, std::int64_t{1} << 52, 0);
    info.hasVideo = r.boolean(o, "hasVideo", info.kind != MediaKind::Audio);
    info.hasAudio = r.boolean(o, "hasAudio", info.kind == MediaKind::Audio);
    info.width = static_cast<int>(r.integer(o, "width", 0, 65536, 0));
    info.height = static_cast<int>(r.integer(o, "height", 0, 65536, 0));
    const QString rate = r.string(o, "frameRate", false, 32);
    if (!rate.isEmpty()) {
        const auto parsed = Rational::fromString(rate);
        if (!parsed || !parsed->isPositive()) {
            r.fail(QStringLiteral("frameRate"), QStringLiteral("invalid rational"));
        } else {
            info.frameRate = *parsed;
        }
    }
    info.audioChannels = static_cast<int>(r.integer(o, "audioChannels", 0, 64, 0));
    info.audioSampleRate = static_cast<int>(r.integer(o, "audioSampleRate", 0, 768000, 0));
    info.container = r.string(o, "container", false, 256);
    info.videoCodec = r.string(o, "videoCodec", false, 128);
    info.audioCodec = r.string(o, "audioCodec", false, 128);
    info.fileSize = r.integer(o, "fileSize", 0, std::int64_t{1} << 52, 0);
    const QString modified = r.string(o, "lastModified", false, 64);
    if (!modified.isEmpty()) {
        info.lastModified = QDateTime::fromString(modified, Qt::ISODate);
    }
    if (info.kind == MediaKind::Audio && info.hasVideo) {
        r.fail(QStringLiteral("hasVideo"), QStringLiteral("audio media cannot have video"));
    }
    return info;
}

QString resolveMediaPath(const QString& storedPath, const QString& relativePath,
                         const QString& projectDir)
{
    const QString absolute = QDir::cleanPath(QDir::fromNativeSeparators(storedPath));
    if (QFileInfo::exists(absolute) || relativePath.isEmpty() || projectDir.isEmpty()) {
        return absolute;
    }
    const QString candidate = QDir::cleanPath(QDir(projectDir).absoluteFilePath(relativePath));
    if (QFileInfo::exists(candidate)) {
        qCInfo(lcProject) << "Resolved moved media" << absolute << "to" << candidate;
        return candidate;
    }
    return absolute;
}

} // namespace

QJsonObject toJson(const Project& project, const QString& projectDir)
{
    QJsonObject settings;
    const ProjectSettings& s = project.settings();
    settings[QLatin1String("width")] = s.width;
    settings[QLatin1String("height")] = s.height;
    settings[QLatin1String("frameRate")] = s.frameRate.rational().toString();
    settings[QLatin1String("sampleRate")] = s.sampleRate;
    settings[QLatin1String("audioChannels")] = s.audioChannels;

    const QDir dir(projectDir);
    QJsonArray media;
    for (const MediaAsset& a : project.media()) {
        QJsonObject o;
        o[QLatin1String("id")] = a.id;
        o[QLatin1String("name")] = a.name;
        o[QLatin1String("path")] = QDir::fromNativeSeparators(a.filePath);
        if (!projectDir.isEmpty()) {
            o[QLatin1String("relativePath")] = dir.relativeFilePath(a.filePath);
        }
        o[QLatin1String("info")] = mediaInfoToJson(a.info);
        media.append(o);
    }

    QJsonArray tracks;
    for (const Track& t : project.timeline().tracks()) {
        QJsonObject o;
        o[QLatin1String("id")] = t.id;
        o[QLatin1String("kind")] = trackKindToString(t.kind);
        o[QLatin1String("name")] = t.name;
        o[QLatin1String("muted")] = t.muted;
        o[QLatin1String("hidden")] = t.hidden;
        o[QLatin1String("locked")] = t.locked;
        QJsonArray clips;
        for (const Clip& c : t.clips) {
            clips.append(clipToJson(c));
        }
        o[QLatin1String("clips")] = clips;
        tracks.append(o);
    }

    QJsonObject body;
    body[QLatin1String("id")] = project.id();
    body[QLatin1String("name")] = project.name();
    body[QLatin1String("created")] = project.created().toUTC().toString(Qt::ISODate);
    body[QLatin1String("settings")] = settings;
    body[QLatin1String("media")] = media;
    body[QLatin1String("tracks")] = tracks;

    QJsonObject root;
    root[QLatin1String("format")] = QLatin1String(kFormatName);
    root[QLatin1String("version")] = kCurrentVersion;
    root[QLatin1String("generator")] =
        QStringLiteral("%1 %2").arg(QLatin1String(version::applicationName), QLatin1String(version::string));
    root[QLatin1String("project")] = body;
    return root;
}

Result<Project> fromJson(const QJsonObject& document, const QString& projectDir)
{
    Reader root(QStringLiteral("$"));
    if (root.string(document, "format", true, 128) != QLatin1String(kFormatName)) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("This file is not a FrameTrellis project."));
    }
    const std::int64_t versionNumber = root.integer(document, "version", 1, 1'000'000);
    if (root.failed()) {
        return root.error();
    }
    if (versionNumber > kCurrentVersion) {
        return makeError(ErrorCode::UnsupportedVersion,
                         QStringLiteral("This project was saved by a newer version of FrameTrellis "
                                        "(format %1). Please update FrameTrellis to open it.")
                             .arg(versionNumber));
    }
    // Future format versions add migration steps here, upgrading the JSON in place.

    const QJsonObject body = root.object(document, "project");
    if (root.failed()) {
        return root.error();
    }

    Reader r(QStringLiteral("$.project"));
    Project project;
    project.setId(r.id(body, "id"));
    const QString name = r.string(body, "name", false, 256).trimmed();
    project.setName(name.isEmpty() ? QStringLiteral("Untitled") : name);
    const QString created = r.string(body, "created", false, 64);
    if (!created.isEmpty()) {
        project.setCreated(QDateTime::fromString(created, Qt::ISODate));
    }

    {
        const QJsonObject so = r.object(body, "settings");
        Reader sr(QStringLiteral("$.project.settings"));
        ProjectSettings settings;
        settings.width = static_cast<int>(sr.integer(so, "width", 1, 65536));
        settings.height = static_cast<int>(sr.integer(so, "height", 1, 65536));
        const auto rate = Rational::fromString(sr.string(so, "frameRate", true, 32));
        const auto frameRate = rate ? FrameRate::fromRational(*rate) : std::nullopt;
        if (!frameRate && !sr.failed()) {
            sr.fail(QStringLiteral("frameRate"), QStringLiteral("invalid frame rate"));
        }
        if (frameRate) {
            settings.frameRate = *frameRate;
        }
        settings.sampleRate = static_cast<int>(sr.integer(so, "sampleRate", 1, 768000, 48000));
        settings.audioChannels = static_cast<int>(sr.integer(so, "audioChannels", 1, 64, 2));
        r.adopt(sr);
        if (!r.failed()) {
            if (const Status valid = settings.validate(); !valid) {
                r.fail(QStringLiteral("settings"), valid.error().message);
            } else {
                (void)project.setSettings(settings);
            }
        }
    }

    const QJsonArray media = r.array(body, "media", kMaxMediaAssets);
    for (qsizetype i = 0; i < media.size() && !r.failed(); ++i) {
        Reader mr(QStringLiteral("$.project.media[%1]").arg(i));
        if (!media.at(i).isObject()) {
            mr.fail(QStringLiteral("entry"), QStringLiteral("expected an object"));
            r.adopt(mr);
            break;
        }
        const QJsonObject o = media.at(i).toObject();
        MediaAsset asset;
        asset.id = mr.id(o, "id");
        const QString path = mr.string(o, "path", true, 4096);
        const QString relative = mr.string(o, "relativePath", false, 4096);
        asset.name = mr.string(o, "name", false, 512);
        Reader ir(mr.path() + QStringLiteral(".info"));
        asset.info = readMediaInfo(ir, mr.object(o, "info"));
        mr.adopt(ir);
        if (!mr.failed()) {
            asset.filePath = resolveMediaPath(path, relative, projectDir);
            if (asset.name.isEmpty()) {
                asset.name = QFileInfo(asset.filePath).fileName();
            }
            if (const Status added = project.addAsset(asset); !added) {
                mr.fail(QStringLiteral("id"), added.error().message);
            }
        }
        r.adopt(mr);
    }

    std::vector<Track> tracks;
    const QJsonArray trackArray = r.array(body, "tracks", kMaxTracks);
    for (qsizetype i = 0; i < trackArray.size() && !r.failed(); ++i) {
        Reader tr(QStringLiteral("$.project.tracks[%1]").arg(i));
        const QJsonObject o = trackArray.at(i).toObject();
        Track track;
        track.id = tr.id(o, "id");
        const auto kind = trackKindFromString(tr.string(o, "kind", true, 16));
        if (!kind && !tr.failed()) {
            tr.fail(QStringLiteral("kind"), QStringLiteral("unknown track kind"));
        }
        track.kind = kind.value_or(TrackKind::Video);
        track.name = tr.string(o, "name", false, 64);
        if (track.name.isEmpty()) {
            track.name = QStringLiteral("Track %1").arg(i + 1);
        }
        track.muted = tr.boolean(o, "muted", false);
        track.hidden = tr.boolean(o, "hidden", false);
        track.locked = tr.boolean(o, "locked", false);
        const QJsonArray clips = tr.array(o, "clips", 100'000);
        for (qsizetype c = 0; c < clips.size() && !tr.failed(); ++c) {
            Reader cr(tr.path() + QStringLiteral(".clips[%1]").arg(c));
            const QJsonObject co = clips.at(c).toObject();
            Clip clip;
            clip.id = cr.id(co, "id");
            clip.assetId = cr.id(co, "media");
            clip.start = cr.integer(co, "start", 0, kMaxTimelineFrames);
            clip.sourceIn = cr.integer(co, "in", 0, kMaxTimelineFrames);
            clip.duration = cr.integer(co, "duration", 1, kMaxTimelineFrames);
            ClipProperties& p = clip.properties;
            p.gain = cr.number(co, "gain", 0.0, kMaxClipGain, 1.0);
            p.muted = cr.boolean(co, "muted", false);
            p.fadeIn = cr.integer(co, "fadeIn", 0, kMaxTimelineFrames, 0);
            p.fadeOut = cr.integer(co, "fadeOut", 0, kMaxTimelineFrames, 0);
            p.video.brightness = cr.number(co, "brightness", -1.0, 1.0, 0.0);
            p.video.contrast = cr.number(co, "contrast", 0.0, 2.0, 1.0);
            p.video.saturation = cr.number(co, "saturation", 0.0, 3.0, 1.0);
            p.video.opacity = cr.number(co, "opacity", 0.0, 1.0, 1.0);
            tr.adopt(cr);
            track.clips.push_back(std::move(clip));
        }
        r.adopt(tr);
        tracks.push_back(std::move(track));
    }
    if (r.failed()) {
        return r.error();
    }
    project.timeline().restoreTracks(std::move(tracks));

    if (const Status valid = project.validate(); !valid) {
        Error error = valid.error();
        error.code = ErrorCode::InvalidDocument;
        error.detail = error.detail.isEmpty() ? error.message : error.detail + QStringLiteral(": ") + error.message;
        error.message = QStringLiteral("The project file is damaged or invalid.");
        return error;
    }
    return project;
}

QByteArray toBytes(const Project& project, const QString& projectDir)
{
    return QJsonDocument(toJson(project, projectDir)).toJson(QJsonDocument::Indented);
}

Result<Project> fromBytes(const QByteArray& data, const QString& projectDir)
{
    if (data.size() > kMaxDocumentBytes) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("The project file is too large."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return makeError(ErrorCode::Parse, QStringLiteral("The project file could not be read."),
                         QStringLiteral("%1 at offset %2").arg(parseError.errorString()).arg(parseError.offset));
    }
    if (!document.isObject()) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("This file is not a FrameTrellis project."));
    }
    return fromJson(document.object(), projectDir);
}

Status saveToFile(const Project& project, const QString& filePath)
{
    const QFileInfo info(filePath);
    const QString directory = info.absolutePath();
    QSaveFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not write the project file."),
                         QStringLiteral("%1: %2").arg(filePath, file.errorString()));
    }
    const QByteArray bytes = toBytes(project, directory);
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return makeError(ErrorCode::Io, QStringLiteral("Could not write the project file."),
                         QStringLiteral("%1: %2").arg(filePath, reason));
    }
    if (!file.commit()) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("Saving failed. The previous version of the file was kept."),
                         QStringLiteral("%1: %2").arg(filePath, file.errorString()));
    }
    qCInfo(lcProject) << "Saved project to" << filePath;
    return {};
}

Result<Project> loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The project file does not exist."), filePath);
    }
    if (file.size() > kMaxDocumentBytes) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("The project file is too large."), filePath);
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("The project file could not be opened."),
                         QStringLiteral("%1: %2").arg(filePath, file.errorString()));
    }
    const QByteArray data = file.read(kMaxDocumentBytes + 1);
    Result<Project> project = fromBytes(data, QFileInfo(filePath).absolutePath());
    if (!project) {
        qCWarning(lcProject) << "Failed to load" << filePath << project.error().toString();
    }
    return project;
}

} // namespace ft::project_format
