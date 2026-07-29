#include "Plugins/PluginRuntime.h"

#include "Core/Logger.h"
#include "Mpv/MpvCore.h"
#include "Network/SecureCredentialStore.h"
#include "PlayerCore/PlayerCore.h"
#include "PlayerCore/PlayerState.h"
#include "UI/MainWindow/MainWindow.h"

#include <QCoreApplication>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#include <cmath>

namespace {
QVariantMap serializeTrack(const MediaTrack &track)
{
    return {
        {QStringLiteral("id"), track.id},
        {QStringLiteral("title"), track.title},
        {QStringLiteral("formattedTitle"), track.readableTitle()},
        {QStringLiteral("lang"), track.language},
        {QStringLiteral("codec"), track.codec},
        {QStringLiteral("isDefault"), track.isDefault},
        {QStringLiteral("isForced"), track.isForced},
        {QStringLiteral("isSelected"), track.isSelected},
        {QStringLiteral("isExternal"), track.isExternal},
        {QStringLiteral("demuxW"), track.width},
        {QStringLiteral("demuxH"), track.height},
        {QStringLiteral("demuxChannelCount"), track.channelCount},
        {QStringLiteral("demuxChannels"), track.channelLayout},
        {QStringLiteral("demuxSamplerate"), track.sampleRate},
        {QStringLiteral("demuxFPS"), track.frameRate},
    };
}

QVariantList serializeTracks(const QList<MediaTrack> &tracks)
{
    QVariantList result;
    for (const MediaTrack &track : tracks) {
        result.append(serializeTrack(track));
    }
    return result;
}

QUrl pluginUrl(const QString &value)
{
    const QUrl parsed(value);
    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        return parsed;
    }
    return QUrl::fromLocalFile(QFileInfo(value).absoluteFilePath());
}

QString settingsGroup(const PluginPackage &package)
{
    return QStringLiteral("pluginPreferences/") + package.manifest.identifier;
}
}

PluginApiBase::PluginApiBase(PluginRuntime *runtime)
    : QObject(runtime), m_runtime(runtime)
{
}

PluginRuntime *PluginApiBase::runtime() const { return m_runtime; }
PlayerCore *PluginApiBase::player() const { return m_runtime->player(); }
MainWindow *PluginApiBase::window() const { return m_runtime->window(); }

bool PluginApiBase::requirePermission(
    PluginPermission permission, const QString &operation)
{
    if (runtime()->package().manifest.hasPermission(permission)) {
        return true;
    }
    runtime()->throwError(
        QStringLiteral("%1 requires permission “%2”.")
            .arg(operation, pluginPermissionName(permission)));
    return false;
}

