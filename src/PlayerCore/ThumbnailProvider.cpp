#include "PlayerCore/ThumbnailProvider.h"

#include "Core/Logger.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
constexpr int cacheVersion = 1;
constexpr int thumbnailCount = 100;

QString stableMediaKey(const QUrl &url)
{
    QFileInfo file(url.toLocalFile());
    QString path = file.canonicalFilePath();
    if (path.isEmpty()) {
        path = file.absoluteFilePath();
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QDir::toNativeSeparators(QDir::cleanPath(path)).toUtf8(),
            QCryptographicHash::Md5)
            .toHex()
            .toUpper());
}

QString mediaCacheDirectory(const QString &key)
{
    return QDir(ThumbnailProvider::cacheDirectory()).filePath(key);
}

const mpv_node *mapValue(const mpv_node &node, const char *name)
{
    if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list) {
        return nullptr;
    }
    for (int index = 0; index < node.u.list->num; ++index) {
        if (node.u.list->keys[index]
            && std::strcmp(node.u.list->keys[index], name) == 0) {
            return &node.u.list->values[index];
        }
    }
    return nullptr;
}

bool wasCancelled(
    const std::atomic<quint64> &generation, quint64 expected)
{
    return generation.load(std::memory_order_relaxed) != expected;
}

bool waitForEvent(
    mpv_handle *mpv, mpv_event_id wanted,
    const std::atomic<quint64> &generation, quint64 expected,
    double timeoutSeconds = 8.0)
{
    double remaining = timeoutSeconds;
    while (remaining > 0.0
           && !wasCancelled(generation, expected)) {
        mpv_event *event = mpv_wait_event(mpv, 0.1);
        remaining -= 0.1;
        if (!event) {
            continue;
        }
        if (event->event_id == wanted) {
            return true;
        }
        if (event->event_id == MPV_EVENT_END_FILE
            || event->event_id == MPV_EVENT_SHUTDOWN) {
            return false;
        }
    }
    return false;
}

QImage screenshotRaw(mpv_handle *mpv)
{
    const char *command[] = {
        "screenshot-raw", "video", "rgba", nullptr};
    mpv_node result{};
    const int error = mpv_command_ret(mpv, command, &result);
    if (error < 0) {
        return {};
    }
    const mpv_node *width = mapValue(result, "w");
    const mpv_node *height = mapValue(result, "h");
    const mpv_node *stride = mapValue(result, "stride");
    const mpv_node *format = mapValue(result, "format");
    const mpv_node *data = mapValue(result, "data");
    QImage image;
    if (width && height && stride && format && data
        && width->format == MPV_FORMAT_INT64
        && height->format == MPV_FORMAT_INT64
        && stride->format == MPV_FORMAT_INT64
        && format->format == MPV_FORMAT_STRING
        && data->format == MPV_FORMAT_BYTE_ARRAY
        && format->u.string
        && std::strcmp(format->u.string, "rgba") == 0
        && data->u.ba && data->u.ba->data) {
        const int w = static_cast<int>(width->u.int64);
        const int h = static_cast<int>(height->u.int64);
        const int sourceStride = static_cast<int>(stride->u.int64);
        if (w > 0 && h > 0 && sourceStride >= w * 4
            && data->u.ba->size
                   >= static_cast<size_t>(sourceStride) * h) {
            image = QImage(w, h, QImage::Format_RGBA8888);
            const auto *source =
                static_cast<const uchar *>(data->u.ba->data);
            for (int row = 0; row < h; ++row) {
                std::memcpy(
                    image.scanLine(row),
                    source + static_cast<qsizetype>(row) * sourceStride,
                    static_cast<size_t>(w) * 4);
            }
        }
    }
    mpv_free_node_contents(&result);
    return image;
}

