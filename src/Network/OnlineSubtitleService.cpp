#include "Network/OnlineSubtitleService.h"
#include "Network/SecureCredentialStore.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrlQuery>

#include <array>
#include <memory>

namespace {
constexpr qint64 maximumSubtitleBytes = 20 * 1024 * 1024;

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30'000);
    request.setRawHeader("Accept", "application/json");
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("Supernova/%1").arg(
            QCoreApplication::applicationVersion()));
    return request;
}

QString networkError(QNetworkReply *reply, const QString &operation)
{
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString detail = reply->errorString();
    const QJsonDocument body = QJsonDocument::fromJson(reply->readAll());
    if (body.isObject()) {
        const QString message =
            body.object().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            detail = message;
        }
    }
    return status > 0
        ? QObject::tr("%1 failed (HTTP %2): %3")
              .arg(operation).arg(status).arg(detail)
        : QObject::tr("%1 failed: %2").arg(operation, detail);
}

bool isSafeDownloadUrl(const QUrl &url)
{
    return url.isValid()
        && (url.scheme().compare(QStringLiteral("https"),
                                Qt::CaseInsensitive) == 0
            || url.scheme().compare(QStringLiteral("http"),
                                    Qt::CaseInsensitive) == 0);
}
}

OnlineSubtitleService::OnlineSubtitleService(QObject *parent)
    : QObject(parent),
      m_network(new QNetworkAccessManager(this))
{
    const QString proxyText = QSettings().value(
        QStringLiteral("network/proxy")).toString().trimmed();
    if (!proxyText.isEmpty()) {
        const QUrl proxyUrl = QUrl::fromUserInput(proxyText);
        if (proxyUrl.isValid() && !proxyUrl.host().isEmpty()) {
            QNetworkProxy proxy(
                proxyUrl.scheme().startsWith(
                    QStringLiteral("socks"), Qt::CaseInsensitive)
                    ? QNetworkProxy::Socks5Proxy
                    : QNetworkProxy::HttpProxy,
                proxyUrl.host(),
                static_cast<quint16>(proxyUrl.port(8080)),
                proxyUrl.userName(), proxyUrl.password());
            m_network->setProxy(proxy);
        }
    }
}

void OnlineSubtitleService::cancel()
{
    ++m_generation;
    const auto replies = m_activeReplies;
    m_activeReplies.clear();
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
    }
}

void OnlineSubtitleService::search(
    OnlineSubtitleProvider provider, const QUrl &mediaUrl,
    const QString &query, const QStringList &languages)
{
    cancel();
    emit statusChanged(tr("Searching…"));
    switch (provider) {
    case OnlineSubtitleProvider::OpenSubtitles:
        searchOpenSubtitles(mediaUrl, query, languages);
        break;
    case OnlineSubtitleProvider::Assrt:
        searchAssrt(query);
        break;
    case OnlineSubtitleProvider::Shooter:
        searchShooter(mediaUrl);
        break;
    }
}

void OnlineSubtitleService::download(
    const OnlineSubtitleResult &result)
{
    cancel();
    emit statusChanged(tr("Preparing download…"));
    switch (result.provider) {
    case OnlineSubtitleProvider::OpenSubtitles:
        downloadOpenSubtitles(result);
        break;
    case OnlineSubtitleProvider::Assrt:
        downloadAssrt(result);
        break;
    case OnlineSubtitleProvider::Shooter:
        downloadShooter(result);
        break;
    }
}