void PluginCoreApi::open(const QString &url)
{
    if (player()) player()->openUrl(pluginUrl(url));
}
void PluginCoreApi::osd(const QString &message)
{
    if (requirePermission(PluginPermission::ShowOsd,
                          QStringLiteral("core.osd"))) {
        emit runtime()->osdRequested(message);
    }
}
void PluginCoreApi::pause() { if (player()) player()->pause(); }
void PluginCoreApi::resume() { if (player()) player()->resume(); }
void PluginCoreApi::stop() { if (player()) player()->stop(); }
void PluginCoreApi::seek(double seconds, bool exact)
{
    if (player()) player()->seekRelative(seconds, exact);
}
void PluginCoreApi::seekTo(double seconds)
{
    if (player()) player()->seekAbsolute(seconds);
}
void PluginCoreApi::setSpeed(double speed)
{
    if (player()) player()->setSpeed(speed);
}
QVariantList PluginCoreApi::getChapters() const
{
    QVariantList chapters;
    if (!player()) return chapters;
    for (const PlaybackChapter &chapter : player()->info().chapters) {
        chapters.append(QVariantMap{
            {QStringLiteral("title"), chapter.title},
            {QStringLiteral("start"), chapter.startTimeSec}});
    }
    return chapters;
}
void PluginCoreApi::playChapter(int index)
{
    if (player()) player()->playChapter(index);
}
QVariantList PluginCoreApi::getHistory() const
{
    QVariantList result;
    if (!player()) return result;
    for (const PlaybackHistoryEntry &item : player()->history()) {
        result.append(QVariantMap{
            {QStringLiteral("name"), item.url.fileName()},
            {QStringLiteral("url"), item.url.toString()},
            {QStringLiteral("date"), item.lastPlayed},
            {QStringLiteral("progress"), item.positionSec},
            {QStringLiteral("duration"), item.durationSec}});
    }
    return result;
}
QVariantList PluginCoreApi::getRecentDocuments() const
{
    QVariantList result;
    if (!player()) return result;
    for (const RecentMediaEntry &item : player()->recentMedia()) {
        result.append(QVariantMap{
            {QStringLiteral("name"), item.url.fileName()},
            {QStringLiteral("url"), item.url.toString()}});
    }
    return result;
}
QVariantMap PluginCoreApi::getVersion() const
{
    return {
        {QStringLiteral("iina"),
         QCoreApplication::applicationVersion()},
        {QStringLiteral("build"),
         QStringLiteral(SUPERNOVA_CHANNEL)},
        {QStringLiteral("mpv"),
         player() ? player()->mpvPropertyString(
                        QStringLiteral("mpv-version"))
                  : QString()}};
}
QVariantMap PluginCoreApi::status() const
{
    if (!player()) return {};
    const PlaybackInfo &info = player()->info();
    return {
        {QStringLiteral("paused"), info.state == PlayerState::Paused},
        {QStringLiteral("idle"), info.state == PlayerState::Idle},
        {QStringLiteral("position"), info.videoPositionSec},
        {QStringLiteral("duration"), info.videoDurationSec},
        {QStringLiteral("speed"), info.playSpeed},
        {QStringLiteral("videoWidth"), info.videoWidth},
        {QStringLiteral("videoHeight"), info.videoHeight},
        {QStringLiteral("isNetworkResource"), info.isNetworkResource},
        {QStringLiteral("url"), info.currentUrl.toString()},
        {QStringLiteral("title"), info.currentUrl.fileName()}};
}
QVariantMap PluginCoreApi::windowStatus() const
{
    if (!window()) return {{QStringLiteral("loaded"), false}};
    const QRect frame = window()->geometry();
    return {
        {QStringLiteral("loaded"), true},
        {QStringLiteral("visible"), window()->isVisible()},
        {QStringLiteral("fullscreen"), window()->isFullScreenMode()},
        {QStringLiteral("miniaturized"), window()->isMinimized()},
        {QStringLiteral("frame"), QVariantMap{
             {QStringLiteral("x"), frame.x()},
             {QStringLiteral("y"), frame.y()},
             {QStringLiteral("width"), frame.width()},
             {QStringLiteral("height"), frame.height()}}}};
}
QVariantMap PluginCoreApi::audio() const
{
    if (!player()) return {};
    const auto &info = player()->info();
    return {
        {QStringLiteral("id"), info.tracks.selectedAudioId},
        {QStringLiteral("tracks"), serializeTracks(info.tracks.audioTracks)},
        {QStringLiteral("delay"), info.audioSettings.delay},
        {QStringLiteral("volume"), info.volume},
        {QStringLiteral("muted"), info.isMuted}};
}
QVariantMap PluginCoreApi::video() const
{
    if (!player()) return {};
    const auto &tracks = player()->info().tracks;
    return {
        {QStringLiteral("id"), tracks.selectedVideoId},
        {QStringLiteral("tracks"), serializeTracks(tracks.videoTracks)}};
}
QVariantMap PluginCoreApi::subtitle() const
{
    if (!player()) return {};
    const auto &info = player()->info();
    return {
        {QStringLiteral("id"), info.tracks.selectedSubtitleId},
        {QStringLiteral("secondID"),
         info.tracks.selectedSecondarySubtitleId},
        {QStringLiteral("tracks"),
         serializeTracks(info.tracks.subtitleTracks)},
        {QStringLiteral("delay"), info.subtitles.primaryDelay}};
}
void PluginCoreApi::setAudioProperty(
    const QString &name, const QVariant &value)
{
    if (!player()) return;
    if (name == QStringLiteral("id"))
        player()->setTrack(MediaTrackType::Audio, value.toInt());
    else if (name == QStringLiteral("delay"))
        player()->setAudioDelay(value.toDouble());
    else if (name == QStringLiteral("volume"))
        player()->setVolume(value.toDouble());
    else if (name == QStringLiteral("muted")
             && value.toBool() != player()->info().isMuted)
        player()->toggleMute();
}
void PluginCoreApi::setVideoProperty(
    const QString &name, const QVariant &value)
{
    if (player() && name == QStringLiteral("id"))
        player()->setTrack(MediaTrackType::Video, value.toInt());
}
void PluginCoreApi::setSubtitleProperty(
    const QString &name, const QVariant &value)
{
    if (!player()) return;
    if (name == QStringLiteral("id"))
        player()->setSubtitleTrack(true, value.toInt());
    else if (name == QStringLiteral("secondID"))
        player()->setSubtitleTrack(false, value.toInt());
    else if (name == QStringLiteral("delay"))
        player()->setSubtitleDelay(true, value.toDouble());
}
void PluginCoreApi::setWindowProperty(
    const QString &name, const QVariant &value)
{
    if (!window()) return;
    if (name == QStringLiteral("fullscreen")
        && value.toBool() != window()->isFullScreenMode()) {
        window()->toggleFullScreen();
    } else if (name == QStringLiteral("miniaturized")) {
        value.toBool() ? window()->showMinimized()
                       : window()->showNormal();
    } else if (name == QStringLiteral("frame")) {
        const QVariantMap frame = value.toMap();
        const QRect geometry(
            frame.value(QStringLiteral("x")).toInt(),
            frame.value(QStringLiteral("y")).toInt(),
            frame.value(QStringLiteral("width")).toInt(),
            frame.value(QStringLiteral("height")).toInt());
        if (geometry.width() > 0 && geometry.height() > 0)
            window()->setGeometry(geometry);
    }
}
void PluginCoreApi::loadTrack(
    const QString &type, const QString &path)
{
    if (!player()) return;
    const QUrl url = pluginUrl(path);
    if (type == QStringLiteral("audio")) player()->loadExternalAudio(url);
    else if (type == QStringLiteral("video"))
        player()->loadExternalVideo(url);
    else player()->loadExternalSubtitle(url);
}

bool PluginMpvApi::getFlag(const QString &property) const
{ return player() && player()->mpvCore()->getFlag(property); }
double PluginMpvApi::getNumber(const QString &property) const
{ return player() ? player()->mpvCore()->getDouble(property) : 0.0; }
QString PluginMpvApi::getString(const QString &property) const
{ return player() ? player()->mpvCore()->getString(property) : QString(); }
QVariant PluginMpvApi::getNative(const QString &property) const
{ return player() ? player()->mpvCore()->getNode(property) : QVariant(); }
void PluginMpvApi::set(
    const QString &property, const QVariant &value)
{
    if (!player()) return;
    MpvCore *mpv = player()->mpvCore();
    switch (value.metaType().id()) {
    case QMetaType::Bool: mpv->setFlag(property, value.toBool()); break;
    case QMetaType::Int:
    case QMetaType::LongLong:
        mpv->setInt(property, value.toLongLong()); break;
    case QMetaType::Double:
        mpv->setDouble(property, value.toDouble()); break;
    default: mpv->setString(property, value.toString()); break;
    }
}
void PluginMpvApi::command(
    const QString &name, const QVariantList &arguments)
{
    if (!player()) return;
    QVariantList command{name};
    command.append(arguments);
    player()->mpvCore()->command(command);
}
QString PluginMpvApi::addHook(
    const QString &name, int priority, const QJSValue &callback)
{
    if (!player() || !callback.isCallable()) return {};
    const quint64 id = player()->mpvCore()->addHook(
        name, priority,
        [callback](const QString &, MpvCore::HookContinuation next) mutable {
            const QJSValue result = callback.call();
            if (result.isError()) {
                Logger::error(QStringLiteral("Plugin mpv hook failed: %1")
                                  .arg(result.toString()));
            }
            next();
        });
    m_hooks.append(id);
    return QString::number(id);
}
PluginMpvApi::~PluginMpvApi()
{
    if (player()) for (quint64 id : std::as_const(m_hooks))
        player()->mpvCore()->removeHook(id);
}

