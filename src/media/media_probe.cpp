// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/media_probe.h"

#include "core/logging.h"
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/still_producer.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>
#include <mlt++/Mlt.h>

#include <climits>
#include <memory>

namespace ft::media {

namespace {

// Probing at 1000 fps makes MLT report lengths in milliseconds.
constexpr int kProbeRate = 1000;

const QSet<QString>& stillImageCodecs()
{
    static const QSet<QString> codecs{
        QStringLiteral("png"),      QStringLiteral("mjpeg"),  QStringLiteral("bmp"),
        QStringLiteral("tiff"),     QStringLiteral("webp"),   QStringLiteral("targa"),
        QStringLiteral("ppm"),      QStringLiteral("pgm"),    QStringLiteral("pam"),
        QStringLiteral("jpeg2000"), QStringLiteral("jpegls"), QStringLiteral("qoi"),
        QStringLiteral("sgi"),      QStringLiteral("exr"),    QStringLiteral("dpx"),
        QStringLiteral("gif"),      QStringLiteral("psd"),    QStringLiteral("hdr")};
    return codecs;
}

std::unique_ptr<Mlt::Producer> openAvformat(Mlt::Profile& profile, const QString& filePath)
{
    // Force the FFmpeg-based producer so that file names are never interpreted as other MLT
    // services, while keeping the loader's normalising filters.
    const QByteArray resource = QByteArrayLiteral("avformat:") + filePath.toUtf8();
    auto producer = std::make_unique<Mlt::Producer>(profile, resource.constData());
    if (!producer->is_valid()) {
        return nullptr;
    }
    return producer;
}

QString streamProperty(Mlt::Producer& producer, int stream, const char* name)
{
    const QByteArray key =
        QStringLiteral("meta.media.%1.%2").arg(stream).arg(QLatin1String(name)).toUtf8();
    const char* value = producer.get(key.constData());
    return value ? QString::fromUtf8(value) : QString();
}

Error unsupported(const QString& filePath, const QString& reason)
{
    return makeError(
        ErrorCode::Media,
        QStringLiteral("\"%1\" is not a supported media file.").arg(QFileInfo(filePath).fileName()),
        reason);
}

} // namespace

QStringList importFileSuffixes()
{
    return {QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("mov"),
            QStringLiteral("mkv"),  QStringLiteral("webm"), QStringLiteral("avi"),
            QStringLiteral("mts"),  QStringLiteral("m2ts"), QStringLiteral("mpg"),
            QStringLiteral("mpeg"), QStringLiteral("ts"),   QStringLiteral("mxf"),
            QStringLiteral("ogv"),  QStringLiteral("wmv"),  QStringLiteral("flv"),
            QStringLiteral("3gp"),  QStringLiteral("wav"),  QStringLiteral("mp3"),
            QStringLiteral("m4a"),  QStringLiteral("aac"),  QStringLiteral("flac"),
            QStringLiteral("ogg"),  QStringLiteral("opus"), QStringLiteral("aiff"),
            QStringLiteral("aif"),  QStringLiteral("wma"),  QStringLiteral("png"),
            QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("bmp"),
            QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("webp"),
            QStringLiteral("gif"),  QStringLiteral("tga")};
}

Result<MediaInfo> probeMedia(const QString& filePath)
{
    if (!engine::MltRuntime::isInitialized()) {
        return makeError(ErrorCode::Engine, QStringLiteral("The media engine is not initialised."));
    }
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return makeError(
            ErrorCode::NotFound,
            QStringLiteral("\"%1\" does not exist or was moved.").arg(fileInfo.fileName()),
            filePath);
    }
    if (!fileInfo.isFile()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("\"%1\" is not a file.").arg(fileInfo.fileName()));
    }
    if (!fileInfo.isReadable()) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("\"%1\" cannot be read. Check its permissions.")
                             .arg(fileInfo.fileName()));
    }
    if (fileInfo.size() == 0) {
        return unsupported(filePath, QStringLiteral("the file is empty"));
    }

    // Stills are decoded with Qt (see engine::createStillProducer); animated images go through
    // FFmpeg like video.
    {
        QImageReader reader(fileInfo.absoluteFilePath());
        reader.setAutoTransform(true);
        if (reader.canRead() && !(reader.supportsAnimation() && reader.imageCount() > 1)) {
            QString reason;
            const QImage check =
                engine::loadStillImage(fileInfo.absoluteFilePath(), QSize(64, 64), &reason);
            QSize size = reader.size();
            if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
                size.transpose();
            }
            if (check.isNull() || !size.isValid()) {
                return unsupported(filePath, reason.isEmpty()
                                                 ? QStringLiteral("the image could not be decoded")
                                                 : reason);
            }
            MediaInfo image;
            image.kind = MediaKind::Image;
            image.hasVideo = true;
            image.width = size.width();
            image.height = size.height();
            image.videoCodec = QString::fromLatin1(reader.format()).toLower();
            image.container = fileInfo.suffix().toLower();
            image.fileSize = fileInfo.size();
            image.lastModified = fileInfo.lastModified().toUTC();
            return image;
        }
    }

    std::unique_ptr<Mlt::Profile> profile =
        engine::createProfile(640, 360, *FrameRate::create(kProbeRate, 1));
    std::unique_ptr<Mlt::Producer> producer = openAvformat(*profile, fileInfo.absoluteFilePath());
    if (!producer) {
        return unsupported(filePath, QStringLiteral("FFmpeg could not open the file"));
    }

    MediaInfo info;
    info.fileSize = fileInfo.size();
    info.lastModified = fileInfo.lastModified().toUTC();
    info.container = fileInfo.suffix().toLower();
    const int streams = producer->get_int("meta.media.nb_streams");
    int firstVideo = -1;
    int firstAudio = -1;
    for (int i = 0; i < streams; ++i) {
        const QString type = streamProperty(*producer, i, "stream.type");
        if (type == QLatin1String("video") && firstVideo < 0) {
            // Embedded cover art is reported as a video stream with a single picture; ignore it
            // for audio files.
            firstVideo = i;
        } else if (type == QLatin1String("audio") && firstAudio < 0) {
            firstAudio = i;
        }
    }
    if (firstVideo < 0 && firstAudio < 0) {
        return unsupported(filePath, QStringLiteral("no audio or video streams"));
    }
    info.hasVideo = firstVideo >= 0;
    info.hasAudio = firstAudio >= 0;
    if (info.hasVideo) {
        info.videoCodec = streamProperty(*producer, firstVideo, "codec.name");
    }
    if (info.hasAudio) {
        info.audioCodec = streamProperty(*producer, firstAudio, "codec.name");
        info.audioChannels = streamProperty(*producer, firstAudio, "codec.channels").toInt();
        info.audioSampleRate = streamProperty(*producer, firstAudio, "codec.sample_rate").toInt();
    }

    // Decode one frame first: this verifies that the codecs work and fills in the picture
    // metadata needed to classify the file.
    std::unique_ptr<Mlt::Frame> frame(producer->get_frame());
    if (!frame || !frame->is_valid()) {
        return unsupported(filePath, QStringLiteral("no frame could be decoded"));
    }
    const int rateNum = producer->get_int("meta.media.frame_rate_num");
    const int rateDen = producer->get_int("meta.media.frame_rate_den");
    const bool stillCodec = stillImageCodecs().contains(info.videoCodec);
    // Embedded cover art in audio files is a single attached picture reported at 90000 fps.
    if (info.hasVideo && info.hasAudio && stillCodec && rateNum == 90000 && rateDen == 1) {
        info.hasVideo = false;
        info.videoCodec.clear();
    }

    const int length = producer->get_length();
    const bool unbounded = length <= 0 || length >= INT_MAX - 1;
    if (info.hasVideo && stillCodec && unbounded) {
        return unsupported(
            filePath, QStringLiteral(
                          "images in this format cannot be decoded; supported image formats are %1")
                          .arg(engine::stillImageFormats().join(QStringLiteral(", "))));
    }
    if (unbounded) {
        return unsupported(filePath,
                           QStringLiteral("the duration is unknown (live or damaged stream)"));
    } else {
        info.kind = info.hasVideo ? MediaKind::Video : MediaKind::Audio;
        info.durationUs = static_cast<std::int64_t>(length) * (1'000'000 / kProbeRate);
    }

    if (info.hasVideo) {
        const QImage probeImage = engine::frameToImage(*frame, QSize(64, 36));
        if (probeImage.isNull()) {
            return unsupported(
                filePath,
                QStringLiteral("the video codec \"%1\" could not be decoded").arg(info.videoCodec));
        }
        info.width = producer->get_int("meta.media.width");
        info.height = producer->get_int("meta.media.height");
        if (info.kind == MediaKind::Video && rateNum > 0 && rateDen > 0) {
            info.frameRate = Rational(rateNum, rateDen);
        }
        if (info.width <= 0 || info.height <= 0) {
            return unsupported(filePath, QStringLiteral("the picture size is unknown"));
        }
    } else {
        const engine::AudioBlock audio = engine::frameToAudio(*frame, kProbeRate, 48000, 2, 0);
        if (audio.samples.empty()) {
            return unsupported(
                filePath,
                QStringLiteral("the audio codec \"%1\" could not be decoded").arg(info.audioCodec));
        }
    }
    qCDebug(lcMedia) << "Probed" << filePath << mediaKindToString(info.kind) << info.durationUs
                     << "us" << info.width << "x" << info.height;
    return info;
}