QString OnlineSubtitleService::openSubtitlesHash(
    const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    constexpr qint64 chunk = 65'536;
    const qint64 size = file.size();
    if (size <= chunk * 2) {
        if (error) *error = tr("The media file is too small to hash.");
        return {};
    }
    quint64 hash = static_cast<quint64>(size);
    const auto addChunk = [&file, &hash](qint64 offset) {
        if (!file.seek(offset)) return false;
        const QByteArray bytes = file.read(65'536);
        if (bytes.size() != 65'536) return false;
        for (qsizetype i = 0; i + 7 < bytes.size(); i += 8) {
            quint64 value = 0;
            for (int byte = 0; byte < 8; ++byte) {
                value |= static_cast<quint64>(
                    static_cast<unsigned char>(bytes.at(i + byte)))
                    << (byte * 8);
            }
            hash += value;
        }
        return true;
    };
    if (!addChunk(0) || !addChunk(size - chunk)) {
        if (error) *error = tr("Could not read the media file.");
        return {};
    }
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QString OnlineSubtitleService::shooterHash(
    const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    const qint64 size = file.size();
    constexpr qint64 chunk = 4096;
    if (size < 12'288) {
        if (error) *error = tr("The media file is too small to hash.");
        return {};
    }
    const std::array<qint64, 4> offsets{
        4096, size / 3 * 2, size / 3, size - 8192};
    QStringList hashes;
    for (qint64 offset : offsets) {
        if (!file.seek(offset)) {
            if (error) *error = tr("Could not read the media file.");
            return {};
        }
        hashes.append(QString::fromLatin1(
            QCryptographicHash::hash(
                file.read(chunk), QCryptographicHash::Md5).toHex()));
    }
    return hashes.join(QLatin1Char(';'));
}

QString OnlineSubtitleService::safeFileName(const QString &fileName)
{
    QString result = QFileInfo(fileName).fileName().trimmed();
    result.replace(
        QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])")),
        QStringLiteral("_"));
    while (result.endsWith(QLatin1Char('.'))
           || result.endsWith(QLatin1Char(' '))) {
        result.chop(1);
    }
    return result.isEmpty() ? QStringLiteral("subtitle.srt") : result.left(180);
}

void OnlineSubtitleService::searchOpenSubtitles(
    const QUrl &mediaUrl, const QString &query,
    const QStringList &languages)
{
    const QString apiKey = SecureCredentialStore::read(
        QStringLiteral("openSubtitlesApiKey")).trimmed();
    if (apiKey.isEmpty()) {
        emit failed(tr("Set an OpenSubtitles API key in Preferences first."));
        return;
    }
    QUrl url(QStringLiteral(
        "https://api.opensubtitles.com/api/v1/subtitles"));
    QUrlQuery parameters;
    if (!query.trimmed().isEmpty()) {
        parameters.addQueryItem(QStringLiteral("query"), query.trimmed());
    }
    if (!languages.isEmpty()) {
        parameters.addQueryItem(
            QStringLiteral("languages"), languages.join(QLatin1Char(',')));
    }
    if (mediaUrl.isLocalFile()) {
        const QString hash = openSubtitlesHash(mediaUrl.toLocalFile());
        if (!hash.isEmpty()) {
            parameters.addQueryItem(QStringLiteral("moviehash"), hash);
        }
    }
    url.setQuery(parameters);
    QNetworkRequest request = requestFor(url);
    request.setRawHeader("Api-Key", apiKey.toUtf8());
    request.setRawHeader("User-Agent", "Supernova Player v0.1");
    QNetworkReply *reply = m_network->get(request);
    const quint64 generation = m_generation;
    track(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        m_activeReplies.removeAll(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            failReply(reply, tr("OpenSubtitles search"));
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        QList<OnlineSubtitleResult> results;
        for (const QJsonValue &value :
             root.value(QStringLiteral("data")).toArray()) {
            const QJsonObject item = value.toObject();
            const QJsonObject attributes =
                item.value(QStringLiteral("attributes")).toObject();
            const QJsonArray files =
                attributes.value(QStringLiteral("files")).toArray();
            if (files.isEmpty()) continue;
            const QJsonObject file = files.at(0).toObject();
            const QJsonObject feature =
                attributes.value(QStringLiteral("feature_details")).toObject();
            OnlineSubtitleResult result;
            result.provider = OnlineSubtitleProvider::OpenSubtitles;
            result.id = item.value(QStringLiteral("id")).toVariant().toString();
            result.title = attributes.value(
                QStringLiteral("release")).toString();
            if (result.title.isEmpty()) {
                result.title = file.value(
                    QStringLiteral("file_name")).toString();
            }
            result.language =
                attributes.value(QStringLiteral("language")).toString();
            result.format = QFileInfo(
                file.value(QStringLiteral("file_name")).toString()).suffix();
            result.downloads = attributes.value(
                QStringLiteral("download_count")).toInt();
            result.uploaded = attributes.value(
                QStringLiteral("upload_date")).toString().left(10);
            result.details = QStringLiteral("%1 · %2")
                .arg(feature.value(QStringLiteral("title")).toString(),
                     attributes.value(QStringLiteral("uploader"))
                         .toObject().value(QStringLiteral("name")).toString());
            result.payload.insert(
                QStringLiteral("fileId"),
                file.value(QStringLiteral("file_id")).toInt());
            result.payload.insert(
                QStringLiteral("fileName"),
                file.value(QStringLiteral("file_name")).toString());
            results.append(std::move(result));
        }
        reply->deleteLater();
        emit statusChanged(
            tr("%1 subtitle results").arg(results.size()));
        emit searchFinished(results);
    });
}

void OnlineSubtitleService::searchAssrt(const QString &query)
{
    const QString token = SecureCredentialStore::read(
        QStringLiteral("assrtToken")).trimmed();
    if (token.isEmpty()) {
        emit failed(tr("Set an Assrt API token in Preferences first."));
        return;
    }
    QNetworkRequest request = requestFor(
        QUrl(QStringLiteral("https://api.assrt.net/v1/sub/search")));
    request.setRawHeader(
        "Content-Type", "application/x-www-form-urlencoded");
    request.setRawHeader(
        "Authorization", QByteArray("Bearer ") + token.toUtf8());
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("q"), query.trimmed());
    QNetworkReply *reply =
        m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    const quint64 generation = m_generation;
    track(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        m_activeReplies.removeAll(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            failReply(reply, tr("Assrt search"));
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        if (root.value(QStringLiteral("status")).toInt() != 0) {
            const QString message =
                root.value(QStringLiteral("errmsg")).toString();
            reply->deleteLater();
            emit failed(message.isEmpty()
                            ? tr("Assrt rejected the search.") : message);
            return;
        }
        QList<OnlineSubtitleResult> results;
        const QJsonArray subtitles = root.value(QStringLiteral("sub"))
            .toObject().value(QStringLiteral("subs")).toArray();
        for (const QJsonValue &value : subtitles) {
            const QJsonObject item = value.toObject();
            OnlineSubtitleResult result;
            result.provider = OnlineSubtitleProvider::Assrt;
            result.id =
                item.value(QStringLiteral("id")).toVariant().toString();
            result.title =
                item.value(QStringLiteral("native_name")).toString();
            result.format =
                item.value(QStringLiteral("subtype")).toString();
            result.language = item.value(QStringLiteral("lang"))
                .toObject().value(QStringLiteral("desc")).toString();
            result.uploaded =
                item.value(QStringLiteral("upload_time")).toString();
            results.append(std::move(result));
        }
        reply->deleteLater();
        emit statusChanged(
            tr("%1 subtitle results").arg(results.size()));
        emit searchFinished(results);
    });
}