QVariantList PluginPlaylistApi::list() const
{
    QVariantList result;
    if (!player()) return result;
    for (const PlaylistItem &item : player()->info().playlist.items) {
        result.append(QVariantMap{
            {QStringLiteral("filename"), item.url.toString()},
            {QStringLiteral("title"), item.title},
            {QStringLiteral("isPlaying"), item.playing},
            {QStringLiteral("isCurrent"), item.current}});
    }
    return result;
}
int PluginPlaylistApi::count() const
{ return player() ? player()->info().playlist.size() : 0; }
bool PluginPlaylistApi::add(const QVariant &url, int at)
{
    if (!player()) return false;
    QList<QUrl> urls;
    const QStringList values = url.typeId() == QMetaType::QStringList
        ? url.toStringList() : QStringList{url.toString()};
    for (const QString &value : values) urls.append(pluginUrl(value));
    if (urls.isEmpty() || at >= count()) return false;
    player()->appendToPlaylist(urls, at);
    return true;
}
bool PluginPlaylistApi::remove(const QVariant &index)
{
    if (!player()) return false;
    QList<int> indexes;
    if (index.canConvert<QVariantList>()) {
        for (const QVariant &item : index.toList())
            indexes.append(item.toInt());
    } else indexes.append(index.toInt());
    for (int value : indexes)
        if (value < 0 || value >= count()) return false;
    player()->removePlaylistItems(indexes);
    return true;
}
bool PluginPlaylistApi::move(int index, int to)
{
    if (!player() || index < 0 || to < 0
        || index >= count() || to >= count() || index == to) return false;
    player()->movePlaylistItems({index}, to);
    return true;
}
void PluginPlaylistApi::play(int index)
{ if (player() && index >= 0 && index < count()) player()->playPlaylistIndex(index); }
void PluginPlaylistApi::playNext()
{ if (player()) player()->navigateInPlaylist(true); }
void PluginPlaylistApi::playPrevious()
{ if (player()) player()->navigateInPlaylist(false); }