QList<MediaThumbnail> readCache(
    const QUrl &url, const QString &key)
{
    const QFileInfo source(url.toLocalFile());
    QFile metadata(
        QDir(mediaCacheDirectory(key)).filePath(
            QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document =
        QJsonDocument::fromJson(metadata.readAll());
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != cacheVersion
        || root.value(QStringLiteral("fileSize")).toDouble()
               != static_cast<double>(source.size())
        || root.value(QStringLiteral("modified")).toString()
               != source.lastModified().toUTC().toString(Qt::ISODateWithMs)) {
        return {};
    }
    QList<MediaThumbnail> thumbnails;
    const QJsonArray frames = root.value(QStringLiteral("frames")).toArray();
    thumbnails.reserve(frames.size());
    for (const QJsonValue &value : frames) {
        const QJsonObject frame = value.toObject();
        QImage image(QDir(mediaCacheDirectory(key)).filePath(
            frame.value(QStringLiteral("file")).toString()));
        if (image.isNull()) {
            return {};
        }
        thumbnails.append(
            {frame.value(QStringLiteral("seconds")).toDouble(),
             std::move(image)});
    }
    if (!thumbnails.isEmpty()) {
        QFile(metadata.fileName()).setFileTime(
            QDateTime::currentDateTimeUtc(), QFileDevice::FileAccessTime);
    }
    return thumbnails;
}

void pruneCache()
{
    const qint64 maximum =
        static_cast<qint64>(QSettings().value(
            QStringLiteral("thumbnails/maxCacheMiB"), 500).toInt())
        * 1024 * 1024;
    if (maximum <= 0) {
        ThumbnailProvider::clearCache();
        return;
    }
    QDir root(ThumbnailProvider::cacheDirectory());
    const QFileInfoList directories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    struct CacheItem {
        QString path;
        qint64 bytes = 0;
    };
    QList<CacheItem> items;
    qint64 total = 0;
    for (const QFileInfo &directory : directories) {
        CacheItem item{directory.absoluteFilePath(), 0};
        const QFileInfoList files = QDir(item.path).entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &file : files) {
            item.bytes += file.size();
        }
        total += item.bytes;
        items.append(item);
    }
    if (total <= maximum) {
        return;
    }
    const qint64 bytesToClear = maximum / 2;
    qint64 cleared = 0;
    for (const CacheItem &item : std::as_const(items)) {
        if (cleared >= bytesToClear) {
            break;
        }
        if (QDir(item.path).removeRecursively()) {
            total -= item.bytes;
            cleared += item.bytes;
        }
    }
}

void writeCache(
    const QUrl &url, const QString &key,
    const QList<MediaThumbnail> &thumbnails)
{
    if (QSettings().value(
            QStringLiteral("thumbnails/maxCacheMiB"), 500).toInt() <= 0) {
        return;
    }
    pruneCache();
    QDir directory(mediaCacheDirectory(key));
    if (directory.exists()) {
        directory.removeRecursively();
    }
    QDir().mkpath(directory.absolutePath());
    QJsonArray frames;
    for (int index = 0; index < thumbnails.size(); ++index) {
        const QString name =
            QStringLiteral("%1.jpg").arg(index, 3, 10, QLatin1Char('0'));
        if (!thumbnails[index].image.save(
                directory.filePath(name), "JPG", 75)) {
            directory.removeRecursively();
            return;
        }
        frames.append(QJsonObject{
            {QStringLiteral("file"), name},
            {QStringLiteral("seconds"), thumbnails[index].seconds}});
    }
    const QFileInfo source(url.toLocalFile());
    const QJsonObject root{
        {QStringLiteral("version"), cacheVersion},
        {QStringLiteral("fileSize"), static_cast<double>(source.size())},
        {QStringLiteral("modified"),
         source.lastModified().toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("frames"), frames}};
    QSaveFile metadata(
        directory.filePath(QStringLiteral("metadata.json")));
    if (metadata.open(QIODevice::WriteOnly)) {
        metadata.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        metadata.commit();
    }
}

QList<MediaThumbnail> generateThumbnails(
    const QUrl &url, double duration, int width,
    const std::atomic<quint64> &generation, quint64 expected)
{
    QList<MediaThumbnail> thumbnails;
    mpv_handle *mpv = mpv_create();
    if (!mpv) {
        return thumbnails;
    }
    const auto option = [mpv](const char *name, const char *value) {
        return mpv_set_option_string(mpv, name, value) >= 0;
    };
    option("config", "no");
    option("terminal", "no");
    option("input-default-bindings", "no");
    option("audio", "no");
    option("sub", "no");
    option("secondary-sid", "no");
    option("pause", "yes");
    option("hwdec", "no");
    option("vd-lavc-threads", "2");
    option("vo", "null");
    option("hr-seek", "no");
    if (mpv_initialize(mpv) < 0) {
        mpv_destroy(mpv);
        return thumbnails;
    }
    const QByteArray path = QFile::encodeName(url.toLocalFile());
    const char *load[] = {"loadfile", path.constData(), "replace", nullptr};
    if (mpv_command(mpv, load) < 0
        || !waitForEvent(
            mpv, MPV_EVENT_FILE_LOADED, generation, expected)) {
        mpv_terminate_destroy(mpv);
        return thumbnails;
    }
    waitForEvent(
        mpv, MPV_EVENT_PLAYBACK_RESTART, generation, expected, 4.0);
    thumbnails.reserve(thumbnailCount + 1);
    for (int index = 0; index <= thumbnailCount
         && !wasCancelled(generation, expected); ++index) {
        const double seconds =
            duration * static_cast<double>(index) / thumbnailCount;
        const QByteArray time = QByteArray::number(seconds, 'f', 3);
        const char *seek[] = {
            "seek", time.constData(), "absolute+keyframes", nullptr};
        if (mpv_command(mpv, seek) < 0
            || !waitForEvent(
                mpv, MPV_EVENT_PLAYBACK_RESTART,
                generation, expected, 4.0)) {
            continue;
        }
        QImage image = screenshotRaw(mpv);
        if (!image.isNull()) {
            image = image.scaledToWidth(
                std::max(80, width * 2), Qt::SmoothTransformation);
            thumbnails.append({seconds, std::move(image)});
        }
    }
    mpv_terminate_destroy(mpv);
    return thumbnails;
}