void OnlineSubtitleService::searchShooter(const QUrl &mediaUrl)
{
    if (!mediaUrl.isLocalFile()) {
        emit failed(tr("Shooter search requires a local media file."));
        return;
    }
    QString hashError;
    const QString hash = shooterHash(mediaUrl.toLocalFile(), &hashError);
    if (hash.isEmpty()) {
        emit failed(hashError);
        return;
    }
    QNetworkRequest request = requestFor(
        QUrl(QStringLiteral("https://www.shooter.cn/api/subapi.php")));
    request.setRawHeader(
        "Content-Type", "application/x-www-form-urlencoded");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("filehash"), hash);
    form.addQueryItem(
        QStringLiteral("pathinfo"), mediaUrl.toLocalFile());
    form.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    QNetworkReply *reply =
        m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    const quint64 generation = m_generation;
    track(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        m_activeReplies.removeAll(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            failReply(reply, tr("Shooter search"));
            return;
        }
        const QJsonArray root =
            QJsonDocument::fromJson(reply->readAll()).array();
        QList<OnlineSubtitleResult> results;
        int index = 0;
        for (const QJsonValue &value : root) {
            const QJsonObject item = value.toObject();
            const QJsonArray files =
                item.value(QStringLiteral("Files")).toArray();
            if (files.isEmpty()) continue;
            const QJsonObject file = files.at(0).toObject();
            OnlineSubtitleResult result;
            result.provider = OnlineSubtitleProvider::Shooter;
            result.id = QString::number(++index);
            result.title =
                item.value(QStringLiteral("Desc")).toString();
            if (result.title.isEmpty()) {
                result.title = tr("Shooter subtitle %1").arg(index);
            }
            result.format =
                file.value(QStringLiteral("Ext")).toString();
            result.details = tr("Delay: %1 ms").arg(
                item.value(QStringLiteral("Delay")).toInt());
            result.payload.insert(
                QStringLiteral("url"),
                file.value(QStringLiteral("Link")).toString());
            result.payload.insert(
                QStringLiteral("fileName"),
                QStringLiteral("shooter-%1.%2")
                    .arg(index).arg(result.format));
            results.append(std::move(result));
        }
        reply->deleteLater();
        emit statusChanged(
            tr("%1 subtitle results").arg(results.size()));
        emit searchFinished(results);
    });
}