PluginEventApi::PluginEventApi(PluginRuntime *runtime)
    : PluginApiBase(runtime)
{
    if (!player()) return;
    m_connections.append(connect(
        player()->mpvCore(), &MpvCore::eventReceived, this,
        [this](const MpvEvent &event) {
            dispatch(QStringLiteral("mpv.") + event.name, {event.data});
        }));
    m_connections.append(connect(
        player()->mpvCore(), &MpvCore::propertyChanged, this,
        [this](const QString &name, const QVariant &value) {
            dispatch(QStringLiteral("mpv.") + name
                         + QStringLiteral(".changed"), {value});
        }));
    m_connections.append(connect(
        player(), &PlayerCore::mediaLoaded, this,
        [this](const QUrl &url) {
            dispatch(QStringLiteral("mpv.file-loaded"), {url.toString()});
        }));
    m_connections.append(connect(
        player(), &PlayerCore::mediaEnded, this,
        [this](const MpvEndFileInfo &info) {
            dispatch(QStringLiteral("mpv.end-file"),
                     {info.reason, info.errorCode});
        }));
    if (window()) {
        m_connections.append(connect(
            window(), &MainWindow::fullScreenChanged, this,
            [this](bool value) {
                dispatch(QStringLiteral("iina.fullscreen.changed"),
                         {value});
            }));
    }
}
PluginEventApi::~PluginEventApi()
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
}
QString PluginEventApi::on(
    const QString &event, const QJSValue &callback)
{
    const QStringList parts = event.split(QLatin1Char('.'));
    if (!callback.isCallable()
        || (parts.size() != 2
            && !(parts.size() == 3
                 && parts.last() == QStringLiteral("changed")))
        || (parts.first() != QStringLiteral("mpv")
            && parts.first() != QStringLiteral("iina"))) {
        runtime()->throwError(
            QStringLiteral("Incorrect event name syntax: “%1”.").arg(event));
        return {};
    }
    if (parts.size() == 3 && parts.first() == QStringLiteral("mpv")
        && player()) {
        player()->mpvCore()->observe(parts.at(1), MPV_FORMAT_NODE);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_listeners.insert(id, {event, callback});
    return id;
}
void PluginEventApi::off(
    const QString &event, const QString &identifier)
{
    const auto found = m_listeners.find(identifier);
    if (found != m_listeners.end() && found->event == event)
        m_listeners.erase(found);
}
void PluginEventApi::dispatch(
    const QString &event, const QVariantList &arguments)
{
    const QList<Listener> listeners = m_listeners.values();
    for (const Listener &listener : listeners) {
        if (listener.event != event) continue;
        QJSValueList jsArguments;
        for (const QVariant &argument : arguments)
            jsArguments.append(runtime()->engine()->toScriptValue(argument));
        QJSValue callback = listener.callback;
        const QJSValue result = callback.call(jsArguments);
        if (result.isError())
            Logger::error(QStringLiteral("Plugin event callback failed: %1")
                              .arg(result.toString()));
    }
}

PluginInputApi::PluginInputApi(PluginRuntime *runtime)
    : PluginApiBase(runtime)
{
    if (qApp) qApp->installEventFilter(this);
}
PluginInputApi::~PluginInputApi()
{
    if (qApp) qApp->removeEventFilter(this);
}
QString PluginInputApi::normalizeKeyCode(const QString &code) const
{
    QStringList components =
        code.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (QString &component : components)
        component = component.trimmed().toUpper();
    std::sort(components.begin(), components.end());
    return components.join(QLatin1Char('+'));
}
QString PluginInputApi::onKeyDown(
    const QString &button, const QJSValue &callback, int priority)
{ return add(normalizeKeyCode(button), Kind::KeyDown, callback, priority); }
QString PluginInputApi::onKeyUp(
    const QString &button, const QJSValue &callback, int priority)
{ return add(normalizeKeyCode(button), Kind::KeyUp, callback, priority); }
QString PluginInputApi::onMouseDown(
    const QString &button, const QJSValue &callback, int priority)
{ return add(button, Kind::MouseDown, callback, priority); }
QString PluginInputApi::onMouseUp(
    const QString &button, const QJSValue &callback, int priority)
{ return add(button, Kind::MouseUp, callback, priority); }
QString PluginInputApi::onMouseDrag(
    const QString &button, const QJSValue &callback, int priority)
{ return add(button, Kind::MouseDrag, callback, priority); }
QString PluginInputApi::add(
    const QString &input, Kind kind,
    const QJSValue &callback, int priority)
{
    for (auto it = m_listeners.begin(); it != m_listeners.end();) {
        if (it->input == input && it->kind == kind)
            it = m_listeners.erase(it);
        else ++it;
    }
    if (!callback.isCallable()) return {};
    const QString identifier =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_listeners.append(
        {identifier, input, kind, callback, priority});
    return identifier;
}
bool PluginInputApi::dispatch(
    const QString &input, Kind kind, const QVariantMap &data)
{
    QList<Listener> matches;
    for (const Listener &listener : std::as_const(m_listeners))
        if (listener.input == input && listener.kind == kind)
            matches.append(listener);
    std::sort(matches.begin(), matches.end(),
              [](const Listener &a, const Listener &b) {
        return a.priority > b.priority;
    });
    bool consumed = false;
    for (const Listener &listener : matches) {
        if (listener.priority < 200) {
            QTimer::singleShot(0, this, [this, listener, data] {
                QJSValue callback = listener.callback;
                callback.call(
                    {runtime()->engine()->toScriptValue(data)});
            });
            continue;
        }
        QJSValue callback = listener.callback;
        const QJSValue result = callback.call(
            {runtime()->engine()->toScriptValue(data)});
        if (result.toBool()) {
            consumed = true;
            break;
        }
    }
    return consumed;
}
bool PluginInputApi::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!window() || !widget || widget->window() != window())
        return false;
    if (event->type() == QEvent::KeyPress
        || event->type() == QEvent::KeyRelease) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QString input = normalizeKeyCode(
            QKeySequence(key->keyCombination()).toString());
        const QPoint cursor = window()->mapFromGlobal(QCursor::pos());
        return dispatch(
            input,
            event->type() == QEvent::KeyPress
                ? Kind::KeyDown : Kind::KeyUp,
            {{QStringLiteral("x"), cursor.x()},
             {QStringLiteral("y"), cursor.y()},
             {QStringLiteral("isRepeat"), key->isAutoRepeat()}});
    }
    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QString input;
        const Qt::MouseButton button =
            event->type() == QEvent::MouseMove
            ? (mouse->buttons().testFlag(Qt::LeftButton)
                   ? Qt::LeftButton : Qt::NoButton)
            : mouse->button();
        if (button == Qt::LeftButton) input = QStringLiteral("*mouse");
        else if (button == Qt::RightButton)
            input = QStringLiteral("*rightMouse");
        else if (button != Qt::NoButton)
            input = QStringLiteral("*otherMouse");
        if (input.isEmpty()) return false;
        const QPoint point = window()->mapFromGlobal(mouse->globalPosition().toPoint());
        const Kind kind = event->type() == QEvent::MouseButtonPress
            ? Kind::MouseDown
            : event->type() == QEvent::MouseButtonRelease
                ? Kind::MouseUp : Kind::MouseDrag;
        return dispatch(input, kind,
                        {{QStringLiteral("x"), point.x()},
                         {QStringLiteral("y"), point.y()}});
    }
    return false;
}

QVariant PluginPreferencesApi::get(const QString &key) const
{
    QSettings settings;
    settings.beginGroup(settingsGroup(runtime()->package()));
    QVariant value = settings.value(key);
    if (!value.isValid())
        value = runtime()->package().manifest.preferenceDefaults
                    .value(key).toVariant();
    return value;
}
void PluginPreferencesApi::set(
    const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(settingsGroup(runtime()->package()));
    settings.setValue(key, value);
}
void PluginPreferencesApi::sync() { QSettings().sync(); }

