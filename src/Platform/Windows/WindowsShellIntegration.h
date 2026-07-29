#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

#include <memory>

class MainWindow;
class PlayerCore;
class QString;

class WindowsShellIntegration final
    : public QObject,
      public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit WindowsShellIntegration(QObject *parent = nullptr);
    ~WindowsShellIntegration() override;

    void registerPlayer(PlayerCore *player, MainWindow *window);

    bool nativeEventFilter(
        const QByteArray &eventType, void *message,
        qintptr *result) override;

private:
    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

class WindowsFileAssociations final {
public:
    [[nodiscard]] static bool isRegistered();
    [[nodiscard]] static bool registerCurrentExecutable(
        QString *error = nullptr);
    [[nodiscard]] static bool unregisterCurrentUser(
        QString *error = nullptr);
    static void openDefaultAppsSettings();
};
