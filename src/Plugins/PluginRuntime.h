#pragma once

#include "Plugins/PluginPackage.h"

#include <QHash>
#include <QJSValue>
#include <QMetaObject>
#include <QObject>

#include <memory>

class MainWindow;
class PlayerCore;
class QJSEngine;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class PluginRuntime;

class PluginApiBase : public QObject {
    Q_OBJECT

public:
    explicit PluginApiBase(PluginRuntime *runtime);

protected:
    [[nodiscard]] PluginRuntime *runtime() const;
    [[nodiscard]] PlayerCore *player() const;
    [[nodiscard]] MainWindow *window() const;
    [[nodiscard]] bool requirePermission(
        PluginPermission permission, const QString &operation);

private:
    PluginRuntime *m_runtime;
};

class PluginCoreApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;

    Q_INVOKABLE void open(const QString &url);
    Q_INVOKABLE void osd(const QString &message);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds, bool exact = false);
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE QVariantList getChapters() const;
    Q_INVOKABLE void playChapter(int index);
    Q_INVOKABLE QVariantList getHistory() const;
    Q_INVOKABLE QVariantList getRecentDocuments() const;
    Q_INVOKABLE QVariantMap getVersion() const;
    Q_INVOKABLE QVariantMap status() const;
    Q_INVOKABLE QVariantMap windowStatus() const;
    Q_INVOKABLE QVariantMap audio() const;
    Q_INVOKABLE QVariantMap video() const;
    Q_INVOKABLE QVariantMap subtitle() const;
    Q_INVOKABLE void setAudioProperty(
        const QString &name, const QVariant &value);
    Q_INVOKABLE void setVideoProperty(
        const QString &name, const QVariant &value);
    Q_INVOKABLE void setSubtitleProperty(
        const QString &name, const QVariant &value);
    Q_INVOKABLE void setWindowProperty(
        const QString &name, const QVariant &value);
    Q_INVOKABLE void loadTrack(
        const QString &type, const QString &path);
};

class PluginMpvApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;
    ~PluginMpvApi() override;

    Q_INVOKABLE bool getFlag(const QString &property) const;
    Q_INVOKABLE double getNumber(const QString &property) const;
    Q_INVOKABLE QString getString(const QString &property) const;
    Q_INVOKABLE QVariant getNative(const QString &property) const;
    Q_INVOKABLE void set(const QString &property, const QVariant &value);
    Q_INVOKABLE void command(
        const QString &name, const QVariantList &arguments = {});
    Q_INVOKABLE QString addHook(
        const QString &name, int priority, const QJSValue &callback);

private:
    QList<quint64> m_hooks;
};

class PluginPlaylistApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;

    Q_INVOKABLE QVariantList list() const;
    Q_INVOKABLE int count() const;
    Q_INVOKABLE bool add(const QVariant &url, int at = -1);
    Q_INVOKABLE bool remove(const QVariant &index);
    Q_INVOKABLE bool move(int index, int to);
    Q_INVOKABLE void play(int index);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrevious();
};

class PluginEventApi final : public PluginApiBase {
    Q_OBJECT

public:
    explicit PluginEventApi(PluginRuntime *runtime);
    ~PluginEventApi() override;

    Q_INVOKABLE QString on(
        const QString &event, const QJSValue &callback);
    Q_INVOKABLE void off(
        const QString &event, const QString &identifier);

private:
    void dispatch(const QString &event, const QVariantList &arguments);

    struct Listener {
        QString event;
        QJSValue callback;
    };
    QHash<QString, Listener> m_listeners;
    QList<QMetaObject::Connection> m_connections;
};

class PluginInputApi final : public PluginApiBase {
    Q_OBJECT

public:
    explicit PluginInputApi(PluginRuntime *runtime);
    ~PluginInputApi() override;

    Q_INVOKABLE QString normalizeKeyCode(const QString &code) const;
    Q_INVOKABLE QString onKeyDown(
        const QString &button, const QJSValue &callback,
        int priority = 100);
    Q_INVOKABLE QString onKeyUp(
        const QString &button, const QJSValue &callback,
        int priority = 100);
    Q_INVOKABLE QString onMouseDown(
        const QString &button, const QJSValue &callback,
        int priority = 100);
    Q_INVOKABLE QString onMouseUp(
        const QString &button, const QJSValue &callback,
        int priority = 100);
    Q_INVOKABLE QString onMouseDrag(
        const QString &button, const QJSValue &callback,
        int priority = 100);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Kind { KeyDown, KeyUp, MouseDown, MouseUp, MouseDrag };
    struct Listener {
        QString identifier;
        QString input;
        Kind kind = Kind::KeyDown;
        QJSValue callback;
        int priority = 100;
    };
    QString add(
        const QString &input, Kind kind,
        const QJSValue &callback, int priority);
    bool dispatch(
        const QString &input, Kind kind,
        const QVariantMap &data);
    QList<Listener> m_listeners;
};

class PluginPreferencesApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;
    Q_INVOKABLE QVariant get(const QString &key) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);
    Q_INVOKABLE void sync();
};

class PluginFileApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;
    Q_INVOKABLE bool exists(const QString &path);
    Q_INVOKABLE QVariantList list(const QString &path);
    Q_INVOKABLE QString read(const QString &path);
    Q_INVOKABLE bool write(const QString &path, const QString &content);
    Q_INVOKABLE bool remove(const QString &path);
    Q_INVOKABLE bool trash(const QString &path);
    Q_INVOKABLE bool showInFinder(const QString &path);
    Q_INVOKABLE QString resolvePath(const QString &path);

private:
    QString resolve(const QString &path, bool forWrite = false);
};

class PluginConsoleApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;
    Q_INVOKABLE void log(const QVariant &message);
    Q_INVOKABLE void warn(const QVariant &message);
    Q_INVOKABLE void error(const QVariant &message);
    Q_INVOKABLE void debug(const QVariant &message);
};

class PluginUtilsApi final : public PluginApiBase {
    Q_OBJECT

public:
    using PluginApiBase::PluginApiBase;
    Q_INVOKABLE bool fileInPath(const QString &file);
    Q_INVOKABLE QString resolvePath(const QString &path);
    Q_INVOKABLE bool ask(const QString &title);
    Q_INVOKABLE QString prompt(const QString &title);
    Q_INVOKABLE bool open(const QString &url);
    Q_INVOKABLE QStringList preferredLocalizations() const;
    Q_INVOKABLE bool keychainWrite(
        const QString &service, const QString &name,
        const QString &password);
    Q_INVOKABLE QVariant keychainRead(
        const QString &service, const QString &name);
    Q_INVOKABLE void exec(
        const QString &file, const QVariantList &arguments,
        const QString &cwd, const QJSValue &stdoutHook,
        const QJSValue &stderrHook, const QJSValue &resolve,
        const QJSValue &reject);
};

class PluginHttpApi final : public PluginApiBase {
    Q_OBJECT

public:
    explicit PluginHttpApi(PluginRuntime *runtime);
    Q_INVOKABLE void request(
        const QString &method, const QString &url,
        const QVariantMap &options, const QJSValue &resolve,
        const QJSValue &reject);
    Q_INVOKABLE void download(
        const QString &url, const QString &destination,
        const QJSValue &resolve, const QJSValue &reject);

private:
    bool canAccess(const QUrl &url);
    QNetworkAccessManager *m_network = nullptr;
};

class PluginRuntime final : public QObject {
    Q_OBJECT

public:
    PluginRuntime(
        PluginPackage package, PlayerCore *player,
        MainWindow *window, QObject *parent = nullptr);
    ~PluginRuntime() override;

    [[nodiscard]] const PluginPackage &package() const { return m_package; }
    [[nodiscard]] PlayerCore *player() const { return m_player; }
    [[nodiscard]] MainWindow *window() const { return m_window; }
    [[nodiscard]] QJSEngine *engine() const { return m_engine.get(); }
    [[nodiscard]] bool isGlobal() const { return m_player == nullptr; }
    [[nodiscard]] bool valid() const { return m_valid; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString resolvePluginPath(
        const QString &path, bool allowPrivate = true,
        bool requireFilePermission = true) const;
    void throwError(const QString &message);

    Q_INVOKABLE QJSValue requireModule(const QString &path);
    Q_INVOKABLE QString setTimer(
        const QJSValue &callback, double milliseconds, bool repeating);
    Q_INVOKABLE void clearTimer(const QString &identifier);

signals:
    void osdRequested(const QString &message);

private:
    void installApi();
    void installPolyfills();
    QJSValue evaluateFile(const QString &path, bool module);

    PluginPackage m_package;
    PlayerCore *m_player = nullptr;
    MainWindow *m_window = nullptr;
    std::unique_ptr<QJSEngine> m_engine;
    QHash<QString, QTimer *> m_timers;
    QStringList m_fileStack;
    QString m_lastError;
    bool m_valid = false;
};