QString PluginFileApi::resolve(const QString &path, bool forWrite)
{
    const QString resolved = runtime()->resolvePluginPath(path);
    if (resolved.isEmpty()) return {};
    if (forWrite) QDir().mkpath(QFileInfo(resolved).absolutePath());
    return resolved;
}
bool PluginFileApi::exists(const QString &path)
{ const QString value = resolve(path); return !value.isEmpty() && QFileInfo::exists(value); }
QVariantList PluginFileApi::list(const QString &path)
{
    QVariantList result;
    const QString value = resolve(path);
    if (value.isEmpty()) return result;
    for (const QFileInfo &entry : QDir(value).entryInfoList(
             QDir::AllEntries | QDir::NoDotAndDotDot)) {
        result.append(QVariantMap{
            {QStringLiteral("filename"), entry.fileName()},
            {QStringLiteral("path"), entry.absoluteFilePath()},
            {QStringLiteral("isDir"), entry.isDir()}});
    }
    return result;
}
QString PluginFileApi::read(const QString &path)
{
    QFile file(resolve(path));
    return file.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(file.readAll()) : QString();
}
bool PluginFileApi::write(
    const QString &path, const QString &content)
{
    const QString value = resolve(path, true);
    if (value.isEmpty()) return false;
    QSaveFile file(value);
    return file.open(QIODevice::WriteOnly)
        && file.write(content.toUtf8()) >= 0 && file.commit();
}
bool PluginFileApi::remove(const QString &path)
{
    const QString value = resolve(path);
    if (value.isEmpty()) return false;
    const QString data = QFileInfo(runtime()->package().dataPath())
                             .canonicalFilePath();
    const QString temp = QFileInfo(runtime()->package().temporaryPath())
                             .canonicalFilePath();
    const QString canonical = QFileInfo(value).canonicalFilePath();
    const auto isInside = [&canonical](const QString &base) {
        return canonical.compare(base, Qt::CaseInsensitive) == 0
            || canonical.startsWith(
                base + QDir::separator(), Qt::CaseInsensitive);
    };
    if (!isInside(data) && !isInside(temp)) {
        runtime()->throwError(
            QStringLiteral("Plugins may delete only private data."));
        return false;
    }
    return QFileInfo(value).isDir()
        ? QDir(value).removeRecursively() : QFile::remove(value);
}
bool PluginFileApi::trash(const QString &path)
{
    const QString value = resolve(path);
    if (value.isEmpty()) return false;
    return QFile::moveToTrash(value);
}
bool PluginFileApi::showInFinder(const QString &path)
{
    const QString value = resolve(path);
    if (value.isEmpty()) return false;
#ifdef Q_OS_WIN
    return QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,%1")
             .arg(QDir::toNativeSeparators(value))});
#else
    return QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(value).absolutePath()));
#endif
}
QString PluginFileApi::resolvePath(const QString &path)
{ return resolve(path); }

void PluginConsoleApi::log(const QVariant &message)
{ Logger::info(QStringLiteral("[%1] %2").arg(runtime()->package().manifest.name, message.toString())); }
void PluginConsoleApi::warn(const QVariant &message)
{ Logger::warn(QStringLiteral("[%1] %2").arg(runtime()->package().manifest.name, message.toString())); }
void PluginConsoleApi::error(const QVariant &message)
{ Logger::error(QStringLiteral("[%1] %2").arg(runtime()->package().manifest.name, message.toString())); }
void PluginConsoleApi::debug(const QVariant &message)
{ Logger::info(QStringLiteral("[%1] %2").arg(runtime()->package().manifest.name, message.toString())); }

bool PluginUtilsApi::fileInPath(const QString &file)
{
    if (!requirePermission(
            PluginPermission::FileSystem,
            QStringLiteral("utils.fileInPath"))) return false;
    return !QStandardPaths::findExecutable(file).isEmpty()
        || QFileInfo::exists(runtime()->resolvePluginPath(file));
}
QString PluginUtilsApi::resolvePath(const QString &path)
{
    if (!requirePermission(
            PluginPermission::FileSystem,
            QStringLiteral("utils.resolvePath"))) return {};
    return runtime()->resolvePluginPath(path);
}
bool PluginUtilsApi::ask(const QString &title)
{
    if (!requirePermission(
            PluginPermission::ShowAlert,
            QStringLiteral("utils.ask"))) return false;
    return QMessageBox::question(
               window(), runtime()->package().manifest.name, title)
        == QMessageBox::Yes;
}
QString PluginUtilsApi::prompt(const QString &title)
{
    if (!requirePermission(
            PluginPermission::ShowAlert,
            QStringLiteral("utils.prompt"))) return {};
    bool accepted = false;
    const QString result = QInputDialog::getMultiLineText(
        window(), runtime()->package().manifest.name,
        title, {}, &accepted);
    return accepted ? result : QString();
}
bool PluginUtilsApi::open(const QString &url)
{
    const QUrl target(url);
    if (target.scheme() == QStringLiteral("http")
        || target.scheme() == QStringLiteral("https"))
        return QDesktopServices::openUrl(target);
    if (!requirePermission(
            PluginPermission::FileSystem,
            QStringLiteral("utils.open"))) return false;
    const QString path = runtime()->resolvePluginPath(url);
    return !path.isEmpty()
        && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
QStringList PluginUtilsApi::preferredLocalizations() const
{
    return QLocale::system().uiLanguages();
}
bool PluginUtilsApi::keychainWrite(
    const QString &service, const QString &name,
    const QString &password)
{
    if (service.isEmpty()) return false;
    return SecureCredentialStore::write(
        QStringLiteral("plugin/%1/%2/%3")
            .arg(runtime()->package().manifest.identifier,
                 service, name),
        password);
}
QVariant PluginUtilsApi::keychainRead(
    const QString &service, const QString &name)
{
    if (service.isEmpty()) return false;
    const QString value = SecureCredentialStore::read(
        QStringLiteral("plugin/%1/%2/%3")
            .arg(runtime()->package().manifest.identifier,
                 service, name));
    return value.isEmpty() ? QVariant(false) : QVariant(value);
}
void PluginUtilsApi::exec(
    const QString &file, const QVariantList &arguments,
    const QString &cwd, const QJSValue &stdoutHook,
    const QJSValue &stderrHook, const QJSValue &resolve,
    const QJSValue &reject)
{
    if (!requirePermission(
            PluginPermission::FileSystem,
            QStringLiteral("utils.exec"))) {
        if (reject.isCallable())
            reject.call({QJSValue(QStringLiteral(
                "file-system permission is required"))});
        return;
    }
    QString executable = file;
    if (file.contains(QLatin1Char('/'))
        || file.contains(QLatin1Char('\\'))) {
        executable = runtime()->resolvePluginPath(file);
    } else {
        const QString found = QStandardPaths::findExecutable(file);
        if (!found.isEmpty()) executable = found;
    }
    if (executable.isEmpty()) {
        if (reject.isCallable())
            reject.call({QJSValue(QStringLiteral(
                "Cannot find executable"))});
        return;
    }
    auto *process = new QProcess(this);
    if (!cwd.isEmpty()) {
        const QString directory = runtime()->resolvePluginPath(cwd);
        if (!directory.isEmpty()) process->setWorkingDirectory(directory);
    }
    QStringList args;
    for (const QVariant &argument : arguments)
        args.append(argument.toString());
    auto stdoutText = std::make_shared<QString>();
    auto stderrText = std::make_shared<QString>();
    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, stdoutText, stdoutHook]() mutable {
        const QString text =
            QString::fromUtf8(process->readAllStandardOutput());
        *stdoutText += text;
        if (stdoutHook.isCallable())
            stdoutHook.call({QJSValue(text)});
    });
    connect(process, &QProcess::readyReadStandardError, this,
            [process, stderrText, stderrHook]() mutable {
        const QString text =
            QString::fromUtf8(process->readAllStandardError());
        *stderrText += text;
        if (stderrHook.isCallable())
            stderrHook.call({QJSValue(text)});
    });
    connect(process, &QProcess::errorOccurred, this,
            [process, reject](QProcess::ProcessError) mutable {
        if (reject.isCallable())
            reject.call({QJSValue(process->errorString())});
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(
                         &QProcess::finished), this,
            [this, process, stdoutText, stderrText, resolve](
                int status, QProcess::ExitStatus) mutable {
        if (resolve.isCallable()) {
            resolve.call({runtime()->engine()->toScriptValue(
                QVariantMap{
                    {QStringLiteral("status"), status},
                    {QStringLiteral("stdout"), *stdoutText},
                    {QStringLiteral("stderr"), *stderrText}})});
        }
        process->deleteLater();
    });
    process->start(executable, args);
}

