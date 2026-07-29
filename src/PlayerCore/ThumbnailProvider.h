#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QThreadPool>
#include <QUrl>

#include <atomic>

struct MediaThumbnail {
    double seconds = 0.0;
    QImage image;
};

class ThumbnailProvider final : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailProvider(QObject *parent = nullptr);
    ~ThumbnailProvider() override;

    void request(const QUrl &url, double duration, int displayWidth = 120);
    void clear();
    [[nodiscard]] QImage imageAt(double seconds) const;
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] double progress() const noexcept { return m_progress; }

    [[nodiscard]] static QString cacheDirectory();
    static bool clearCache();
    [[nodiscard]] static QImage previewFor(
        const QUrl &url, int displayWidth = 160);

signals:
    void thumbnailsChanged();
    void thumbnailProgressChanged(double progress);
    void thumbnailsReady();

private:
    void applyResult(
        quint64 generation, const QString &mediaKey,
        const QList<MediaThumbnail> &thumbnails, bool succeeded);

    QThreadPool m_pool;
    std::atomic<quint64> m_generation{0};
    QString m_mediaKey;
    QList<MediaThumbnail> m_thumbnails;
    double m_progress = 0.0;
    bool m_ready = false;
};