Result<MediaAsset> createAsset(const QString& filePath)
{
    Result<MediaInfo> info = probeMedia(filePath);
    if (!info) {
        return info.error();
    }
    MediaAsset asset;
    asset.id = createId();
    asset.filePath = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    asset.name = QFileInfo(filePath).fileName();
    asset.info = std::move(info).value();
    asset.status = MediaStatus::Online;
    return asset;
}

Result<QImage> renderThumbnail(const QString& filePath, const MediaInfo& info, QSize maxSize)
{
    if (!info.hasVideo || info.width <= 0 || info.height <= 0) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("This media has no picture."));
    }
    const QSize size = engine::fitPreviewSize(QSize(info.width, info.height), maxSize);
    if (info.kind == MediaKind::Image) {
        QString reason;
        QImage image = engine::loadStillImage(filePath, size, &reason);
        if (image.isNull()) {
            return makeError(ErrorCode::Media, QStringLiteral("Could not decode a thumbnail."),
                             reason);
        }
        return image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGBA8888);
    }
    std::unique_ptr<Mlt::Profile> profile =
        engine::createProfile(size.width(), size.height(), *FrameRate::create(kProbeRate, 1));
    std::unique_ptr<Mlt::Producer> producer = openAvformat(*profile, filePath);
    if (!producer) {
        return makeError(ErrorCode::Media,
                         QStringLiteral("Could not open the file for a thumbnail."));
    }
    // Pick a frame a little into the clip, which is more representative than a fade from black.
    int position = 0;
    if (info.kind == MediaKind::Video) {
        const std::int64_t millis = info.durationUs / 1000;
        position = static_cast<int>(std::min<std::int64_t>(millis / 10, 2000));
    }
    QImage image = engine::grabFrame(*producer, position, size);
    if (image.isNull()) {
        return makeError(ErrorCode::Media, QStringLiteral("Could not decode a thumbnail."));
    }
    return image;
}

} // namespace ft::media
