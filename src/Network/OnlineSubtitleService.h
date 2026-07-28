#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

enum class OnlineSubtitleProvider {
    OpenSubtitles,
    Assrt,
    Shooter,
};

struct OnlineSubtitleResult {
    OnlineSubtitleProvider provider = OnlineSubtitleProvider::OpenSubtitles;
    QString id;
    QString title;
    QString language;
    QString format;
    QString details;
    QString uploaded;
    int downloads = 0;
    QVariantMap payload;
};

class OnlineSubtitleService final : public QObject {
    Q_OBJECT

public:
    explicit OnlineSubtitleService(QObject *parent = nullptr);

    void search(OnlineSubtitleProvider provider, const QUrl &mediaUrl,
                const QString &query, const QStringList &languages);
    void download(const OnlineSubtitleResult &result);
    void cancel();

    [[nodiscard]] static QString openSubtitlesHash(
        const QString &filePath, QString *error = nullptr);
    [[nodiscard]] static QString shooterHash(
        const QString &filePath, QString *error = nullptr);
    [[nodiscard]] static QString safeFileName(const QString &fileName);

signals:
    void statusChanged(const QString &message);
    void searchFinished(const QList<OnlineSubtitleResult> &results);
    void downloadFinished(const QStringList &filePaths);
    void failed(const QString &message);

private:
    void searchOpenSubtitles(const QUrl &mediaUrl, const QString &query,
                            const QStringList &languages);
    void searchAssrt(const QString &query);
    void searchShooter(const QUrl &mediaUrl);
    void downloadOpenSubtitles(const OnlineSubtitleResult &result);
    void downloadAssrt(const OnlineSubtitleResult &result);
    void downloadShooter(const OnlineSubtitleResult &result);
    void downloadFiles(const QList<QPair<QUrl, QString>> &files);
    void track(QNetworkReply *reply);
    void failReply(QNetworkReply *reply, const QString &operation);

    QNetworkAccessManager *m_network = nullptr;
    QList<QNetworkReply *> m_activeReplies;
    quint64 m_generation = 0;
};

Q_DECLARE_METATYPE(OnlineSubtitleResult)