void OnlineSubtitleService::downloadOpenSubtitles(
    const OnlineSubtitleResult &result)
{
    const QString apiKey = SecureCredentialStore::read(
        QStringLiteral("openSubtitlesApiKey")).trimmed();
    QNetworkRequest request = requestFor(
        QUrl(QStringLiteral("https://api.opensubtitles.com/api/v1/download")));
    request.setRawHeader("Api-Key", apiKey.toUtf8());
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("User-Agent", "Supernova Player v0.1");
    const QString token = SecureCredentialStore::read(
        QStringLiteral("openSubtitlesToken")).trimmed();
    if (!token.isEmpty()) {
        request.setRawHeader(
            "Authorization", QByteArray("Bearer ") + token.toUtf8());
    }
    const QJsonObject body{
        {QStringLiteral("file_id"),
         result.payload.value(QStringLiteral("fileId")).toInt()}};
    QNetworkReply *reply =
        m_network->post(request, QJsonDocument(body).toJson(
                                     QJsonDocument::Compact));
    const quint64 generation = m_generation;
    track(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, result, generation] {
        m_activeReplies.removeAll(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            failReply(reply, tr("OpenSubtitles download request"));
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QUrl link(root.value(QStringLiteral("link")).toString());
        QString name =
            root.value(QStringLiteral("file_name")).toString();
        if (name.isEmpty()) {
            name = result.payload.value(
                QStringLiteral("fileName")).toString();
        }
        reply->deleteLater();
        downloadFiles({{link, name}});
    });
}