PluginHttpApi::PluginHttpApi(PluginRuntime *runtime)
    : PluginApiBase(runtime),
      m_network(new QNetworkAccessManager(this))
{
}
bool PluginHttpApi::canAccess(const QUrl &url)
{
    if (!requirePermission(
            PluginPermission::NetworkRequest,
            QStringLiteral("http"))) return false;
    const QString host = url.host().toLower();
    for (const QString &domain :
         runtime()->package().manifest.allowedDomains) {
        if (domain == QStringLiteral("*") || host == domain
            || (domain.startsWith(QStringLiteral("*."))
                && host.endsWith(domain.mid(1))
                && host.size() > domain.size() - 1)) return true;
    }
    runtime()->throwError(
        QStringLiteral("Network domain is not allowed: %1").arg(host));
    return false;
}
void PluginHttpApi::request(
    const QString &method, const QString &url,
    const QVariantMap &options, const QJSValue &resolve,
    const QJSValue &reject)
{
    const QUrl target(url);
    if (!target.isValid() || !canAccess(target)) {
        if (reject.isCallable()) {
            QJSValue callback = reject;
            callback.call({QStringLiteral("Request not permitted")});
        }
        return;
    }
    QNetworkRequest request(target);
    const QVariantMap headers =
        options.value(QStringLiteral("headers")).toMap();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    const QByteArray body =
        options.value(QStringLiteral("data")).toByteArray();
    QNetworkReply *reply = m_network->sendCustomRequest(
        request, method.toUpper().toUtf8(), body);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, resolve, reject, target]() mutable {
        const QUrl finalUrl = reply->url();
        if (!canAccess(finalUrl)) {
            if (reject.isCallable()) reject.call(
                {QJSValue(QStringLiteral(
                    "Redirect domain not permitted"))});
            reply->deleteLater();
            return;
        }
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (reject.isCallable()) reject.call(
                {runtime()->engine()->toScriptValue(QVariantMap{
                    {QStringLiteral("status"),
                     reply->attribute(
                         QNetworkRequest::HttpStatusCodeAttribute)},
                    {QStringLiteral("error"), reply->errorString()}})});
        } else if (resolve.isCallable()) {
            resolve.call({runtime()->engine()->toScriptValue(QVariantMap{
                {QStringLiteral("status"),
                 reply->attribute(
                     QNetworkRequest::HttpStatusCodeAttribute)},
                {QStringLiteral("text"), QString::fromUtf8(data)},
                {QStringLiteral("data"), data},
                {QStringLiteral("url"), target.toString()}})});
        }
        reply->deleteLater();
    });
}
void PluginHttpApi::download(
    const QString &url, const QString &destination,
    const QJSValue &resolve, const QJSValue &reject)
{
    const QUrl target(url);
    const QString output =
        runtime()->resolvePluginPath(destination);
    if (!target.isValid() || !canAccess(target)
        || output.isEmpty()) {
        if (reject.isCallable())
            reject.call({QJSValue(QStringLiteral(
                "Download is not permitted"))});
        return;
    }
    QNetworkReply *reply =
        m_network->get(QNetworkRequest(target));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, output, resolve, reject]() mutable {
        if (!canAccess(reply->url())) {
            if (reject.isCallable())
                reject.call({QJSValue(QStringLiteral(
                    "Redirect domain not permitted"))});
            reply->deleteLater();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            if (reject.isCallable())
                reject.call({QJSValue(reply->errorString())});
            reply->deleteLater();
            return;
        }
        QDir().mkpath(QFileInfo(output).absolutePath());
        QSaveFile file(output);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(reply->readAll()) < 0 || !file.commit()) {
            if (reject.isCallable())
                reject.call({QJSValue(QStringLiteral(
                    "Cannot write downloaded file"))});
        } else if (resolve.isCallable()) {
            resolve.call({QJSValue(output)});
        }
        reply->deleteLater();
    });
}

