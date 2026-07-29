#pragma once

#include <QObject>
#include <QList>
#include <QUrl>

#include <memory>
#include <vector>

class MainWindow;
class PlayerCore;
class WindowsShellIntegration;

class PlayerWindowManager final : public QObject {
    Q_OBJECT

public:
    explicit PlayerWindowManager(QObject *parent = nullptr);
    ~PlayerWindowManager() override;

    MainWindow *createPlayer(const QList<QUrl> &urls = {});
    void open(const QList<QUrl> &urls, bool forceNewWindow = false);

private:
    struct Session;

    Session *activeSession() const;
    void openInSession(Session *session, const QList<QUrl> &urls);
    void removeSession(MainWindow *window);

    std::vector<std::unique_ptr<Session>> m_sessions;
    std::unique_ptr<WindowsShellIntegration> m_windowsIntegration;
};