QImage readPreviewCache(const QUrl &url, const QString &key)
{
    const QFileInfo source(url.toLocalFile());
    const QDir directory(mediaCacheDirectory(key));
    QFile metadata(directory.filePath(QStringLiteral("preview.json")));
    if (!metadata.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject root =
        QJsonDocument::fromJson(metadata.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != cacheVersion
        || root.value(QStringLiteral("fileSize")).toDouble()
               != static_cast<double>(source.size())
        || root.value(QStringLiteral("modified")).toString()
               != source.lastModified().toUTC().toString(
                      Qt::ISODateWithMs)) {
        return {};
    }
    return QImage(directory.filePath(QStringLiteral("preview.png")));
}

void writePreviewCache(
    const QUrl &url, const QString &key, const QImage &image)
{
    QDir directory(mediaCacheDirectory(key));
    if (!directory.mkpath(QStringLiteral("."))) {
        return;
    }
    QSaveFile preview(
        directory.filePath(QStringLiteral("preview.png")));
    if (!preview.open(QIODevice::WriteOnly)) {
        return;
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly)
        || !image.save(&buffer, "PNG")) {
        return;
    }
    preview.write(encoded);
    if (!preview.commit()) {
        return;
    }
    const QFileInfo source(url.toLocalFile());
    const QJsonObject root{
        {QStringLiteral("version"), cacheVersion},
        {QStringLiteral("fileSize"), static_cast<double>(source.size())},
        {QStringLiteral("modified"),
         source.lastModified().toUTC().toString(Qt::ISODateWithMs)}};
    QSaveFile metadata(
        directory.filePath(QStringLiteral("preview.json")));
    if (metadata.open(QIODevice::WriteOnly)) {
        metadata.write(
            QJsonDocument(root).toJson(QJsonDocument::Compact));
        metadata.commit();
    }
}

QImage generatePreview(const QUrl &url, int width)
{
    mpv_handle *mpv = mpv_create();
    if (!mpv) {
        return {};
    }
    const auto option = [mpv](const char *name, const char *value) {
        return mpv_set_option_string(mpv, name, value) >= 0;
    };
    option("config", "no");
    option("terminal", "no");
    option("input-default-bindings", "no");
    option("audio", "no");
    option("sub", "no");
    option("secondary-sid", "no");
    option("pause", "yes");
    option("hwdec", "no");
    option("vd-lavc-threads", "2");
    option("vo", "null");
    option("hr-seek", "no");
    if (mpv_initialize(mpv) < 0) {
        mpv_destroy(mpv);
        return {};
    }
    std::atomic<quint64> generation{0};
    const QByteArray path = QFile::encodeName(url.toLocalFile());
    const char *load[] = {"loadfile", path.constData(), "replace", nullptr};
    if (mpv_command(mpv, load) < 0
        || !waitForEvent(
            mpv, MPV_EVENT_FILE_LOADED, generation, 0)) {
        mpv_terminate_destroy(mpv);
        return {};
    }
    waitForEvent(
        mpv, MPV_EVENT_PLAYBACK_RESTART, generation, 0, 4.0);
    double duration = 0.0;
    mpv_get_property(
        mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
    const double position =
        duration > 0.0 && duration < 20.0
        ? duration / 2.0 : 10.0;
    const QByteArray time = QByteArray::number(position, 'f', 3);
    const char *seek[] = {
        "seek", time.constData(), "absolute+keyframes", nullptr};
    if (mpv_command(mpv, seek) >= 0) {
        waitForEvent(
            mpv, MPV_EVENT_PLAYBACK_RESTART, generation, 0, 4.0);
    }
    QImage image = screenshotRaw(mpv);
    mpv_terminate_destroy(mpv);
    if (!image.isNull()) {
        image = image.scaledToWidth(
            std::max(80, width * 2), Qt::SmoothTransformation);
    }
    return image;
}
}