PluginRuntime::PluginRuntime(
    PluginPackage package, PlayerCore *player,
    MainWindow *window, QObject *parent)
    : QObject(parent),
      m_package(std::move(package)),
      m_player(player),
      m_window(window),
      m_engine(std::make_unique<QJSEngine>())
{
    installApi();
    installPolyfills();
    const QString entry = m_package.entryPath(isGlobal());
    const QJSValue result = evaluateFile(entry, false);
    m_valid = !result.isError();
    if (!m_valid) {
        m_lastError = QStringLiteral("%1:%2: %3\n%4")
            .arg(result.property(QStringLiteral("fileName")).toString())
            .arg(result.property(QStringLiteral("lineNumber")).toInt())
            .arg(result.toString(),
                 result.property(QStringLiteral("stack")).toString());
        Logger::error(QStringLiteral("Plugin %1 failed to load: %2")
                          .arg(m_package.manifest.name, m_lastError));
    }
}
PluginRuntime::~PluginRuntime()
{
    for (QTimer *timer : std::as_const(m_timers)) {
        timer->stop();
        delete timer;
    }
    m_timers.clear();
    if (m_engine) {
        m_engine->setInterrupted(true);
        const auto apis = findChildren<PluginApiBase *>(
            QString(), Qt::FindDirectChildrenOnly);
        qDeleteAll(apis);
        m_engine.reset();
    }
}
void PluginRuntime::throwError(const QString &message)
{
    m_lastError = message;
    m_engine->throwError(message);
    Logger::warn(QStringLiteral("[%1] %2")
                        .arg(m_package.manifest.name, message));
}
QString PluginRuntime::resolvePluginPath(
    const QString &path, bool allowPrivate,
    bool requireFilePermission) const
{
    QString base;
    QString relative = QDir::fromNativeSeparators(path);
    bool privatePath = false;
    if ((relative.startsWith(QStringLiteral("@video/"))
         || relative.startsWith(QStringLiteral("@audio/"))
         || relative.startsWith(QStringLiteral("@sub/")))
        && m_player) {
        const QString type = relative.section(QLatin1Char('/'), 0, 0);
        const int id = relative.section(QLatin1Char('/'), 1, 1).toInt();
        const QList<MediaTrack> *tracks = nullptr;
        if (type == QStringLiteral("@video"))
            tracks = &m_player->info().tracks.videoTracks;
        else if (type == QStringLiteral("@audio"))
            tracks = &m_player->info().tracks.audioTracks;
        else tracks = &m_player->info().tracks.subtitleTracks;
        for (const MediaTrack &track : *tracks) {
            if (track.id != id) continue;
            if (!track.externalFilename.isEmpty())
                return QFileInfo(track.externalFilename).absoluteFilePath();
            if (track.isSelected
                && m_player->info().currentUrl.isLocalFile())
                return m_player->info().currentUrl.toLocalFile();
            return {};
        }
        return {};
    } else if (relative == QStringLiteral("@data")
        || relative.startsWith(QStringLiteral("@data/"))) {
        base = m_package.dataPath();
        relative = relative.mid(5);
        privatePath = true;
    } else if (relative == QStringLiteral("@tmp")
               || relative.startsWith(QStringLiteral("@tmp/"))) {
        base = m_package.temporaryPath();
        relative = relative.mid(4);
        privatePath = true;
    } else if (QFileInfo(relative).isAbsolute()) {
        if (requireFilePermission
            && !m_package.manifest.hasPermission(
                PluginPermission::FileSystem)) return {};
        return QFileInfo(relative).absoluteFilePath();
    } else {
        base = m_package.rootPath;
    }
    if (privatePath && !allowPrivate) return {};
    const QString clean = QDir::cleanPath(QDir(base).filePath(relative));
    const QString absoluteBase = QFileInfo(base).absoluteFilePath();
    if (clean.compare(absoluteBase, Qt::CaseInsensitive) != 0
        && !clean.startsWith(
            absoluteBase + QDir::separator(),
            Qt::CaseInsensitive)) return {};
    if (!privatePath && requireFilePermission
        && !m_package.manifest.hasPermission(
            PluginPermission::FileSystem)
        && !m_package.containsPath(clean)) return {};
    return clean;
}
void PluginRuntime::installApi()
{
    QJSValue iina = m_engine->newObject();
    auto expose = [this, &iina](const QString &name, QObject *api) {
        iina.setProperty(name, m_engine->newQObject(api));
    };
    expose(QStringLiteral("console"), new PluginConsoleApi(this));
    expose(QStringLiteral("preferences"), new PluginPreferencesApi(this));
    expose(QStringLiteral("file"), new PluginFileApi(this));
    expose(QStringLiteral("utils"), new PluginUtilsApi(this));
    expose(QStringLiteral("http"), new PluginHttpApi(this));
    if (!isGlobal()) {
        expose(QStringLiteral("core"), new PluginCoreApi(this));
        expose(QStringLiteral("mpv"), new PluginMpvApi(this));
        expose(QStringLiteral("event"), new PluginEventApi(this));
        expose(QStringLiteral("input"), new PluginInputApi(this));
        expose(QStringLiteral("playlist"), new PluginPlaylistApi(this));
    }
    m_engine->globalObject().setProperty(QStringLiteral("iina"), iina);
    m_engine->globalObject().setProperty(
        QStringLiteral("__runtime"), m_engine->newQObject(this));

    if (!isGlobal()) {
        m_engine->evaluate(QStringLiteral(R"JS(
(() => {
  const c = iina.core;
  const proxy = (read, write, extra = {}) => new Proxy(extra, {
    get(obj, prop) {
      if (prop in obj) return obj[prop];
      return read()[prop];
    },
    set(obj, prop, value) { write(String(prop), value); return true; }
  });
  const readStatus = c.status.bind(c);
  const readWindow = c.windowStatus.bind(c);
  const readAudio = c.audio.bind(c);
  const readVideo = c.video.bind(c);
  const readSubtitle = c.subtitle.bind(c);
  c.status = proxy(readStatus, () => {});
  c.window = proxy(readWindow,
                   (p, v) => c.setWindowProperty(p, v));
  c.audio = proxy(readAudio,
                  (p, v) => c.setAudioProperty(p, v), {
                    loadTrack: p => c.loadTrack("audio", p)
                  });
  c.video = proxy(readVideo,
                  (p, v) => c.setVideoProperty(p, v), {
                    loadTrack: p => c.loadTrack("video", p)
                  });
  c.subtitle = proxy(readSubtitle,
                     (p, v) => c.setSubtitleProperty(p, v), {
                       loadTrack: p => c.loadTrack("subtitle", p)
                     });
})();
)JS"), QStringLiteral("supernova:core-proxies"));
        m_engine->evaluate(QStringLiteral(R"JS(
iina.input.MOUSE = "*mouse";
iina.input.RIGHT_MOUSE = "*rightMouse";
iina.input.OTHER_MOUSE = "*otherMouse";
iina.input.PRIORITY_LOW = 100;
iina.input.PRIORITY_HIGH = 200;
)JS"), QStringLiteral("supernova:input"));
    }
    m_engine->evaluate(QStringLiteral(R"JS(
(() => {
  const native = iina.http;
  const request = (method, url, options = {}) => new Promise(
    (resolve, reject) => native.request(method, url, options, resolve, reject));
  iina.http = {
    request,
    get: (url, options = {}) => request("GET", url, options),
    post: (url, options = {}) => request("POST", url, options),
    put: (url, options = {}) => request("PUT", url, options),
    patch: (url, options = {}) => request("PATCH", url, options),
    delete: (url, options = {}) => request("DELETE", url, options),
    download: (url, destination) => new Promise((resolve, reject) =>
      native.download(url, destination, resolve, reject))
  };
})();
)JS"), QStringLiteral("supernova:http"));
    m_engine->evaluate(QStringLiteral(R"JS(
(() => {
  const u = iina.utils;
  const nativeExec = u.exec.bind(u);
  u.ERROR_BINARY_NOT_FOUND = -1;
  u.ERROR_RUNTIME = -2;
  u.exec = (file, args = [], cwd = "", stdoutHook = null,
            stderrHook = null) => new Promise((resolve, reject) =>
    nativeExec(file, args, cwd || "", stdoutHook, stderrHook,
               resolve, reject));
})();
)JS"), QStringLiteral("supernova:utils"));
    m_engine->evaluate(QStringLiteral(R"JS(
(() => {
  const f = iina.file;
  iina.file = {
    list: (path, options = {}) => f.list(path),
    exists: path => f.exists(path),
    write: (path, content) => f.write(path, content),
    read: (path, options = {}) => f.read(path),
    trash: path => f.trash(path),
    delete: path => f.remove(path),
    showInFinder: path => f.showInFinder(path)
  };
})();
)JS"), QStringLiteral("supernova:file"));
}
void PluginRuntime::installPolyfills()
{
    m_engine->evaluate(QStringLiteral(R"JS(
globalThis.setTimeout = (callback, ms = 0) =>
  __runtime.setTimer(callback, Number(ms), false);
globalThis.setInterval = (callback, ms = 0) =>
  __runtime.setTimer(callback, Number(ms), true);
globalThis.clearTimeout = id => __runtime.clearTimer(String(id));
globalThis.clearInterval = id => __runtime.clearTimer(String(id));
globalThis.require = (() => {
  const cache = {};
  return file => {
    const result = __runtime.requireModule(String(file));
    if (result === undefined || result === null) return undefined;
    if (result.__filename && cache[result.__filename])
      return cache[result.__filename];
    if (result.__filename) cache[result.__filename] = result.exports;
    return result.exports;
  };
})();
)JS"), QStringLiteral("supernova:polyfills"));
}
QJSValue PluginRuntime::evaluateFile(
    const QString &path, bool module)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_engine->throwError(
            QStringLiteral("Cannot read script %1").arg(path));
        return QJSValue(QJSValue::UndefinedValue);
    }
    m_fileStack.append(path);
    const QString content = QString::fromUtf8(file.readAll());
    QString script = content;
    if (module) {
        script = QStringLiteral(
            "(function(){ const module={exports:{}}; const exports="
            "module.exports;\n%1\n; return module.exports; })();")
                     .arg(content);
    }
    const QJSValue result = m_engine->evaluate(script, path);
    m_fileStack.removeLast();
    return result;
}
QJSValue PluginRuntime::requireModule(const QString &path)
{
    const QString parent = m_fileStack.isEmpty()
        ? m_package.rootPath
        : QFileInfo(m_fileStack.last()).absolutePath();
    QString candidate = QDir(parent).filePath(path);
    if (QFileInfo(candidate).suffix().isEmpty())
        candidate += QStringLiteral(".js");
    candidate = QFileInfo(candidate).absoluteFilePath();
    if (!m_package.containsPath(candidate)) {
        throwError(QStringLiteral(
            "require() cannot access files outside the plugin."));
        return {};
    }
    QJSValue exports = evaluateFile(candidate, true);
    QJSValue result = m_engine->newObject();
    result.setProperty(QStringLiteral("__filename"), candidate);
    result.setProperty(QStringLiteral("exports"), exports);
    return result;
}
QString PluginRuntime::setTimer(
    const QJSValue &callback, double milliseconds, bool repeating)
{
    if (!callback.isCallable() || !std::isfinite(milliseconds)) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *timer = new QTimer;
    timer->setSingleShot(!repeating);
    timer->setInterval(
        std::clamp(static_cast<int>(milliseconds), 0, 2147483647));
    connect(timer, &QTimer::timeout, this,
            [this, id, callback, repeating]() mutable {
        QJSValue function = callback;
        const QJSValue result = function.call();
        if (result.isError())
            Logger::error(QStringLiteral("Plugin timer failed: %1")
                              .arg(result.toString()));
        if (!repeating) clearTimer(id);
    });
    m_timers.insert(id, timer);
    timer->start();
    return id;
}
void PluginRuntime::clearTimer(const QString &identifier)
{
    if (QTimer *timer = m_timers.take(identifier)) {
        timer->stop();
        timer->deleteLater();
    }
}