void OnlineSubtitleService::downloadAssrt(
    const OnlineSubtitleResult &result)
{
    const QString token = SecureCredentialStore::read(
        QStringLiteral("assrtToken")).trimmed();
    QNetworkRequest request = requestFor(
        QUrl(QStringLiteral("https://api.assrt.net/v1/sub/detail")));
    request.setRawHeader(
        "Content-Type", "application/x-www-form-urlencoded");
    request.setRawHeader(
        "Authorization", QByteArray("Bearer ") + token.toUtf8());
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("id"), result.id);
    QNetworkReply *reply =
        m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    const quint64 generation = m_generation;
    track(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation] {
        m_activeReplies.removeAll(reply);
        if (generation != m_generation) {
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            failReply(reply, tr("Assrt subtitle details"));
            return;
        }
        const QJsonObject root =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray subtitles = root.value(QStringLiteral("sub"))
            .toObject().value(QStringLiteral("subs")).toArray();
        QList<QPair<QUrl, QString>> files;
        if (!subtitles.isEmpty()) {
            const QJsonObject subtitle = subtitles.at(0).toObject();
            for (const QJsonValue &value :
                 subtitle.value(QStringLiteral("filelist")).toArray()) {
                const QJsonObject file = value.toObject();
                files.append({
                    QUrl(file.value(QStringLiteral("url")).toString()),
                    file.value(QStringLiteral("f")).toString()});
            }
            if (files.isEmpty()) {
                files.append({
                    QUrl(subtitle.value(QStringLiteral("url")).toString()),
                    subtitle.value(QStringLiteral("filename")).toString()});
            }
        }
        reply->deleteLater();
        if (files.isEmpty()) {
            emit failed(tr("Assrt returned no downloadable files."));
            return;
        }
        downloadFiles(files);
    });
}

void OnlineSubtitleService::downloadShooter(
    const OnlineSubtitleResult &result)
{
    downloadFiles({{
        QUrl(result.payload.value(QStringLiteral("url")).toString()),
        result.payload.value(QStringLiteral("fileName")).toString()}});
}

void OnlineSubtitleService::downloadFiles(
    const QList<QPair<QUrl, QString>> &files)
{
    if (files.isEmpty()) {
        emit failed(tr("The provider returned no downloadable files."));
        return;
    }
    for (const auto &[url, name] : files) {
        Q_UNUSED(name);
        if (!isSafeDownloadUrl(url)) {
            emit failed(tr("The provider returned an unsafe download URL."));
            return;
        }
    }
    const quint64 generation = m_generation;
    auto paths = std::make_shared<QStringList>();
    auto remaining = std::make_shared<int>(files.size());
    for (const auto &[url, name] : files) {
        QNetworkReply *reply = m_network->get(requestFor(url));
        track(reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, generation, name, paths, remaining] {
            m_activeReplies.removeAll(reply);
            if (generation != m_generation) {
                reply->deleteLater();
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                failReply(reply, tr("Subtitle download"));
                cancel();
                return;
            }
            const QByteArray data = reply->readAll();
            reply->deleteLater();
            if (data.isEmpty() || data.size() > maximumSubtitleBytes) {
                emit failed(tr("The downloaded subtitle is empty or too large."));
                cancel();
                return;
            }
            const QString folder = QDir(
                QStandardPaths::writableLocation(
                    QStandardPaths::AppLocalDataLocation))
                .filePath(QStringLiteral("subtitles"));
            QDir().mkpath(folder);
            QString path = QDir(folder).filePath(safeFileName(name));
            const QFileInfo info(path);
            for (int suffix = 1; QFileInfo::exists(path); ++suffix) {
                path = QDir(folder).filePath(
                    QStringLiteral("%1-%2.%3")
                        .arg(info.completeBaseName()).arg(suffix)
                        .arg(info.suffix().isEmpty()
                                 ? QStringLiteral("srt") : info.suffix()));
            }
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)
                || output.write(data) != data.size()
                || !output.commit()) {
                emit failed(tr("Could not save the downloaded subtitle."));
                cancel();
                return;
            }
            paths->append(path);
            --*remaining;
            if (*remaining == 0) {
                emit statusChanged(
                    tr("Downloaded and loaded %1 subtitle file(s).")
                        .arg(paths->size()));
                emit downloadFinished(*paths);
            }
        });
    }
}

void OnlineSubtitleService::track(QNetworkReply *reply)
{
    m_activeReplies.append(reply);
}

void OnlineSubtitleService::failReply(
    QNetworkReply *reply, const QString &operation)
{
    const QString message = networkError(reply, operation);
    reply->deleteLater();
    emit failed(message);
}