ThumbnailProvider::ThumbnailProvider(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

ThumbnailProvider::~ThumbnailProvider()
{
    m_generation.fetch_add(1, std::memory_order_relaxed);
    m_pool.waitForDone();
}

QString ThumbnailProvider::cacheDirectory()
{
    return QDir(QStandardPaths::writableLocation(
                    QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("thumb_cache"));
}

bool ThumbnailProvider::clearCache()
{
    QDir directory(cacheDirectory());
    return !directory.exists() || directory.removeRecursively();
}

QImage ThumbnailProvider::previewFor(
    const QUrl &url, int displayWidth)
{
    if (!url.isLocalFile()
        || !QFileInfo::exists(url.toLocalFile())) {
        return {};
    }
    const QString key = stableMediaKey(url);
    const QList<MediaThumbnail> cached = readCache(url, key);
    if (!cached.isEmpty()) {
        const double duration = cached.constLast().seconds;
        const double preferred =
            duration > 0.0 && duration < 20.0
            ? duration / 2.0 : 10.0;
        auto nearest = std::lower_bound(
            cached.cbegin(), cached.cend(), preferred,
            [](const MediaThumbnail &thumbnail, double target) {
                return thumbnail.seconds < target;
            });
        if (nearest == cached.cend()) {
            nearest = std::prev(cached.cend());
        }
        return nearest->image;
    }
    QImage preview = readPreviewCache(url, key);
    if (!preview.isNull()) {
        return preview;
    }
    preview = generatePreview(url, displayWidth);
    if (!preview.isNull()) {
        writePreviewCache(url, key, preview);
    }
    return preview;
}

void ThumbnailProvider::clear()
{
    m_generation.fetch_add(1, std::memory_order_relaxed);
    m_mediaKey.clear();
    m_thumbnails.clear();
    m_ready = false;
    m_progress = 0.0;
    emit thumbnailsChanged();
    emit thumbnailProgressChanged(0.0);
}

void ThumbnailProvider::request(
    const QUrl &url, double duration, int displayWidth)
{
    clear();
    if (!QSettings().value(
            QStringLiteral("thumbnails/enabled"), true).toBool()
        || !url.isLocalFile() || duration <= 0.0
        || !QFileInfo::exists(url.toLocalFile())) {
        return;
    }
    const QString key = stableMediaKey(url);
    const quint64 generation =
        m_generation.fetch_add(1, std::memory_order_relaxed) + 1;
    m_mediaKey = key;
    QPointer<ThumbnailProvider> guarded(this);
    m_pool.start(QRunnable::create(
        [this, guarded, generation, key, url, duration, displayWidth] {
            QList<MediaThumbnail> thumbnails = readCache(url, key);
            bool succeeded = !thumbnails.isEmpty();
            if (!succeeded && !wasCancelled(m_generation, generation)) {
                thumbnails = generateThumbnails(
                    url, duration, displayWidth,
                    m_generation, generation);
                succeeded = !thumbnails.isEmpty();
                if (succeeded
                    && !wasCancelled(m_generation, generation)) {
                    writeCache(url, key, thumbnails);
                }
            }
            if (!guarded || wasCancelled(m_generation, generation)) {
                return;
            }
            QMetaObject::invokeMethod(
                guarded,
                [guarded, generation, key,
                 thumbnails = std::move(thumbnails), succeeded] {
                    if (guarded) {
                        guarded->applyResult(
                            generation, key, thumbnails, succeeded);
                    }
                },
                Qt::QueuedConnection);
        }), -1);
}

void ThumbnailProvider::applyResult(
    quint64 generation, const QString &mediaKey,
    const QList<MediaThumbnail> &thumbnails, bool succeeded)
{
    if (generation != m_generation.load(std::memory_order_relaxed)
        || mediaKey != m_mediaKey) {
        return;
    }
    m_thumbnails = thumbnails;
    m_ready = succeeded;
    m_progress = succeeded ? 1.0 : 0.0;
    emit thumbnailsChanged();
    emit thumbnailProgressChanged(m_progress);
    if (succeeded) {
        emit thumbnailsReady();
    }
}

QImage ThumbnailProvider::imageAt(double seconds) const
{
    if (m_thumbnails.isEmpty()) {
        return {};
    }
    auto next = std::lower_bound(
        m_thumbnails.cbegin(), m_thumbnails.cend(), seconds,
        [](const MediaThumbnail &thumbnail, double target) {
            return thumbnail.seconds < target;
        });
    if (next == m_thumbnails.cbegin()) {
        return next->image;
    }
    if (next == m_thumbnails.cend()) {
        return m_thumbnails.constLast().image;
    }
    return std::prev(next)->image;
}
